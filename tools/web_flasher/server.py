#!/usr/bin/env python3
"""小智AI-BotHub 本地 Web 烧录工具后端.

仅用 Python 标准库，提供静态页面 + 烧录/擦除/串口探测 REST API。
烧录底层调用 ESP-IDF 的 esptool.py，兼容所有浏览器（不依赖 Web Serial）。
"""
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from collections import deque
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.abspath(os.path.join(HERE, "..", ".."))
BUILD_DIR = os.path.join(PROJECT, "build")
HOST = "127.0.0.1"
PORT = 8765

# ---------- esptool 探测 ----------
def find_esptool():
    """返回可直接执行的 esptool 路径，找不到返回 None。优先新的 esptool 入口。"""
    cands = []
    home = os.path.expanduser("~")
    for p in glob.glob(os.path.join(home, ".espressif", "python_env", "*", "bin", "esptool")):
        cands.append(p)
    for root in (os.path.join(home, "esp", "esp-idf"), os.environ.get("IDF_PATH", "")):
        if root and os.path.isfile(os.path.join(root, "components", "esptool_py", "esptool.py")):
            cands.append(os.path.join(root, "components", "esptool_py", "esptool.py"))
    for p in cands:
        if os.path.isfile(p):
            return p
    which = shutil.which("esptool") or shutil.which("esptool.py")
    return which


def is_new_esptool():
    """esptool >=5 用连字符参数（write-flash / --flash-mode）。"""
    exe = find_esptool() or ""
    try:
        out = subprocess.run([exe, "version"], capture_output=True, text=True, timeout=10).stdout
        m = re.search(r"v?(\d+)\.(\d+)", out)
        if m:
            return (int(m.group(1)), int(m.group(2))) >= (5, 0)
    except Exception:
        pass
    return False


def esptool_cmd(args):
    """构造 esptool 命令。优先用 venv esptool（shebang 自带 python）。"""
    exe = find_esptool()
    if exe:
        return [exe] + args
    # 兜底：source IDF 环境
    home = os.path.expanduser("~")
    export = os.path.join(home, "esp", "esp-idf", "export.sh")
    inner = " ".join(shlex_quote(a) for a in ["esptool.py"] + args)
    return ["bash", "-c", f'source "{export}" >/dev/null 2>&1 && {inner}']


def shlex_quote(s):
    if re.fullmatch(r"[A-Za-z0-9_./:=-]+", s):
        return s
    return "'" + s.replace("'", "'\\''") + "'"


# ---------- 烧录状态 ----------
class FlashState:
    def __init__(self):
        self.lock = threading.Lock()
        self.running = False
        self.logs = deque(maxlen=4000)
        self.seq = 0
        self.progress = 0
        self.stage = "idle"
        self.result = None
        self.proc = None
        self.cancel = False

    def emit(self, text, level="info"):
        with self.lock:
            self.seq += 1
            self.logs.append({"seq": self.seq, "level": level, "text": text})

    def snapshot(self, since=0):
        with self.lock:
            items = [m for m in self.logs if m["seq"] > since]
            return {
                "running": self.running,
                "stage": self.stage,
                "progress": self.progress,
                "result": self.result,
                "latest_seq": self.seq,
                "logs": items,
            }

    def reset(self):
        with self.lock:
            self.running = True
            self.logs.clear()
            self.seq = 0
            self.progress = 0
            self.stage = "starting"
            self.result = None
            self.cancel = False

    def set(self, **kw):
        with self.lock:
            for k, v in kw.items():
                setattr(self, k, v)


STATE = FlashState()


# ---------- 工具函数 ----------
def list_ports():
    ports = []
    for pat in ("/dev/cu.usb*", "/dev/cu.usbmodem*", "/dev/cu.usbserial*",
                "/dev/ttyUSB*", "/dev/ttyACM*", "/dev/cu.SLAB*", "/dev/cu.CH34*"):
        for p in glob.glob(pat):
            if p not in ports and "Bluetooth" not in p:
                ports.append(p)
    return sorted(ports)


