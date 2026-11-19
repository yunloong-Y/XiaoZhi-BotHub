/*
 * 板级 IO 扩展实现 — I2C master + PCA9557（地址 0x19）
 * 对照原厂 lichuang_dev_board.cc 已验证配置：
 *   Pca9557 构造: WriteReg(0x01, 0x03); WriteReg(0x03, 0xf8);
 *   SetOutputState(bit, level): 读 0x01 -> 改位 -> 写 0x01
 */
#include "board_io.h"
#include "board_config.h"

#include <esp_log.h>
#include <esp_check.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "board_io";

static i2c_master_bus_handle_t s_bus = nullptr;
static i2c_master_dev_handle_t s_pca9557 = nullptr;

static esp_err_t pca_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_pca9557, buf, 2, pdMS_TO_TICKS(200));
}

static esp_err_t pca_read_reg(uint8_t reg, uint8_t* val) {
    return i2c_master_transmit_receive(s_pca9557, &reg, 1, val, 1, pdMS_TO_TICKS(200));
}

esp_err_t board_io_init(void) {
    if (s_bus) return ESP_OK;  /* 幂等 */

    i2c_master_bus_config_t cfg = {
        .i2c_port = AUDIO_CODEC_I2C_PORT,
        .sda_io_num = AUDIO_CODEC_I2C_SDA,
        .scl_io_num = AUDIO_CODEC_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = { .enable_internal_pullup = true },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &s_bus));

    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCA9557_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev, &s_pca9557));

    // 与原厂一致：先设输出初值，再配方向（Bit0~2 输出）
    ESP_ERROR_CHECK(pca_write_reg(0x01, PCA9557_OUTPUT_INIT));
    ESP_ERROR_CHECK(pca_write_reg(0x03, PCA9557_CONFIG_OUTPUT));

    ESP_LOGI(TAG, "I2C bus + PCA9557 @0x19 ready");
    return ESP_OK;
}

esp_err_t board_io_set_output(uint8_t bit, uint8_t level) {
    if (!s_pca9557) return ESP_ERR_INVALID_STATE;
    uint8_t data = 0;
    esp_err_t ret = pca_read_reg(0x01, &data);
    if (ret != ESP_OK) return ret;
    data = (data & ~(1 << bit)) | ((level & 1) << bit);
    return pca_write_reg(0x01, data);
}

void* board_io_get_i2c_bus(void) {
    return (void*)s_bus;
}
