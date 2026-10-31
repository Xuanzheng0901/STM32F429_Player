#ifndef MUSIC_LIBRARY_H
#define MUSIC_LIBRARY_H
#include "music.h"

/* 调用方持有已挂载卷；只保存路径索引，按窗口读取标签。 */
FRESULT music_library_rebuild(uint32_t *total);
FRESULT music_library_load(uint32_t total, uint32_t center, music_window_t *window);
#endif
