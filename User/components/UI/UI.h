#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>
#include "music.h"
#include "player.h"

void UI_init(void);


/* 以下接口在 UI_init() 返回后由曲库任务调用，内部复制数据并获取 LVGL 锁。
 * 不在中断或持有 LVGL 锁时调用，音频任务不能调用。
 */
/* 后台任务发布只读窗口；index 固定显示在中间行，首尾留白，过期浏览结果忽略。 */
bool UI_set_music_window(const music_window_t *window, uint32_t index, bool select);

/* 音频实际状态快照；选曲、格式和进度一次更新，不改变浏览焦点。 */
bool UI_set_player_status(uint32_t index, const music_info_t *info,
                          uint32_t elapsed_seconds, bool playing);

/* 建索引或读窗口失败时显示提示；保留已有列表供再次浏览。 */
bool UI_set_music_error(void);

void UI_set_media_state(player_media_t state);

#endif
