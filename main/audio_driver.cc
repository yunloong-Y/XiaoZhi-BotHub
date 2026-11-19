/*
 * 音频驱动 — ES8311(DAC 播放) + ES7210(ADC 录音) + I2S0
 *
 * 参考立创实战派板已验证配置：
 *   - TX(std 模式, 单声道输出) -> ES8311 -> 扬声器
 *   - RX(tdm 模式, 4 slot)     <- ES7210 <- 麦克风
 *   - PCA9557 Bit1 控制功放使能
 */
#include "bothub.h"
#include "board_config.h"
#include "board_io.h"

#include <esp_log.h>
#include <esp_check.h>
#include <driver/i2c_master.h>
#include <driver/i2s_tdm.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

static const char* TAG = "audio";

/* I2S DMA 配置（沿用已验证参数） */
#define DMA_DESC_NUM   6
#define DMA_FRAME_NUM  240

static i2c_master_bus_handle_t i2c_bus = nullptr;  /* 复用 board_io 的总线 */

static i2s_chan_handle_t tx_handle = nullptr;
static i2s_chan_handle_t rx_handle = nullptr;

static const audio_codec_data_if_t* data_if = nullptr;
static const audio_codec_gpio_if_t* gpio_if = nullptr;
/* ES8311 输出 */
static const audio_codec_ctrl_if_t* out_ctrl_if = nullptr;
static const audio_codec_if_t* out_codec_if = nullptr;
static esp_codec_dev_handle_t out_dev = nullptr;
/* ES7210 输入 */
static const audio_codec_ctrl_if_t* in_ctrl_if = nullptr;
static const audio_codec_if_t* in_codec_if = nullptr;
static esp_codec_dev_handle_t in_dev = nullptr;

static bool input_open = false;
static bool output_open = false;
static int cur_volume = 70;

/* ---------- PCA9557：控制功放（复用 board_io 的 I2C/PCA9557） ---------- */
esp_err_t audio_set_pa(bool enable) {
    return board_io_set_output(PCA9557_PA_BIT, enable ? 1 : 0);
}

static esp_err_t i2c_init(void) {
    // I2C 总线与 PCA9557 由 board_io 统一初始化（display 先行调用）
    ESP_ERROR_CHECK(board_io_init());
    i2c_bus = (i2c_master_bus_handle_t)board_io_get_i2c_bus();
    audio_set_pa(false);  // 默认关闭功放
    return ESP_OK;
}

/* ---------- I2S 双通道（TX std / RX tdm） ---------- */
static esp_err_t i2s_init(void) {
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = DMA_DESC_NUM,
        .dma_frame_num = DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
        },
        .gpio_cfg = {
            .mclk = AUDIO_I2S_GPIO_MCLK,
            .bclk = AUDIO_I2S_GPIO_BCLK,
            .ws = AUDIO_I2S_GPIO_WS,
            .dout = AUDIO_I2S_GPIO_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));

    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = {
            .sample_rate_hz = AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .bclk_div = 8,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = (i2s_tdm_slot_mask_t)(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 |
                                               I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
            .ws_width = I2S_TDM_AUTO_WS_WIDTH,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = false,
            .total_slot = I2S_TDM_AUTO_SLOT_NUM,
        },
        .gpio_cfg = {
            .mclk = AUDIO_I2S_GPIO_MCLK,
            .bclk = AUDIO_I2S_GPIO_BCLK,
            .ws = AUDIO_I2S_GPIO_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = AUDIO_I2S_GPIO_DIN,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_tdm_mode(rx_handle, &tdm_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
    ESP_LOGI(TAG, "I2S duplex channels ready");
    return ESP_OK;
}

esp_err_t audio_init(void) {
    i2c_init();
    i2s_init();

    audio_codec_i2s_cfg_t i2s_if_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = rx_handle,
        .tx_handle = tx_handle,
    };
    data_if = audio_codec_new_i2s_data(&i2s_if_cfg);
    gpio_if = audio_codec_new_gpio();

    /* ---- ES8311 输出 ---- */
    audio_codec_i2c_cfg_t out_i2c = {
        .port = AUDIO_CODEC_I2C_PORT,
        .addr = ES8311_I2C_ADDR,
        .bus_handle = i2c_bus,
    };
    out_ctrl_if = audio_codec_new_i2c_ctrl(&out_i2c);
    es8311_codec_cfg_t es8311_cfg = {};
    es8311_cfg.ctrl_if = out_ctrl_if;
    es8311_cfg.gpio_if = gpio_if;
    es8311_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
    es8311_cfg.pa_pin = GPIO_NUM_NC;  /* 功放由 PCA9557 控制 */
    es8311_cfg.use_mclk = true;
    es8311_cfg.hw_gain.pa_voltage = 5.0;
    es8311_cfg.hw_gain.codec_dac_voltage = 3.3;
    out_codec_if = es8311_codec_new(&es8311_cfg);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = out_codec_if,
        .data_if = data_if,
    };
    out_dev = esp_codec_dev_new(&dev_cfg);

    /* ---- ES7210 输入 ---- */
    audio_codec_i2c_cfg_t in_i2c = {
        .port = AUDIO_CODEC_I2C_PORT,
        .addr = ES7210_I2C_ADDR,
        .bus_handle = i2c_bus,
    };
    in_ctrl_if = audio_codec_new_i2c_ctrl(&in_i2c);
    es7210_codec_cfg_t es7210_cfg = {};
    es7210_cfg.ctrl_if = in_ctrl_if;
    es7210_cfg.mic_selected = ES7210_SEL_MIC1;  /* 只用 MIC1 录音 */
    in_codec_if = es7210_codec_new(&es7210_cfg);

    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    dev_cfg.codec_if = in_codec_if;
    dev_cfg.data_if = data_if;
    in_dev = esp_codec_dev_new(&dev_cfg);

    ESP_LOGI(TAG, "ES8311/ES7210 initialized");
    return ESP_OK;
}

