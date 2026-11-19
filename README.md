# 小智AI-BotHub

ESP32-S3 上的**拍照 / 录音 / 回放**小工具。基于 ESP-IDF v6.1 原生组件从零构建，不依赖小智云端框架。所有照片与录音保存在板载 Flash 的 SPIFFS 分区，可离线使用。

> GitHub 仓库名只支持 ASCII 字符，因此远端仓库名为 `XiaoZhi-BotHub`（URL 中显示为小写 `xiaozhi-bothub`）；项目显示名为“小智AI-BotHub”。

## 功能

| 功能 | 说明 |
| --- | --- |
| 📷 拍照 | GC0308 摄像头输出 JPEG（QVGA 320×240），保存到 `/storage/photo_XXX.jpg` |
| 🎙️ 录音 | ES7210 麦克风采集，16bit/24kHz 单声道 WAV，保存到 `/storage/rec_XXX.wav` |
| 🔊 回放 | 播放 SPIFFS 中的 WAV，经 ES8311 从扬声器输出，双击切换下一段 |
| 🖥️ 状态显示 | ST7789 屏幕实时显示模式、照片/录音数量、存储占用 |

## 硬件

本工程针对**立创实战派小智 AI 智能体开发板**：

- **主控**：ESP32-S3-WROOM-1 N16R8（16MB Flash / 8MB PSRAM）
- **摄像头**：GC0308，DVP 并口，JPEG 输出
- **显示屏**：ST7789，240×320，SPI3
- **音频**：ES8311（DAC/扬声器）+ ES7210（ADC/麦克风），I2S0
- **功放使能**：PCA9557 IO 扩展（I2C 0x19）Bit1
- **按键**：BOOT（GPIO0）

### 引脚连接

完整的引脚定义在 [`main/board_config.h`](main/board_config.h)，摘要如下：

| 外设 | 引脚 |
| --- | --- |
| I2S MCLK / BCLK / WS | GPIO38 / GPIO14 / GPIO13 |
| I2S DOUT（→ES8311）/ DIN（←ES7210） | GPIO45 / GPIO12 |
| I2C SDA / SCL | GPIO1 / GPIO2 |
| 摄像头 D0–D7 | GPIO16,18,8,17,15,6,4,9 |
| 摄像头 XCLK / PCLK / VSYNC / HREF | GPIO5 / GPIO7 / GPIO3 / GPIO46 |
| LCD MOSI / SCLK / DC | GPIO40 / GPIO41 / GPIO39 |
| 背光 / BOOT | GPIO42 / GPIO0 |

## 使用方法

### 编译烧录

```sh
source ~/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

### 图形界面烧录（本地 Web 工具）

不想敲命令？双击 `tools/web_flasher/start.command`，浏览器会自动打开 `http://127.0.0.1:8765`，即可在网页里选择串口、勾选固件、改烧录地址、查看实时日志并一键烧录/擦除。

- **无需联网、无需安装第三方库**（纯 Python 标准库 + 系统 ESP-IDF 的 esptool）；兼容 Chrome/Safari/Firefox 等所有浏览器。
- 固件列表和烧录地址自动从 `build/flasher_args.json` 读取；也可手动指定任意 bin 与地址。
- 支持 ESP32-S3/C3/C6/ESP32，波特率、Flash 模式/大小/频率均可在页面上调整。
- 停止服务：在运行窗口按 `Ctrl+C`。

### 操作

**BOOT 按键：**
- **单击** → 拍照
- **长按**（>1.2 秒）→ 开始录音，再长按停止
- **双击** → 播放下一段录音

**串口命令**（monitor 中直接输入）：

```
photo    拍照
rec      开始/停止录音
play     播放下一段
ls       列出所有文件
status   查看状态与存储占用
vol70    设置音量（0-100）
```

## 存储容量

16MB Flash 中约 7.8MB 分配给 SPIFFS。参考：
- 一张 QVGA JPEG 约 10–20KB → 可存数百张
- 24kHz/16bit 单声道 WAV 约 48KB/秒 → 可存约 2–3 分钟录音

## 目录结构

```
├── main/
│   ├── app_main.cc        主程序、状态机、按键与串口命令
│   ├── bothub.h           模块接口与全局状态
│   ├── board_config.h     板级引脚定义（硬件结构）
│   ├── audio_driver.cc    ES8311/ES7210 + I2S 驱动
│   ├── camera_driver.cc   GC0308 拍照
│   ├── display_driver.cc  ST7789 状态显示
│   ├── storage.cc         SPIFFS 文件读写
│   ├── wav_codec.cc       WAV 头封装/解析
│   └── font5x7.h          显示用点阵字体
├── partitions/partitions_16mb.csv
├── sdkconfig.defaults
├── tools/web_flasher/       本地 Web 烧录工具
│   ├── start.command        双击启动
│   ├── server.py            后端服务（标准库）
│   ├── index.html           页面
│   ├── app.js / style.css   前端逻辑与样式
│   └── README.md            工具说明
└── README.md
```

## 说明

本仓库为全新重建工程。原 ESP32S3-Jarvis 项目的完整备份保存在仓库外（含原始 git 历史、天气应用、固件备份与项目记忆）。
