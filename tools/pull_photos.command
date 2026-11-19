#!/bin/bash
# 双击拉取小智AI-BotHub设备照片 -> 项目 photos/<时间戳>/
cd "$(dirname "$0")/.." || true
PROJECT="/Users/alpha/Desktop/yunlong/workproject/yunze/ESP32S3-Jarvis"
cd "$PROJECT" || { echo "找不到项目目录"; read -r -p "按回车退出"; exit 1; }

echo "=========================================="
echo "  小智AI-BotHub 照片拉取"
echo "=========================================="
python3 tools/pull_photos.py "$@"
rc=$?
echo ""
if [ $rc -ne 0 ]; then read -r -p "出错了，按回车退出"; fi
# 成功时也停一下，方便看结果
read -r -p "按回车关闭窗口"
exit $rc
