#ifndef MUSIC_H
#define MUSIC_H

#include <stdint.h>
#include <stdbool.h>
#include "ff.h"

#define MUSIC_PATH_CAPACITY 1024U

/* 模块之间只共享歌曲数据；路径为 FatFs UTF-8，显示字符串为 UTF-8。 */
typedef enum {
    MUSIC_WAV, MUSIC_MP3, MUSIC_FLAC, MUSIC_UNKNOWN = 0xFF
} music_type_t;

typedef struct {
    char title[64];
    char artist[32];
    char album[32];
    TCHAR path[MUSIC_PATH_CAPACITY];
    music_type_t type;
    uint32_t sample_rate_hz;
    uint32_t duration_seconds;
    uint8_t bits_per_sample;
} music_info_t;

#define MUSIC_WINDOW_COUNT 15U

typedef struct {
    music_info_t items[MUSIC_WINDOW_COUNT];
    uint32_t first_index;
    uint32_t total;
    uint16_t count;
} music_window_t;

#endif
