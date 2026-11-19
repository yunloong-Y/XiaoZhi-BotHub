#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
小智AI-BotHub 照片拉取脚本
通过串口 CLI 的 dump 命令把设备 SPIFFS 里的所有 .jpg 导出到电脑。

用法:
    python3 pull_photos.py              # 拉取全部照片，保存到 photos/<时间戳>/
    python3 pull_photos.py --list       # 只列出设备上的照片，不下载
    python3 pull_photos.py --out ~/Desk # 指定输出目录（默认项目 photos/）
    python3 pull_photos.py --flat       # 不建时间戳子目录，直接放输出目录
依赖: pyserial（IDF 的 python 自带；系统 python 没有时脚本会自动切换 IDF python）
"""
import sys, os, time, glob, base64, argparse

# ---------- bootstrap: 若当前解释器没有 pyserial，尝试切到 IDF python ----------
try:
    import serial  # noqa: F401
except ImportError:
    idf_py = None
    # ESP-IDF python venv 常见位置（~/.espressif/python_env/idfX.Y_pyZ.W_env/）
    for cand in sorted(glob.glob(os.path.expanduser(
            "~/.espressif/python_env/*_env/bin/python3")), reverse=True):
        idf_py = cand; break
    if idf_py is None:
        for cand in sorted(glob.glob(os.path.expanduser(
                "~/esp/*/python_env/*env*/bin/python3")), reverse=True):
            idf_py = cand; break
    if idf_py and os.path.exists(idf_py):
        print(f"(当前 python 无 pyserial，自动切换: {idf_py})")
        os.execv(idf_py, [idf_py] + sys.argv)
    print("错误: 找不到 pyserial，且未发现 IDF python 环境。")
    print("请先: source ~/esp/esp-idf/export.sh 后再运行，或 pip3 install pyserial")
    sys.exit(1)

import serial as _serial

BAUD = 115200
PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ---------- 串口工具 ----------
def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.usbserial*"))
    if not ports:
        print("错误: 未找到设备串口（/dev/cu.usbmodem*）。请插好 USB 线后重试。")
        sys.exit(1)
    return ports[0]

def open_port(port):
    s = _serial.Serial(port, BAUD, timeout=0.3)
    time.sleep(1.5)          # 等设备 CLI 就绪
    s.reset_input_buffer()
    return s

def send(s, cmd, wait=4, until=None, max_wait=120):
    """发命令并收输出；until 出现即停"""
    s.write((cmd + "\n").encode())
    buf = b""
    t0 = time.time()
    while time.time() - t0 < max_wait:
        if s.in_waiting:
            buf += s.read(s.in_waiting)
            if until and until.encode() in buf:
                break
        if time.time() - t0 > wait and not until:
            break
        time.sleep(0.05)
    return buf.decode("utf-8", "replace")

# ---------- 业务 ----------
def list_jpgs(s):
    txt = send(s, "ls", wait=3)
    files = []
    for ln in txt.splitlines():
        if "/storage/" in ln and ln.strip().endswith(".jpg"):
            files.append(ln.strip().split()[-1])
    return files

B64SET = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=")

def extract_b64(txt):
    """从串口输出中提取纯 base64 流。
    固件每行输出恰 200 个 base64 字符；其他任务日志行（含空格/括号/冒号等）
    会与 dump 输出交织，凡整行不是纯 base64 的行一律剔除。"""
    seg = txt.split("BEGIN", 1)[1]
    if "dump: END" in seg:
        seg = seg.split("dump: END", 1)[0]
    out = []
    for ln in seg.splitlines():
        s = ln.strip()
        if s and set(s) <= B64SET:
            out.append(s)
    return "".join(out)

def pull_file(s, path, out_path, retries=3):
    """拉单个文件，失败自动重试"""
    last_err = ""
    for attempt in range(1, retries + 1):
        if attempt > 1:
            time.sleep(1.0)
            s.reset_input_buffer()
        txt = send(s, f"dump {path}", until="dump: END", max_wait=180)
        if "BEGIN" not in txt or "dump: END" not in txt:
            last_err = "no BEGIN/END in output"
            continue
        chars = extract_b64(txt)
        try:
            import base64 as _b64
            raw = _b64.b64decode(chars)
        except Exception as e:
            last_err = f"b64 decode fail (attempt {attempt}): {e}"
            continue
        if not (raw[:2] == b"\xff\xd8" and raw[-2:] == b"\xff\xd9"):
            last_err = f"bad jpeg marker head={raw[:2].hex()} tail={raw[-2:].hex()}"
            continue
        open(out_path, "wb").write(raw)
        return True, f"{len(raw)} bytes" + (f" (第{attempt}次成功)" if attempt > 1 else "")
    return False, last_err

def main():
    ap = argparse.ArgumentParser(description="拉取 BotHub 设备照片")
    ap.add_argument("--list", action="store_true", help="只列出照片不下载")
    ap.add_argument("--out", default=None, help="输出目录（默认 项目/photos/）")
    ap.add_argument("--flat", action="store_true", help="不加时间戳子目录")
    args = ap.parse_args()

    port = find_port()
    print(f"[1/3] 连接设备 {port} ...")
    s = open_port(port)

    print("[2/3] 读取设备照片列表 ...")
    jpgs = list_jpgs(s)
    if not jpgs:
        print("     设备上没有照片。（拍一张后再运行本脚本）")
        s.close(); return
    print(f"     共 {len(jpgs)} 张:")
    for f in jpgs:
        print(f"       - {f}")

    if args.list:
        s.close(); return

    outdir = args.out or os.path.join(PROJECT, "photos")
    if not args.flat:
        outdir = os.path.join(outdir, time.strftime("%Y%m%d-%H%M%S"))
    os.makedirs(outdir, exist_ok=True)

    print(f"[3/3] 导出 {len(jpgs)} 张照片 -> {outdir} ...")
    ok = fail = 0
    for f in jpgs:
        name = os.path.basename(f)
        outp = os.path.join(outdir, name)
        t0 = time.time()
        good, msg = pull_file(s, f, outp)
        dt = time.time() - t0
        if good:
            ok += 1
            print(f"       ✓ {name}  {msg}  ({dt:.1f}s)")
        else:
            fail += 1
            print(f"       ✗ {name}  失败: {msg}  ({dt:.1f}s)")
    s.close()
    print(f"\n完成: 成功 {ok} 张, 失败 {fail} 张")
    print(f"保存位置: {os.path.abspath(outdir)}")
    if fail == 0:
        print(f"打开目录: open \"{os.path.abspath(outdir)}\"")

if __name__ == "__main__":
    main()
