/*
 * 显示驱动 — ST7789 240x320，直接用 esp_lcd 绘制，无 LVGL 依赖
 * 文本用内置 5x7 点阵放大显示，行缓冲写屏。
 */
#include "bothub.h"
#include "board_config.h"
#include "font5x7.h"

#include <esp_log.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <cstring>

static const char* TAG = "display";

static esp_lcd_panel_handle_t panel = nullptr;
static esp_lcd_panel_io_handle_t panel_io = nullptr;

/* RGB565 */
#define COLOR_BG     0x0001u  /* 近黑 */
#define COLOR_FG     0xFFFFu  /* 白 */
#define COLOR_TITLE  0x07E0u  /* 绿 */
#define COLOR_HL     0xF800u  /* 红 */

static void fill_rect(int x, int y, int w, int h, uint16_t color) {
    static uint16_t line[DISPLAY_WIDTH];
    for (int i = 0; i < w; i++) line[i] = color;
    for (int j = 0; j < h; j++) {
        esp_lcd_panel_draw_bitmap(panel, x, y + j, x + w, y + j + 1, line);
    }
}

static void draw_char(int x, int y, char c, int scale, uint16_t color) {
    int idx = (int)(unsigned char)c - 0x20;
    if (idx < 0 || idx >= 95) return;
    const uint8_t* glyph = font5x7_bits[idx];
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if (glyph[row] & (1 << col)) {
                fill_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static void draw_text(int x, int y, const char* text, int scale, uint16_t color) {
    int cx = x;
    while (*text) {
        if (*text == '\n') { break; }
        draw_char(cx, y, *text, scale, color);
        cx += 6 * scale;
        text++;
    }
}

esp_err_t display_init(void) {
    spi_bus_config_t buscfg = {
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = LCD_PIN_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_PIN_CS,
        .dc_gpio_num = LCD_PIN_DC,
        .spi_mode = 2,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 8,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_cfg, &panel_io));

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = LCD_PIN_RST;
    panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_cfg.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_cfg, &panel));

    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_invert_color(panel, true);
    esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
    esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    esp_lcd_panel_disp_on_off(panel, true);

    // 背光
    gpio_config_t bl = {
        .pin_bit_mask = (1ULL << LCD_PIN_BACKLIGHT),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bl);
    gpio_set_level(LCD_PIN_BACKLIGHT, 1);

    fill_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, COLOR_BG);
    ESP_LOGI(TAG, "ST7789 display initialized");
    return ESP_OK;
}

void display_update(const char* title, const char* const* lines, int line_count) {
    if (!panel) return;
    fill_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, COLOR_BG);
    draw_text(8, 8, title, 3, COLOR_TITLE);
    fill_rect(0, 40, DISPLAY_WIDTH, 2, COLOR_TITLE);

    int y = 56;
    const int max_lines = 8;
    for (int i = 0; i < line_count && i < max_lines; i++) {
        draw_text(8, y, lines[i], 2, (i == 0) ? COLOR_HL : COLOR_FG);
        y += 22;
    }
}
