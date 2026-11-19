/*
 * 小智AI-BotHub — 立创实战派 ESP32-S3 板级硬件定义
 *
 * 板卡：立创实战派小智 AI 智能体开发板
 * 主控：ESP32-S3-WROOM-1 N16R8（16MB Flash / 8MB PSRAM，八线 PSRAM）
 * 摄像头：GC0308（DVP 并口，JPEG 输出）
 * 显示屏：ST7789，240x320，SPI3，FT5x06 电容触摸（本工具未用触摸）
 * 音频：ES8311 DAC（扬声器）+ ES7210 ADC（麦克风），I2S0
 * 扩展：PCA9557 IO 扩展（I2C 0x19），Bit1 控制功放使能
 * 按键：BOOT（GPIO0）低电平有效
 */
#pragma once

#include <driver/gpio.h>

/* ---------- 音频 ---------- */
#define AUDIO_SAMPLE_RATE        24000

#define AUDIO_I2S_GPIO_MCLK      GPIO_NUM_38
#define AUDIO_I2S_GPIO_BCLK      GPIO_NUM_14
#define AUDIO_I2S_GPIO_WS        GPIO_NUM_13
#define AUDIO_I2S_GPIO_DIN       GPIO_NUM_12   /* ES7210 -> ESP32 */
#define AUDIO_I2S_GPIO_DOUT      GPIO_NUM_45   /* ESP32 -> ES8311 */

#define AUDIO_CODEC_I2C_SDA      GPIO_NUM_1
#define AUDIO_CODEC_I2C_SCL      GPIO_NUM_2
#define AUDIO_CODEC_I2C_PORT     I2C_NUM_1
#define ES8311_I2C_ADDR          (0x30)
#define ES7210_I2C_ADDR          (0x82)  /* 8-bit 形式，高位为 R/W */

#define PCA9557_I2C_ADDR         (0x19)
#define PCA9557_PA_BIT           (1)     /* Bit1: 功放使能，高电平开启 */

/* ---------- 按键 ---------- */
#define BOOT_BUTTON_GPIO         GPIO_NUM_0

/* ---------- 显示屏 ST7789 (SPI3) ---------- */
#define DISPLAY_WIDTH            320
#define DISPLAY_HEIGHT           240
#define LCD_SPI_HOST             SPI3_HOST
#define LCD_PIN_MOSI             GPIO_NUM_40
#define LCD_PIN_SCLK             GPIO_NUM_41
#define LCD_PIN_DC               GPIO_NUM_39
#define LCD_PIN_CS               GPIO_NUM_NC  /* 常低，未接 CS */
#define LCD_PIN_RST              GPIO_NUM_NC
#define LCD_PIN_BACKLIGHT        GPIO_NUM_42
#define DISPLAY_MIRROR_X         true
#define DISPLAY_MIRROR_Y         false
#define DISPLAY_SWAP_XY          true

/* ---------- 摄像头 GC0308 (DVP) ---------- */
#define CAMERA_PIN_PWDN          GPIO_NUM_NC
#define CAMERA_PIN_RESET         GPIO_NUM_NC
#define CAMERA_PIN_XCLK          GPIO_NUM_5
#define CAMERA_PIN_SIOD          GPIO_NUM_1
#define CAMERA_PIN_SIOC          GPIO_NUM_2
#define CAMERA_PIN_D7            GPIO_NUM_9
#define CAMERA_PIN_D6            GPIO_NUM_4
#define CAMERA_PIN_D5            GPIO_NUM_6
#define CAMERA_PIN_D4            GPIO_NUM_15
#define CAMERA_PIN_D3            GPIO_NUM_17
#define CAMERA_PIN_D2            GPIO_NUM_8
#define CAMERA_PIN_D1            GPIO_NUM_18
#define CAMERA_PIN_D0            GPIO_NUM_16
#define CAMERA_PIN_VSYNC         GPIO_NUM_3
#define CAMERA_PIN_HREF          GPIO_NUM_46
#define CAMERA_PIN_PCLK          GPIO_NUM_7
#define CAMERA_XCLK_FREQ_HZ      20000000

/* ---------- 存储 ---------- */
#define STORAGE_PARTITION_NAME   "storage"