def load_default_files():
    """从 build/flasher_args.json 读取默认烧录文件与地址。"""
    path = os.path.join(BUILD_DIR, "flasher_args.json")
    result = []
    settings = {"flash_mode": "dio", "flash_size": "16MB", "flash_freq": "80m"}
    if os.path.isfile(path):
        try:
            with open(path, encoding="utf-8") as f:
                data = json.load(f)
            settings.update(data.get("flash_settings", {}))
            for offset, rel in data.get("flash_files", {}).items():
                full = os.path.join(BUILD_DIR, rel)
                size = os.path.getsize(full) if os.path.isfile(full) else 0
                label = rel.replace("bootloader/", "").replace("partition_table/", "")
                result.append({"offset": offset, "path": rel, "size": size, "label": label})
        except Exception as e:
            STATE.emit(f"读取 flasher_args.json 失败: {e}", "warn")
    if not result:
        for p in sorted(glob.glob(os.path.join(BUILD_DIR, "*.bin"))):
            result.append({"offset": "", "path": os.path.basename(p),
                           "size": os.path.getsize(p), "label": os.path.basename(p)})
    return result, settings


ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def clean_line(line):
    line = line.replace("\r", "\n")
    line = ANSI_RE.sub("", line)
    # 去掉进度条闪烁残留的控制字符
    line = re.sub(r"[\x00-\x08\x0b\x0c\x0e-\x1f]", "", line)
    return line.strip()


def run_esptool(argv, stage_name):
    """后台执行 esptool，实时推送日志。argv 为完整参数列表。"""
    STATE.reset()
    STATE.set(stage=stage_name)
    cmd = esptool_cmd(argv)
    STATE.emit("$ " + " ".join(shlex_quote(c) for c in cmd), "cmd")
    STATE.emit(f"esptool: {find_esptool() or 'env fallback'}", "dim")
    try:
        proc = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, bufsize=1, start_new_session=True,
        )
        STATE.set(proc=proc)
        total_files = len([a for a in argv if a.lower().endswith(".bin")])
        idx = 0
        for raw in proc.stdout:
            if STATE.cancel:
                try:
                    os.killpg(os.getpgid(proc.pid), 15)
                except Exception:
                    proc.terminate()
                break
            for seg in clean_line(raw).split("\n"):
                seg = seg.strip()
                if not seg:
                    continue
                lvl = "info"
                low = seg.lower()
                if "error" in low or "failed" in low or "traceback" in low:
                    lvl = "err"
                elif "warning" in low or "warn" in low:
                    lvl = "warn"
                elif "hash of data verified" in low:
                    lvl = "ok"
                    idx += 1
                    STATE.set(progress=min(99, int(100 * idx / max(total_files, 1))))
                elif "writing at" in low or "wrote " in low:
                    m = re.search(r"(\d+(?:\.\d+)?)%", seg)
                    if m and total_files:
                        frac = (idx + float(m.group(1)) / 100.0) / total_files
                        STATE.set(progress=min(99, int(frac * 100)))
                STATE.emit(seg, lvl)
        rc = proc.wait()
        if STATE.cancel:
            STATE.set(result="cancelled", progress=0, stage="idle", running=False)
            STATE.emit("已取消烧录", "warn")
        elif rc == 0:
            STATE.set(result="success", progress=100, stage="idle", running=False)
            STATE.emit("烧录完成，设备已复位。", "ok")
        else:
            STATE.set(result=f"failed (exit {rc})", stage="idle", running=False)
            STATE.emit(f"烧录失败，退出码 {rc}", "err")
    except Exception as e:
        STATE.set(result="error", stage="idle", running=False)
        STATE.emit(f"启动 esptool 异常: {e}", "err")


