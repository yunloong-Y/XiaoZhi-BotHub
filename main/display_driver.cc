/*
 * 显示驱动 — ST7789 320x240，直接用 esp_lcd 绘制，无 LVGL 依赖
 *
 * 关键：先在 PSRAM 里画好整帧 framebuffer，再一次 draw_bitmap 刷屏。
 * 逐行/逐点调用 draw_bitmap 会产生数千次 SPI 事务，导致严重帧撕裂/花屏闪烁。
 */
#include "bothub.h"
#include "board_config.h"
#include "board_io.h"
#include "cn_font.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <cstring>

static const char* TAG = "display";

static esp_lcd_panel_handle_t panel = nullptr;
static esp_lcd_panel_io_handle_t panel_io = nullptr;
static uint16_t* s_fb = nullptr;   /* 整帧 RGB565 framebuffer（PSRAM） */

/*
 * 面板已设置 invert_color(true)，发送到屏幕的颜色会被硬件取反。
 * 因此这里写入的色值 = 期望显示色的按位取反，最终显示才是预期颜色。
 * 期望：背景近黑、正文白、标题绿、首行红。
 */
#define INV(c)          ((uint16_t)(~(c)))
#define COLOR_BG        INV(0x0001u)   /* -> 近黑 */
#define COLOR_FG        INV(0xFFFFu)   /* -> 白 */
#define COLOR_TITLE     INV(0x07E0u)   /* -> 绿 */
#define COLOR_HL        INV(0xF800u)   /* -> 红 */

static void fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (!s_fb) return;
    /* 裁剪到屏幕范围，防止越界写 PSRAM */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= DISPLAY_WIDTH  || y >= DISPLAY_HEIGHT) return;
    if (x + w > DISPLAY_WIDTH)  w = DISPLAY_WIDTH  - x;
    if (y + h > DISPLAY_HEIGHT) h = DISPLAY_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    for (int j = 0; j < h; j++) {
        uint16_t* dst = s_fb + (size_t)(y + j) * DISPLAY_WIDTH + x;
        for (int i = 0; i < w; i++) dst[i] = color;   /* 纯内存写，无 SPI */
    }
}

/* 查字库（线性查找，151 个字足够快） */
static const cn_glyph_t* find_glyph(uint16_t unicode) {
    for (int i = 0; i < CN_FONT_COUNT; i++) {
        if (cn_font[i].unicode == unicode) return &cn_font[i];
    }
    return nullptr;
}

/* 解码一个 UTF-8 字符，返回字节数；unicode 为 BMP 码点 */
static int utf8_decode(const char* s, uint16_t* uni) {
    const uint8_t* p = (const uint8_t*)s;
    uint8_t c = p[0];
    if (c < 0x80) { *uni = c; return 1; }
    if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        *uni = ((c & 0x1F) << 6) | (p[1] & 0x3F); return 2;
    }
    if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *uni = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); return 3;
    }
    *uni = '?';
    return 1;
}

/* 画单个 16x16 字形（scale 倍放大） */
static void draw_glyph(int x, int y, const cn_glyph_t* g, int scale, uint16_t color) {
    for (int row = 0; row < CN_FONT_SIZE; row++) {
        uint16_t bits = g->rows[row];
        for (int col = 0; col < CN_FONT_SIZE; col++) {
            if (bits & (1 << (CN_FONT_SIZE - 1 - col))) {
                fill_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

/* UTF-8 文本绘制，等宽 16*scale 步进；未收录字符画方框占位 */
static void draw_text(int x, int y, const char* text, int scale, uint16_t color) {
    int cx = x;
    int step = CN_FONT_SIZE * scale;
    while (*text) {
        if (*text == '\n') break;
        uint16_t uni = 0;
        int n = utf8_decode(text, &uni);
        if (cx + step <= DISPLAY_WIDTH) {
            const cn_glyph_t* g = find_glyph(uni);
            if (g) {
                draw_glyph(cx, y, g, scale, color);
            } else {
                fill_rect(cx, y, step, step, color);  /* 缺字占位 */
            }
        }
        cx += step;
        text += n;
    }
}

/* 背光：LEDC + output_invert（低电平点亮 PWM），25kHz 防电感啸叫 */
static void backlight_init(void) {
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = LCD_BACKLIGHT_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t ch = {
        .gpio_num = LCD_PIN_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
        .flags = { .output_invert = LCD_BACKLIGHT_INVERT },
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));

    uint32_t duty = (1023 * LCD_BACKLIGHT_DUTY_PCT) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}

esp_err_t display_init(void) {
    /* 屏幕复位脚挂在 PCA9557 Bit0，需先初始化 IO 扩展 */
    ESP_ERROR_CHECK(board_io_init());

    /* 整帧 framebuffer：优先 PSRAM，回退内部 RAM */
    size_t fb_size = (size_t)DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
    bool fb_in_psram = true;
    s_fb = (uint16_t*)heap_caps_malloc(fb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_fb) {
        fb_in_psram = false;
        s_fb = (uint16_t*)heap_caps_malloc(fb_size, MALLOC_CAP_8BIT);
    }
    if (!s_fb) {
        ESP_LOGE(TAG, "framebuffer alloc failed (%u bytes)", (unsigned)fb_size);
        return ESP_ERR_NO_MEM;
    }
    memset(s_fb, 0, fb_size);

    spi_bus_config_t buscfg = {
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = LCD_PIN_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = (int)fb_size,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_PIN_CS,
        .dc_gpio_num = LCD_PIN_DC,
        .spi_mode = 2,
        .pclk_hz = 80 * 1000 * 1000,   /* 与原厂一致 80MHz */
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_cfg, &panel_io));

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = LCD_PIN_RST;   /* NC：复位由 PCA9557 Bit0 控制 */
    panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_cfg.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_cfg, &panel));

    esp_lcd_panel_reset(panel);
    /* 放开 PCA9557 上的 LCD 复位（低电平=正常工作），原厂 SetOutputState(0, 0) */
    ESP_ERROR_CHECK(board_io_set_output(PCA9557_LCD_RST_BIT, 0));

    esp_lcd_panel_init(panel);
    esp_lcd_panel_invert_color(panel, true);
    esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
    esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    esp_lcd_panel_disp_on_off(panel, true);

    backlight_init();

    fill_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, COLOR_BG);
    esp_lcd_panel_draw_bitmap(panel, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, s_fb);
    ESP_LOGI(TAG, "ST7789 display initialized (framebuffer %u bytes in %s, backlight %d%%)",
             (unsigned)fb_size, fb_in_psram ? "PSRAM" : "RAM", LCD_BACKLIGHT_DUTY_PCT);
    return ESP_OK;
}

void display_update(const char* title, const char* const* lines, int line_count) {
    if (!panel || !s_fb) return;

    /* 1) 全部在内存里绘制（16x16 点阵，标题与正文均为 scale=1） */
    fill_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, COLOR_BG);
    draw_text(8, 6, title, 1, COLOR_TITLE);
    fill_rect(0, 26, DISPLAY_WIDTH, 2, COLOR_TITLE);

    int y = 36;
    const int max_lines = 8;
    for (int i = 0; i < line_count && i < max_lines; i++) {
        draw_text(8, y, lines[i], 1, (i == 0) ? COLOR_HL : COLOR_FG);
        y += 26;
    }

    /* 2) 一次 SPI 事务刷整帧，杜绝撕裂/闪烁 */
    esp_lcd_panel_draw_bitmap(panel, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, s_fb);
}
