/*
 * 板级 IO 扩展 — I2C 总线 + PCA9557
 * 立创实战派板上 LCD 复位、功放使能、摄像头电源都挂在 PCA9557 上，
 * 这里统一管理，避免 display/audio/camera 各自初始化造成冲突。
 */
#pragma once
#include "bothub.h"

/** 初始化 I2C 总线与 PCA9557（配置方向寄存器、保持默认输出）。幂等。 */
esp_err_t board_io_init(void);

/** 读改写 PCA9557 输出寄存器 0x01 的某一位。 */
esp_err_t board_io_set_output(uint8_t bit, uint8_t level);

/** 获取 I2C 总线句柄（供 ES8311/ES7210 的 ctrl_if 使用）。 */
void* board_io_get_i2c_bus(void);
