#!/bin/bash
# 小智AI-BotHub 本地 Web 烧录工具 —— 双击即可启动
cd "$(dirname "$0")" || exit 1

echo "=============================================="
echo "  小智AI-BotHub · 本地 Web 固件烧录工具"
echo "=============================================="
echo ""

# 优先用系统 python3
PY=$(command -v python3 || command -v python)
if [ -z "$PY" ]; then
  echo "未找到 Python 3，请先安装。"
  read -r -p "按回车退出..."
  exit 1
fi

# 检查端口是否被占用
if lsof -ti tcp:8765 >/dev/null 2>&1; then
  PID=$(lsof -ti tcp:8765 | head -1)
  echo "端口 8765 已被占用 (PID $PID)，可能是工具已在运行。"
  echo "直接在浏览器打开: http://127.0.0.1:8765"
  open "http://127.0.0.1:8765" 2>/dev/null
  read -r -p "按回车退出..."
  exit 0
fi

echo "启动本地服务..."
# 延迟打开浏览器
( sleep 1; open "http://127.0.0.1:8765" 2>/dev/null ) &

"$PY" server.py
STATUS=$?

if [ $STATUS -ne 0 ]; then
  echo ""
  echo "服务异常退出 (code $STATUS)。"
fi
read -r -p "按回车关闭窗口..."
