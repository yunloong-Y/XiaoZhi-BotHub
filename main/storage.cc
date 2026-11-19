/*
 * 存储模块 — SPIFFS 挂载与文件读写
 * 照片（.jpg）和录音（.wav）保存在 "storage" 分区
 */
#include "bothub.h"
#include "board_config.h"

#include <esp_log.h>
#include <esp_spiffs.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstdio>
#include <cstring>

static const char* TAG = "storage";

esp_err_t storage_init(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/storage",
        .partition_label = STORAGE_PARTITION_NAME,
        .max_files = 8,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS register failed: %s", esp_err_to_name(ret));
        return ret;
    }
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(STORAGE_PARTITION_NAME, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS mounted: total=%u KB used=%u KB", total / 1024, used / 1024);
    }
    return ret;
}

esp_err_t storage_save(const char* path, const uint8_t* data, size_t len) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "open for write failed: %s", path);
        return ESP_FAIL;
    }
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    if (written != len) {
        ESP_LOGE(TAG, "short write: %u/%u", written, len);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "saved %s (%u bytes)", path, len);
    return ESP_OK;
}

int storage_read(const char* path, uint8_t** out) {
    *out = nullptr;
    FILE* f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "open for read failed: %s", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return -1;
    }
    uint8_t* buf = (uint8_t*)malloc(size);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t got = fread(buf, 1, size, f);
    fclose(f);
    if (got != (size_t)size) {
        free(buf);
        return -1;
    }
    *out = buf;
    return (int)size;
}

bool storage_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

int storage_list(const char* dir, const char* ext, char out_names[][64], int max_count) {
    DIR* d = opendir(dir);
    if (!d) return 0;
    int count = 0;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr && count < max_count) {
        const char* name = ent->d_name;
        size_t nlen = strlen(name);
        size_t elen = strlen(ext);
        if (nlen > elen && strcmp(name + nlen - elen, ext) == 0) {
            // name 最长 255，显式用精度限制避免 -Wformat-truncation 误报溢出
            snprintf(out_names[count], 64, "%s/%.*s", dir, 30, name);
            count++;
        }
    }
    closedir(d);
    // 按名字排序，保证回放顺序稳定
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (strcmp(out_names[i], out_names[j]) > 0) {
                char tmp[64];
                strncpy(tmp, out_names[i], 64);
                strncpy(out_names[i], out_names[j], 64);
                strncpy(out_names[j], tmp, 64);
            }
        }
    }
    return count;
}

size_t storage_used_bytes(void) {
    size_t total = 0, used = 0;
    if (esp_spiffs_info(STORAGE_PARTITION_NAME, &total, &used) == ESP_OK) return used;
    return 0;
}

size_t storage_total_bytes(void) {
    size_t total = 0, used = 0;
    if (esp_spiffs_info(STORAGE_PARTITION_NAME, &total, &used) == ESP_OK) return total;
    return 0;
}
