/*
 * 小智AI-BotHub — 主程序
 *
 * 交互（BOOT 按键 GPIO0）：
 *   单击       -> 拍照（JPEG 存 /storage/photo_XXX.jpg）
 *   长按(>1.2s)-> 开始/停止录音（WAV 存 /storage/rec_XXX.wav）
 *   双击       -> 回放下一段录音
 *
 * 也可通过 USB 串口输入命令：photo / rec / play / ls / status / vol
 */
#include "bothub.h"
#include "board_config.h"

#include <esp_log.h>
#include <esp_err.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static const char* TAG = "bothub";

/* ---------- 全局状态 ---------- */
static AppState g_state;
AppState& app_state() { return g_state; }

static SemaphoreHandle_t g_state_mutex = nullptr;
static volatile bool g_record_stop = false;
static volatile bool g_busy = false;

#define LOCK_STATE()   xSemaphoreTake(g_state_mutex, portMAX_DELAY)
#define UNLOCK_STATE() xSemaphoreGive(g_state_mutex)

static void set_message(const char* msg) {
    LOCK_STATE();
    strncpy(g_state.last_message, msg, sizeof(g_state.last_message) - 1);
    UNLOCK_STATE();
}

static void set_mode(WorkMode m) {
    LOCK_STATE();
    g_state.mode = m;
    UNLOCK_STATE();
}

/* ---------- 拍照任务 ---------- */
static void photo_task(void*) {
    if (g_busy) { set_message("BUSY"); vTaskDelete(nullptr); return; }
    g_busy = true;
    set_mode(MODE_PHOTO);
    set_message("TAKING PHOTO");

    uint8_t* jpg = nullptr;
    size_t len = 0;
    esp_err_t ret = camera_take_jpeg(&jpg, &len);
    if (ret == ESP_OK && jpg) {
        char path[64];
        int idx;
        LOCK_STATE(); idx = g_state.photo_count; UNLOCK_STATE();
        snprintf(path, sizeof(path), "/storage/photo_%03d.jpg", idx);
        if (storage_save(path, jpg, len) == ESP_OK) {
            LOCK_STATE(); g_state.photo_count++; UNLOCK_STATE();
            char msg[96]; snprintf(msg, sizeof(msg), "SAVED photo_%03d.jpg", idx);
            set_message(msg);
        } else {
            set_message("SAVE FAIL");
        }
        free(jpg);
    } else {
        set_message("CAPTURE FAIL");
    }

    g_busy = false;
    set_mode(MODE_IDLE);
    vTaskDelete(nullptr);
}

/* ---------- 录音任务 ---------- */
static void record_task(void*) {
    if (g_busy) { set_message("BUSY"); vTaskDelete(nullptr); return; }
    g_busy = true;
    g_record_stop = false;
    set_mode(MODE_RECORDING);
    set_message("RECORDING...");

    int idx;
    LOCK_STATE(); idx = g_state.record_count; UNLOCK_STATE();
    char path[64];
    snprintf(path, sizeof(path), "/storage/rec_%03d.wav", idx);

    FILE* f = fopen(path, "wb");
    if (!f) {
        set_message("OPEN FAIL");
        g_busy = false; set_mode(MODE_IDLE); vTaskDelete(nullptr); return;
    }
    // 先写占位 WAV 头，结束时回填
    WavHeader hdr;
    wav_make_header(&hdr, AUDIO_SAMPLE_RATE, 1, 16, 0);
    fwrite(&hdr, 1, sizeof(hdr), f);

    if (audio_record_start() != ESP_OK) {
        fclose(f);
        remove(path);
        set_message("MIC FAIL");
        g_busy = false; set_mode(MODE_IDLE); vTaskDelete(nullptr); return;
    }

    int16_t buf[512];
    size_t total_bytes = 0;
    int tick = 0;
    while (!g_record_stop) {
        int n = audio_record_read(buf, 512);
        if (n <= 0) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
        fwrite(buf, sizeof(int16_t), n, f);
        total_bytes += n * sizeof(int16_t);
        // 每 5 秒刷新一次显示，提示录音时长
        if (++tick % (AUDIO_SAMPLE_RATE / 512 * 5) == 0) {
            char msg[96];
            snprintf(msg, sizeof(msg), "REC %u s", (unsigned)(total_bytes / (AUDIO_SAMPLE_RATE * 2)));
            set_message(msg);
        }
    }

    audio_record_stop();
    // 回填头
    wav_make_header(&hdr, AUDIO_SAMPLE_RATE, 1, 16, total_bytes);
    fseek(f, 0, SEEK_SET);
    fwrite(&hdr, 1, sizeof(hdr), f);
    fclose(f);

    if (total_bytes > 44) {
        LOCK_STATE(); g_state.record_count++; UNLOCK_STATE();
        char msg[96];
        snprintf(msg, sizeof(msg), "SAVED rec_%03d.wav", idx);
        set_message(msg);
    } else {
        remove(path);
        set_message("EMPTY REC");
    }
    g_busy = false;
    set_mode(MODE_IDLE);
    vTaskDelete(nullptr);
}

