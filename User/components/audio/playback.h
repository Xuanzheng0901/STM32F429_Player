#ifndef PLAYBACK_H
#define PLAYBACK_H
#include "audio_player.h"
#include "player_events.h"

typedef struct {
    music_info_t info;
    uint32_t index, elapsed, serial;
    audio_state_t state;
} audio_status_t;

bool playback_init(void);
void playback_deinit(void);
uint32_t playback_select(uint32_t index, const music_info_t *info, bool autoplay);
void playback_stop(void);
uint32_t playback_toggle(void);
bool playback_status(audio_status_t *status);
bool playback_notice(player_notice_t *notice);
bool playback_usb(bool *playing);
#endif
