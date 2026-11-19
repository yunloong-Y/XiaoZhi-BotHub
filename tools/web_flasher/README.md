# 小智AI-BotHub · 本地 Web 烧录工具

一个可在浏览器里给 ESP32 烧录固件的本地小工具。**无需联网、无需 pip 安装任何库**，底层直接调用系统 ESP-IDF 自带的 esptool，兼容所有浏览器（不依赖 Web Serial）。

## 启动

双击本目录下的 `start.command`，浏览器会自动打开 `http://127.0.0.1:8765`。

或手动启动：

```sh
cd tools/web_flasher
python3 server.py
# 浏览器打开 http://127.0.0.1:8765
```

停止服务：在运行窗口按 `Ctrl+C`。

## 使用步骤

1. **串口设备**：选择板子对应的串口（如 `/dev/cu.usbmodem11301`）。若没出现，点右侧 ↻ 重新扫描，并检查 USB 连接与驱动。
2. **芯片 / 波特率**：默认 ESP32-S3 / 460800，按需调整。
3. **Flash 参数**：默认从 `build/flasher_args.json` 自动填入（dio / 16MB / 80m）。
4. **固件文件**：默认勾选 `build/` 下 4 个 bin 并填好烧录地址。可勾选部分文件、手动改地址或改指向其它 bin。
5. 点击 **▶ 开始烧录**，实时日志与进度会显示在右侧。完成后设备自动复位。

**擦除 Flash**：点 `擦除 Flash`（不可恢复，谨慎使用）。
**中断**：烧录中点 `■ 停止`。

## 实现说明

- 后端 `server.py` 用 Python 标准库 `http.server` 实现，仅监听 `127.0.0.1`，不对外网开放。
- `/api/firmware` 读取 `build/flasher_args.json` 得到默认的固件路径、烧录地址与 Flash 参数。
- `/api/flash`、`/api/erase` 调用 esptool，日志通过 `/api/status?since=N` 轮询实时推送到页面。
- esptool 探测顺序：`~/.espressif/python_env/*/bin/esptool` → IDF 的 `components/esptool_py` → PATH。
- 自动适配 esptool 版本：≥5 用 `esptool` 入口与连字符参数，旧版回退到 `esptool.py` 与下划线参数。
- esptool 输出中的 ANSI 转义、`\r` 进度条会被清洗后再显示。

## 目录

```
start.command   双击启动脚本（自动开浏览器）
server.py       HTTP 服务 + 烧录 API
index.html      页面结构
app.js          前端交互逻辑
style.css       页面样式（浅色主题）
```
