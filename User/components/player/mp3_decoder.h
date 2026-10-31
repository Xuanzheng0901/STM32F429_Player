#ifndef MP3_DECODER_H
#define MP3_DECODER_H

#include "music.h"

/* 仅音频任务调用，文件由 audio_player 打开和关闭，解码工作区固定分配。 */
bool mp3_decoder_open(FIL *file, music_info_t *info);
bool mp3_decoder_read(int16_t *stereo, uint32_t frames, uint32_t *decoded);

#endif
