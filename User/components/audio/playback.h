#ifndef PLAYBACK_H
#define PLAYBACK_H
#include "audio_player.h"
#include "player_events.h"

typedef struct {
    music_info_t info;
    uint32_t index, elapsed;
    audio_state_t state;
} audio_status_t;

bool playback_init(void);
void playback_deinit(void);
void playback_select(uint32_t index, const music_info_t *info, bool autoplay);
void playback_stop(void);
void playback_toggle(void);
void playback_set_repeat(bool repeat_one);
bool playback_status(audio_status_t *status);
bool playback_notice(player_notice_t *notice);
bool playback_usb(bool *playing);
#endif