# ---------- HTTP 处理 ----------
class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _send(self, code, body, ctype="application/json; charset=utf-8"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body, ensure_ascii=False).encode("utf-8")
        elif isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _json_body(self):
        length = int(self.headers.get("Content-Length", 0))
        if not length:
            return {}
        try:
            return json.loads(self.rfile.read(length))
        except Exception:
            return {}

    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        path = u.path
        if path == "/api/status":
            since = int(q.get("since", ["0"])[0])
            return self._send(200, STATE.snapshot(since))
        if path == "/api/ports":
            return self._send(200, {"ports": list_ports(), "current": list_ports()[0] if list_ports() else ""})
        if path == "/api/firmware":
            files, settings = load_default_files()
            return self._send(200, {"files": files, "settings": settings, "build_dir": BUILD_DIR})
        if path == "/api/esptool":
            return self._send(200, {"path": find_esptool() or "env-fallback"})
        if path in ("/", "/index.html"):
            return self._serve_file(os.path.join(HERE, "index.html"), "text/html; charset=utf-8")
        if path in ("/app.js", "/style.css"):
            ctype = "application/javascript; charset=utf-8" if path.endswith(".js") else "text/css; charset=utf-8"
            return self._serve_file(os.path.join(HERE, path.lstrip("/")), ctype)
        return self._send(404, {"error": "not found"})

    def _serve_file(self, path, ctype):
        if not os.path.isfile(path):
            return self._send(404, {"error": "file not found"})
        with open(path, "rb") as f:
            return self._send(200, f.read(), ctype)

    def do_POST(self):
        path = urlparse(self.path).path
        data = self._json_body()
        if path == "/api/flash":
            if STATE.running:
                return self._send(409, {"error": "烧录正在进行中"})
            port = data.get("port", "")
            baud = str(data.get("baud", "460800"))
            files = data.get("files", [])
            if not port:
                return self._send(400, {"error": "请选择串口"})
            if not files:
                return self._send(400, {"error": "没有选择固件文件"})
            if is_new_esptool():
                wf, fm, fs, ff = "write-flash", "--flash-mode", "--flash-size", "--flash-freq"
            else:
                wf, fm, fs, ff = "write_flash", "--flash_mode", "--flash_size", "--flash_freq"
            argv = ["--port", port, "--baud", baud, "--chip", data.get("chip", "esp32s3"),
                    "--before", "default-reset", "--after", "hard-reset", wf]
            argv += [fm, data.get("flash_mode", "dio"),
                     fs, data.get("flash_size", "16MB"),
                     ff, data.get("flash_freq", "80m")]
            for item in files:
                rel = item["path"]
                full = rel if os.path.isabs(rel) else os.path.join(BUILD_DIR, rel)
                if not os.path.isfile(full):
                    return self._send(400, {"error": f"文件不存在: {rel}"})
                argv += [str(item["offset"]), full]
            threading.Thread(target=run_esptool, args=(argv, "flash"), daemon=True).start()
            return self._send(200, {"ok": True})
        if path == "/api/erase":
            if STATE.running:
                return self._send(409, {"error": "烧录正在进行中"})
            port = data.get("port", "")
            if not port:
                return self._send(400, {"error": "请选择串口"})
            argv = ["--port", port, "--baud", str(data.get("baud", "460800")),
                    "--chip", data.get("chip", "esp32s3"), "erase_flash"]
            threading.Thread(target=run_esptool, args=(argv, "erase"), daemon=True).start()
            return self._send(200, {"ok": True})
        if path == "/api/stop":
            STATE.set(cancel=True)
            return self._send(200, {"ok": True})
        return self._send(404, {"error": "not found"})


def main():
    print(f"小智AI-BotHub Web 烧录工具")
    print(f"工程目录: {PROJECT}")
    print(f"esptool : {find_esptool() or 'IDF env fallback'}")
    print(f"串口列表: {', '.join(list_ports()) or '未检测到'}")
    print(f"打开浏览器: http://{HOST}:{PORT}")
    print("按 Ctrl+C 停止服务")
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n服务已停止")
        server.shutdown()


if __name__ == "__main__":
    main()
