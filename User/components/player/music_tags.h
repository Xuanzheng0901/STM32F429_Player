#ifndef MUSIC_TAGS_H
#define MUSIC_TAGS_H

#include "music.h"

/* 曲库任务调用：只读文件，非空标签覆盖对应字段，缺失字段保持文件名回退值。 */
FRESULT music_tags_read(music_info_t *info);

#endif
