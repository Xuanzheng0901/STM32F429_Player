#ifndef SDCARD_H
#define SDCARD_H
#include "music.h"

typedef enum { SDCARD_UNCHANGED, SDCARD_INSERTED, SDCARD_REMOVED } sdcard_event_t;

/* 曲库任务每 20 ms 调用；启动和重插卡均需连续三次有卡。 */
sdcard_event_t sdcard_poll(void);
bool sdcard_is_removed(void);
/* 仅曲库任务调用；卸载前必须等音频任务关闭文件。 */
FRESULT sdcard_mount_and_scan(void);
FRESULT sdcard_load_music_window(uint32_t center, music_window_t *window);
void sdcard_unmount(void);
#endif
