#ifndef MUSIC_LIBRARY_H
#define MUSIC_LIBRARY_H
#include "music.h"

/* 调用方持有已挂载卷；建库缓存路径和标签，按窗口只读取索引。 */
/* 卸载卷前关闭只读索引；重建时也会关闭旧索引。 */
void music_library_close(void);
FRESULT music_library_rebuild(uint32_t *total);
FRESULT music_library_load(uint32_t total, uint32_t center, music_window_t *window);
#endif
