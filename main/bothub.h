/*
 * 小智AI-BotHub — 公共定义
 * 拍照 / 录音 / 回放 小工具的模块间接口与全局状态
 */
#pragma once

#include <esp_err.h>
#include <stddef.h>
#include <stdint.h>

/* ---------------- 工作模式 ---------------- */
typedef enum {
    MODE_IDLE = 0,      /* 待机 */
    MODE_PHOTO,         /* 拍照中 */
    MODE_RECORDING,     /* 录音中 */
    MODE_PLAYBACK,      /* 回放中 */
} WorkMode;

/* ---------------- 全局状态（app_main 持有） ---------------- */
struct AppState {
    WorkMode mode = MODE_IDLE;
    int photo_count = 0;      /* 已拍照片数 */
    int record_count = 0;     /* 已录音数 */
    int playback_index = 0;   /* 当前回放文件序号 */
    char last_message[96] = "";
};

AppState& app_state();

/* ---------------- 存储模块 storage.cc ---------------- */
esp_err_t storage_init(void);
esp_err_t storage_save(const char* path, const uint8_t* data, size_t len);
/** 读取整个文件到 malloc 缓冲区，返回长度（<0 表示失败）；调用者须 free(*out) */
int storage_read(const char* path, uint8_t** out);
/** 列出目录中匹配前缀与扩展名的文件，写入 out_names（每个为静态缓冲区内的字符串指针） */
int storage_list(const char* dir, const char* ext, char out_names[][64], int max_count);
bool storage_exists(const char* path);
size_t storage_used_bytes(void);
size_t storage_total_bytes(void);

/* ---------------- 摄像头模块 camera_driver.cc ---------------- */
esp_err_t camera_init(void);
/** 拍一张 JPEG，返回 malloc 后的缓冲区与长度；调用者须 free(*buf) */
esp_err_t camera_take_jpeg(uint8_t** buf, size_t* len);

/* ---------------- 音频模块 audio_driver.cc ---------------- */
esp_err_t audio_init(void);
esp_err_t audio_set_pa(bool enable);
/** 开始录音（打开输入通道） */
esp_err_t audio_record_start(void);
/** 读取一批 16bit 单声道采样；返回读取的采样数 */
int audio_record_read(int16_t* buf, int samples);
esp_err_t audio_record_stop(void);
/** 开始播放（打开输出通道） */
esp_err_t audio_play_start(int sample_rate);
/** 写入一批 16bit 采样 */
int audio_play_write(const int16_t* buf, int samples);
esp_err_t audio_play_stop(void);
void audio_set_volume(int volume); /* 0-100 */

/* ---------------- WAV 封装 wav_codec.cc ---------------- */
#pragma pack(push, 1)
struct WavHeader {
    char riff[4];        /* "RIFF" */
    uint32_t riff_size;  /* 文件总长 - 8 */
    char wave[4];        /* "WAVE" */
    char fmt[4];         /* "fmt " */
    uint32_t fmt_size;   /* 16 */
    uint16_t audio_fmt;  /* 1 = PCM */
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data[4];        /* "data" */
    uint32_t data_size;
};
#pragma pack(pop)

void wav_make_header(WavHeader* h, uint32_t sample_rate, uint16_t channels,
                     uint16_t bits, uint32_t data_size);
bool wav_parse_header(const uint8_t* data, size_t len, WavHeader* out,
                      size_t* data_offset, size_t* data_size);

/* ---------------- 显示模块 display_driver.cc ---------------- */
esp_err_t display_init(void);
/** 刷新屏幕：标题 + 若干行状态文本（最多 8 行） */
void display_update(const char* title, const char* const* lines, int line_count);
