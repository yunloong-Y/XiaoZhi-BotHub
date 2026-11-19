/*
 * WAV 封装/解析 — 16bit PCM
 */
#include "bothub.h"
#include <cstring>

static void put32(uint8_t* p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}
static void put16(uint8_t* p, uint16_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
}
static uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void wav_make_header(WavHeader* h, uint32_t sample_rate, uint16_t channels,
                     uint16_t bits, uint32_t data_size) {
    uint16_t block_align = channels * bits / 8;
    uint32_t byte_rate = sample_rate * block_align;
    memcpy(h->riff, "RIFF", 4);
    put32((uint8_t*)&h->riff_size, 36 + data_size);
    memcpy(h->wave, "WAVE", 4);
    memcpy(h->fmt, "fmt ", 4);
    put32((uint8_t*)&h->fmt_size, 16);
    put16((uint8_t*)&h->audio_fmt, 1);
    put16((uint8_t*)&h->channels, channels);
    put32((uint8_t*)&h->sample_rate, sample_rate);
    put32((uint8_t*)&h->byte_rate, byte_rate);
    put16((uint8_t*)&h->block_align, block_align);
    put16((uint8_t*)&h->bits_per_sample, bits);
    memcpy(h->data, "data", 4);
    put32((uint8_t*)&h->data_size, data_size);
}

bool wav_parse_header(const uint8_t* data, size_t len, WavHeader* out,
                      size_t* data_offset, size_t* data_size) {
    if (len < 44) return false;
    if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) return false;
    memcpy(out, data, sizeof(WavHeader));
    *data_offset = 44;
    // 简单起见只支持标准 44 字节头（本工具录制的 WAV 即此格式）
    *data_size = get32(data + 40);
    if (*data_offset + *data_size > len) {
        *data_size = len - *data_offset;
    }
    return out->bits_per_sample == 16 && out->channels >= 1;
}
