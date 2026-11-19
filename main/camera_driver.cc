/*
 * 摄像头驱动 — GC0308 DVP，直接输出 JPEG
 * JPEG 数据来自 esp32-camera 内置 JPEG 编码器，可直接存为 .jpg
 */
#include "bothub.h"
#include "board_config.h"

#include <esp_log.h>
#include <esp_camera.h>
#include <cstdlib>

static const char* TAG = "camera";

esp_err_t camera_init(void) {
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
    config.pin_sccb_sda = CAMERA_PIN_SIOD;
    config.pin_sccb_scl = CAMERA_PIN_SIOC;
    config.pin_pwdn = CAMERA_PIN_PWDN;
    config.pin_reset = CAMERA_PIN_RESET;
    config.xclk_freq_hz = CAMERA_XCLK_FREQ_HZ;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QVGA;   /* 320x240 */
    config.jpeg_quality = 12;             /* 数值越小质量越高 */
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
    ESP_LOGI(TAG, "GC0308 JPEG camera initialized (QVGA)");
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
    if (fb->format != PIXFORMAT_JPEG) {
        ESP_LOGE(TAG, "unexpected format %d", fb->format);
        esp_camera_fb_return(fb);
        return ESP_FAIL;
    }
    uint8_t* copy = (uint8_t*)malloc(fb->len);
    if (!copy) {
        esp_camera_fb_return(fb);
        return ESP_ERR_NO_MEM;
    }
    memcpy(copy, fb->buf, fb->len);
    *len = fb->len;
    *buf = copy;
    esp_camera_fb_return(fb);
    ESP_LOGI(TAG, "captured JPEG %u bytes", *len);
    return ESP_OK;
}
