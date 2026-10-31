#ifndef FLAC_DECODER_H
#define FLAC_DECODER_H

#include "music.h"

bool flac_decoder_open(FIL *file, music_info_t *info);
/* 16 位源写入 int16_t；24/32 位源写入左对齐 int32_t，均为交错立体声。 */
bool flac_decoder_read(void *stereo, uint32_t frames, uint32_t *decoded);
void flac_decoder_close(void);

#endif
