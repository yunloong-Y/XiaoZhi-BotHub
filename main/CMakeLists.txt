idf_component_register(
    SRCS
        "app_main.cc"
        "board_io.cc"
        "audio_driver.cc"
        "camera_driver.cc"
        "storage.cc"
        "wav_codec.cc"
        "display_driver.cc"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_spi
        esp_driver_ledc
        esp_timer
        esp_lcd
        spiffs
        vfs
        nvs_flash
        esp_partition
        esp32-camera
        esp_codec_dev
)

# IDF 6.1 默认开启 -Werror=missing-field-initializers；本项目大量使用
# 聚合结构体部分初始化（ESP-IDF 常见写法），降级为警告以免误判。
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=missing-field-initializers
    -Wno-error=format-truncation
)
