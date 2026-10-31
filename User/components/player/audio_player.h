#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include "music.h"

typedef enum {
    AUDIO_STOPPED, AUDIO_PLAYING, AUDIO_PAUSED, AUDIO_FINISHED, AUDIO_ERROR
} audio_state_t;

#define AUDIO_CONTROL_EVENT 8U

/* 仅音频任务调用。FIL 和 DMA 缓冲均位于普通 SRAM。 */
bool audio_player_start(music_info_t *info);
void audio_player_stop(void);
bool audio_player_toggle(void);
audio_state_t audio_player_poll(uint32_t timeout_ticks);
audio_state_t audio_player_state(void);
uint32_t audio_player_elapsed(void);

#endif
