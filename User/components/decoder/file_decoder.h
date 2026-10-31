#ifndef FILE_DECODER_H
#define FILE_DECODER_H
#include "music.h"
/* 单实例，由音频任务持有；输出交错双声道，24/32 位为左对齐 int32_t。 */
bool file_decoder_open(music_info_t *info);
bool file_decoder_read(void *stereo, uint32_t frames, uint32_t *decoded);
void file_decoder_close(void);
#endif
