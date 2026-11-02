#ifndef PLAYER_H
#define PLAYER_H

#include "music.h"
#include "player_events.h"

typedef enum { PLAYER_NO_CARD, PLAYER_SCANNING } player_media_t;
typedef enum {
    PLAYER_PLAY_ONCE, PLAYER_PLAY_LIST, PLAYER_REPEAT_ONE, PLAYER_SHUFFLE
} player_mode_t;


/* 在曲库任务中同步通知，接收方复制数据、处理界面锁。
 * 接口可留空用于无界面播放，音频补数任务不调用这些接口。
 */
typedef struct {
    bool (*window)(const music_window_t *window, uint32_t index, bool initial);
    bool (*playback)(uint32_t index, const music_info_t *info, uint32_t elapsed, bool playing);
    bool (*error)(void);
    void (*media)(player_media_t state);
    void (*notice)(player_notice_t notice);
    void (*usb)(bool connected, bool playing);
    /* 曲库任务中每次自然结束通知一次；停止、拔卡、错误和 USB 不触发。 */
    void (*finished)(uint32_t index);
} player_callbacks_t;

/* UI、I2S 和 DAC 初始化后调用一次，复制通知接口并创建后台任务。 */
bool player_init(const player_callbacks_t *callbacks);
/* 非阻塞，可在 UI 回调中调用；后续浏览不会覆盖待处理的 select 确认。 */
void player_request(uint32_t index, bool select);
void player_toggle(void);
/* 不受卡状态或 USB 模式限制；每档 8 dB，+1 增大、-1 减小。 */
void player_adjust_volume(int direction);
/* 列表末尾回首曲；洗牌整轮不重复，手动选曲或切换模式会重建轮次。 */
void player_set_mode(player_mode_t mode);
/* 兼容旧接口：false 为播完停止，true 为单曲循环。 */
void player_set_repeat(bool repeat_one);

#endif //PLAYER_H
