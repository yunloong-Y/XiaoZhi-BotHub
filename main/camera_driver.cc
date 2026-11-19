/*
 * 摄像头驱动 — GC0308 DVP，直接输出 JPEG
 * JPEG 数据来自 esp32-camera 内置 JPEG 编码器，可直接存为 .jpg
 */
#include "bothub.h"
#include "board_config.h"
#include "board_io.h"

#include <esp_log.h>
#include <esp_camera.h>
#include <img_converters.h>   // frame2jpg：GC0308 无硬件 JPEG，需软件编码
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdlib>

static const char* TAG = "camera";

esp_err_t camera_init(void) {
    // 摄像头电源挂在 PCA9557 Bit2，低电平开启（原厂 SetOutputState(2, 0)）
    ESP_ERROR_CHECK(board_io_init());
    ESP_ERROR_CHECK(board_io_set_output(PCA9557_CAM_PWR_BIT, 0));
    vTaskDelay(pdMS_TO_TICKS(20));

    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_2;
    config.ledc_timer = LEDC_TIMER_2;
    config.pin_d0 = CAMERA_PIN_D0;
    config.pin_d1 = CAMERA_PIN_D1;
    config.pin_d2 = CAMERA_PIN_D2;
    config.pin_d3 = CAMERA_PIN_D3;
    config.pin_d4 = CAMERA_PIN_D4;
    config.pin_d5 = CAMERA_PIN_D5;
    config.pin_d6 = CAMERA_PIN_D6;
    config.pin_d7 = CAMERA_PIN_D7;
    config.pin_xclk = CAMERA_PIN_XCLK;
    config.pin_pclk = CAMERA_PIN_PCLK;
    config.pin_vsync = CAMERA_PIN_VSYNC;
    config.pin_href = CAMERA_PIN_HREF;
    // 复用 board_io 已建立的 I2C 总线（port 1），不要另建总线，否则 bus acquire 冲突
    config.pin_sccb_sda = -1;
    config.pin_sccb_scl = CAMERA_PIN_SIOC;
    config.sccb_i2c_port = 1;   /* I2C_NUM_1，与 board_io / 音频共用同一总线 */
    config.pin_pwdn = CAMERA_PIN_PWDN;
    config.pin_reset = CAMERA_PIN_RESET;
    config.xclk_freq_hz = CAMERA_XCLK_FREQ_HZ;
    config.pixel_format = PIXFORMAT_RGB565;  /* GC0308 无硬件 JPEG，采集后软件编码 */
    config.frame_size = FRAMESIZE_QVGA;   /* 320x240 */
    config.jpeg_quality = 90;             /* 传感器硬件 JPEG 用（本板 RGB565 输出，实际由 frame2jpg 控制） */
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed: 0x%x", err);
        return err;
    }

    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_vflip(s, 0);
        s->set_hmirror(s, 0);
    }
    ESP_LOGI(TAG, "GC0308 RGB565 camera initialized (QVGA, software JPEG)");
    return ESP_OK;
}

esp_err_t camera_take_jpeg(uint8_t** buf, size_t* len) {
    *buf = nullptr;
    *len = 0;
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "fb_get failed");
        return ESP_FAIL;
    }

    uint8_t* jpeg = nullptr;
    size_t jpeg_len = 0;
    esp_err_t ret = ESP_OK;

    if (fb->format == PIXFORMAT_JPEG) {
        /* 万一传感器支持硬件 JPEG，直接拷贝 */
        jpeg_len = fb->len;
        jpeg = (uint8_t*)malloc(jpeg_len);
        if (jpeg) memcpy(jpeg, fb->buf, jpeg_len);
    } else {
        /* GC0308: RGB565 -> 软件编码 JPEG。jpge 编码器 quality 1-100，越大越清晰。
         * 之前误用 12（受传感器 JPEG 语义误导）导致照片糊成马赛克，改为 90。 */
        bool ok = frame2jpg(fb, 90, &jpeg, &jpeg_len);
        if (!ok) {
            ESP_LOGE(TAG, "frame2jpg failed");
            ret = ESP_FAIL;
        }
    }
    esp_camera_fb_return(fb);

    if (ret != ESP_OK || !jpeg) {
        if (jpeg) free(jpeg);
        return ret != ESP_OK ? ret : ESP_ERR_NO_MEM;
    }
    *buf = jpeg;
    *len = jpeg_len;
    ESP_LOGI(TAG, "captured JPEG %u bytes", (unsigned)jpeg_len);
    return ESP_OK;
}
