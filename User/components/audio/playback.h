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
/* 每档寄存器变化 8（4 dB），范围 64～192；192 处再减静音，加音量取消静音。 */
void playback_adjust_volume(int direction);
void playback_toggle_mute(void);
/* 消费最近一次音量设置事件；同值调节也通知，以刷新提示时长。 */
bool playback_volume(uint8_t *attenuation, bool *muted);
void playback_deinit(void);
uint32_t playback_select(uint32_t index, const music_info_t *info, bool autoplay);
/* 超时返回 false，调用者必须保留旧卷，不能继续卸载/重新挂载。 */
bool playback_stop(void);
uint32_t playback_toggle(void);
bool playback_status(audio_status_t *status);
bool playback_notice(player_notice_t *notice);
bool playback_usb(bool *playing);
#endif
