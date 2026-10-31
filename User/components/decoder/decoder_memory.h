#ifndef DECODER_MEMORY_H
#define DECODER_MEMORY_H

#include <stddef.h>

/* MP3/FLAC 互斥播放，共用静态工作区；仅音频任务在旧解码器关闭后重置。 */
void decoder_memory_reset(void);
void *decoder_memory_alloc(size_t size);

#endif