/* ---------- 录音接口（16bit 单声道） ---------- */
esp_err_t audio_record_start(void) {
    if (input_open) return ESP_OK;
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 4,               /* ES7210 TDM 4 slot */
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
        .sample_rate = AUDIO_SAMPLE_RATE,
        .mclk_multiple = 0,
    };
    ESP_ERROR_CHECK(esp_codec_dev_open(in_dev, &fs));
    ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(in_dev, 28.0f));
    input_open = true;
    return ESP_OK;
}

int audio_record_read(int16_t* buf, int samples) {
    // TDM 4 通道交错数据，取 slot0（MIC1）解交错成单声道
    static int16_t tdm_buf[512 * 4];
    int got = 0;
    while (got < samples) {
        int chunk = samples - got;
        if (chunk > 512) chunk = 512;
        esp_err_t ret = esp_codec_dev_read(in_dev, tdm_buf, chunk * 4 * sizeof(int16_t));
        if (ret != ESP_OK) break;
        for (int i = 0; i < chunk; i++) {
            buf[got + i] = tdm_buf[i * 4];
        }
        got += chunk;
    }
    return got;
}

esp_err_t audio_record_stop(void) {
    if (!input_open) return ESP_OK;
    esp_codec_dev_close(in_dev);
    input_open = false;
    return ESP_OK;
}

/* ---------- 播放接口 ---------- */
esp_err_t audio_play_start(int sample_rate) {
    (void)sample_rate;  // 固定 AUDIO_SAMPLE_RATE
    if (output_open) return ESP_OK;
    audio_set_pa(true);
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = AUDIO_SAMPLE_RATE,
        .mclk_multiple = 0,
    };
    ESP_ERROR_CHECK(esp_codec_dev_open(out_dev, &fs));
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(out_dev, cur_volume));
    output_open = true;
    return ESP_OK;
}

int audio_play_write(const int16_t* buf, int samples) {
    esp_err_t ret = esp_codec_dev_write(out_dev, (void*)buf, samples * sizeof(int16_t));
    return (ret == ESP_OK) ? samples : 0;
}

esp_err_t audio_play_stop(void) {
    if (!output_open) return ESP_OK;
    esp_codec_dev_close(out_dev);
    output_open = false;
    audio_set_pa(false);
    return ESP_OK;
}

void audio_set_volume(int volume) {
    cur_volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    if (output_open) esp_codec_dev_set_out_vol(out_dev, cur_volume);
}