/* ---------- 回放任务 ---------- */
static void playback_task(void*) {
    if (g_busy) { set_message("BUSY"); vTaskDelete(nullptr); return; }
    g_busy = true;
    set_mode(MODE_PLAYBACK);

    char files[32][64];
    int count = storage_list("/storage", ".wav", files, 32);
    if (count == 0) {
        set_message("NO RECORDING");
        g_busy = false; set_mode(MODE_IDLE); vTaskDelete(nullptr); return;
    }

    int idx;
    LOCK_STATE(); idx = g_state.playback_index % count; UNLOCK_STATE();
    const char* path = files[idx];

    uint8_t* wav = nullptr;
    int wav_len = storage_read(path, &wav);
    if (wav_len <= 0) {
        set_message("READ FAIL");
        g_busy = false; set_mode(MODE_IDLE); vTaskDelete(nullptr); return;
    }

    WavHeader hdr;
    size_t data_off = 0, data_size = 0;
    bool ok = wav_parse_header(wav, wav_len, &hdr, &data_off, &data_size);
    if (!ok) {
        set_message("BAD WAV");
        free(wav);
        g_busy = false; set_mode(MODE_IDLE); vTaskDelete(nullptr); return;
    }

    char base[32];
    const char* slash = strrchr(path, '/');
    strncpy(base, slash ? slash + 1 : path, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    char msg[96];
    snprintf(msg, sizeof(msg), "PLAY %s", base);
    set_message(msg);

    audio_play_start(hdr.sample_rate);
    const int16_t* pcm = (const int16_t*)(wav + data_off);
    int total_samples = data_size / sizeof(int16_t);
    int done = 0;
    while (done < total_samples) {
        int chunk = total_samples - done;
        if (chunk > 512) chunk = 512;
        int written = audio_play_write(pcm + done, chunk);
        if (written <= 0) break;
        done += written;
    }
    audio_play_stop();
    free(wav);

    LOCK_STATE(); g_state.playback_index = (idx + 1) % count; UNLOCK_STATE();
    set_message("PLAY DONE");
    g_busy = false;
    set_mode(MODE_IDLE);
    vTaskDelete(nullptr);
}

/* ---------- 显示刷新任务 ---------- */
static void display_task(void*) {
    while (true) {
        char line0[96], line1[96], line2[96], line3[96];
        int photos, recs;
        char msg[96];
        LOCK_STATE();
        photos = g_state.photo_count;
        recs = g_state.record_count;
        strncpy(msg, g_state.last_message, sizeof(msg) - 1); msg[sizeof(msg) - 1] = '\0';
        UNLOCK_STATE();

        snprintf(line0, sizeof(line0), "%s", msg);
        snprintf(line1, sizeof(line1), "PHOTO:%d  REC:%d", photos, recs);
        snprintf(line2, sizeof(line2), "TAP:PHOTO HOLD:REC");
        snprintf(line3, sizeof(line3), "DBL:PLAY  STORAGE:%uK",
                 (unsigned)(storage_used_bytes() / 1024));

        const char* lines[4] = {line0, line1, line2, line3};
        display_update("XIAOZHI BOTHUB", lines, 4);
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}

/* ---------- BOOT 按键（原生 GPIO + 软件消抖） ---------- */
static void button_task(void*) {
    const int LONG_PRESS_MS = 1200;
    const int DEBOUNCE_MS = 30;
    bool last = true;             // 上拉，未按下为高
    bool long_fired = false;
    uint32_t press_start = 0;
    uint32_t last_click_end = 0;

    while (true) {
        bool level = gpio_get_level(BOOT_BUTTON_GPIO);
        uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (level != last) {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            level = gpio_get_level(BOOT_BUTTON_GPIO);
            if (level != last) {
                if (!level) {
                    // 按下
                    press_start = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
                    long_fired = false;
                } else {
                    // 松开
                    uint32_t dur = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - press_start;
                    if (long_fired) {
                        // 长按已在按下期间处理
                    } else if (dur < 600) {
                        // 判断双击
                        if (now - last_click_end < 350) {
                            xTaskCreate(playback_task, "playback", 6144, nullptr, 3, nullptr);
                            last_click_end = 0;
                        } else {
                            last_click_end = now;
                            // 单击拍照延迟触发，等待可能的双击
                        }
                    }
                }
                last = level;
            }
        }
        // 长按检测
        if (!last && !long_fired && press_start > 0) {
            uint32_t held = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - press_start;
            if (held >= LONG_PRESS_MS) {
                long_fired = true;
                LOCK_STATE();
                bool recording = (g_state.mode == MODE_RECORDING);
                UNLOCK_STATE();
                if (recording) {
                    g_record_stop = true;
                    set_message("STOPPING REC");
                } else if (!g_busy) {
                    xTaskCreate(record_task, "record", 6144, nullptr, 3, nullptr);
                }
            }
        }
        // 单击确认（距上次按下松开 > 350ms，判定为单击）
        if (last_click_end > 0 && last && (now - last_click_end) > 350) {
            xTaskCreate(photo_task, "photo", 6144, nullptr, 3, nullptr);
            last_click_end = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ---------- 串口命令任务 ---------- */
static void cli_task(void*) {
    char line[64];
    int idx = 0;
    ESP_LOGI(TAG, "CLI ready: photo | rec | play | ls | status | vol<pct>");
    while (true) {
        int c = getchar();
        if (c < 0) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        if (c == '\n' || c == '\r') {
            line[idx] = '\0';
            if (idx > 0) {
                if (strcmp(line, "photo") == 0) {
                    xTaskCreate(photo_task, "photo", 6144, nullptr, 3, nullptr);
                } else if (strcmp(line, "rec") == 0) {
                    if (g_busy && app_state().mode == MODE_RECORDING) g_record_stop = true;
                    else xTaskCreate(record_task, "record", 6144, nullptr, 3, nullptr);
                } else if (strcmp(line, "play") == 0) {
                    xTaskCreate(playback_task, "playback", 6144, nullptr, 3, nullptr);
                } else if (strcmp(line, "ls") == 0) {
                    char files[32][64];
                    int n = storage_list("/storage", ".wav", files, 32);
                    int n2 = storage_list("/storage", ".jpg", files + n, 32 - n);
                    ESP_LOGI(TAG, "files: %d wav, %d jpg", n, n2);
                    for (int i = 0; i < n + n2; i++) ESP_LOGI(TAG, "  %s", files[i]);
                } else if (strcmp(line, "status") == 0) {
                    ESP_LOGI(TAG, "photos=%d recs=%d used=%u/%u KB",
                             app_state().photo_count, app_state().record_count,
                             (unsigned)(storage_used_bytes()/1024),
                             (unsigned)(storage_total_bytes()/1024));
                } else if (strncmp(line, "vol", 3) == 0) {
                    int v = atoi(line + 3);
                    audio_set_volume(v);
                    ESP_LOGI(TAG, "volume=%d", v);
                } else {
                    ESP_LOGI(TAG, "unknown cmd: %s", line);
                }
            }
            idx = 0;
        } else if (idx < (int)sizeof(line) - 1) {
            line[idx++] = (char)c;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

/* ---------- 入口 ---------- */
extern "C" void app_main(void) {
    // NVS 初始化（esp_codec_dev / camera 依赖）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    g_state_mutex = xSemaphoreCreateMutex();

    ESP_LOGI(TAG, "=== XiaoZhi AI-BotHub starting ===");

    ESP_ERROR_CHECK(storage_init());
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(audio_init());
    ESP_ERROR_CHECK(camera_init());

    gpio_config_t btn = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn));

    set_message("READY");
    set_mode(MODE_IDLE);

    xTaskCreate(display_task, "display", 4096, nullptr, 2, nullptr);
    xTaskCreate(button_task, "button", 4096, nullptr, 3, nullptr);
    xTaskCreate(cli_task, "cli", 4096, nullptr, 2, nullptr);

    ESP_LOGI(TAG, "Ready. BOOT: tap=photo, hold=record, double=play");
}
