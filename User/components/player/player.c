#include "player.h"
#include "sdcard.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "log.h"
#include "playback.h"
#include "player_shuffle.h"

#define MUSIC_STACK_WORDS 4096U

/* 保留 16 KiB 曲库栈容量，改为普通 SRAM 静态分配，腾出 CCMRAM 给 LVGL。 */
static _Alignas(8) StackType_t s_music_stack[MUSIC_STACK_WORDS];
static StaticTask_t s_music_task_storage;

typedef struct {
    uint32_t index;
    uint32_t select_index;
    bool select;
} music_request_t;

static QueueHandle_t s_requests;
static music_window_t s_window;
static player_callbacks_t s_callbacks;

static volatile bool s_available;
static volatile player_mode_t s_mode = PLAYER_PLAY_LIST;
static volatile bool s_shuffle_reset = true;
static uint32_t s_selection, s_finished;

static FRESULT update_window(uint32_t index, bool select, uint32_t total)
{
    if(index >= total) return FR_INVALID_PARAMETER;
    uint32_t first = index > 2 ? index - 2 : 0;
    uint32_t last_first = total > 5 ? total - 5 : 0;
    if(first > last_first) first = last_first;
    uint32_t count = total < 5 ? total : 5;
    FRESULT result = FR_OK;
    if(!s_window.count || first < s_window.first_index || first + count > s_window.first_index + s_window.count)
        result = sdcard_load_music_window(index, &s_window);
    if(result != FR_OK) LOGE("SD", "read music window failed: FatFs=%d", (int)result);
    if(result == FR_OK) {
        if(select) s_selection = playback_select(index, &s_window.items[index - s_window.first_index], true);
        else if(s_callbacks.window) s_callbacks.window(&s_window, index, false);
    }
    else if(s_callbacks.error) s_callbacks.error();
    return result;
}

/* 关闭文件的确认是卸载卷的唯一同步点，不让 f_mount 与音频读文件并发。 */
static bool stop_audio(void)
{
    s_available = false;
    xQueueReset(s_requests);
    return playback_stop();
}

static void music_task(void *argument)
{
    (void)argument;
    music_request_t request;
    audio_status_t status;
    bool card_seen = false, usb_mode = false, usb_playing = false;
    while(1) {
        uint8_t attenuation;
        if(playback_volume(&attenuation) && s_callbacks.volume) s_callbacks.volume(attenuation);
        bool active, connected = playback_usb(&active);
        if(usb_mode != connected || usb_playing != active) {
            usb_mode = connected;
            usb_playing = active;
            if(s_callbacks.usb) s_callbacks.usb(usb_mode, usb_playing);
        }
        player_notice_t notice;
        while(playback_notice(&notice)) {
            /* 断开事件不能被状态快照合并掉，UI 忙时仍须恢复 SD 选曲。 */
            if(notice == PLAYER_USB_DISCONNECTED && !usb_mode && s_available && s_window.count) {
                s_selection = playback_select(s_window.first_index, &s_window.items[0], false);
                s_shuffle_reset = true;
            }
            if(s_callbacks.notice) s_callbacks.notice(notice);
        }
        sdcard_event_t event = sdcard_poll();
        if(event == SDCARD_REMOVED) {
            if(card_seen && s_callbacks.notice) s_callbacks.notice(PLAYER_CARD_REMOVED);
            card_seen = false;
            LOGI("SD", "waiting for audio stop");
            if(!stop_audio()) {
                if(s_callbacks.error) s_callbacks.error();
                continue;
            }
            sdcard_unmount();
            s_window.count = 0;
            s_window.first_index = s_window.total = 0;
            if(s_callbacks.media) s_callbacks.media(PLAYER_NO_CARD);
            LOGI("SD", "card removed");
        }
        if(event == SDCARD_INSERTED) {
            card_seen = true;
            if(s_callbacks.notice) s_callbacks.notice(PLAYER_CARD_INSERTED);
            LOGI("SD", "waiting for audio stop");
            if(!stop_audio()) {
                if(s_callbacks.error) s_callbacks.error();
                continue;
            }
            LOGI("SD", "audio stopped, mounting card");
            if(s_callbacks.media) s_callbacks.media(PLAYER_SCANNING);
            FRESULT result = sdcard_mount_and_scan();
            if(result == FR_OK) result = sdcard_load_music_window(0, &s_window);
            if(result != FR_OK) {
                LOGE("SD", "mount/scan failed: FatFs=%d", (int)result);
                sdcard_unmount();
                if(s_callbacks.error) s_callbacks.error();
                continue;
            }
            /* 首次选中但不自动播放，重新插卡不沿用旧卡的曲目或控制命令。 */
            if(s_window.count) s_selection = playback_select(0, &s_window.items[0], false);
            s_shuffle_reset = true;
            if(s_callbacks.window) s_callbacks.window(&s_window, 0, true);
            s_available = true;
            LOGI("SD", "library ready: %lu songs, RTOS heap free=%u min=%u",
                 (unsigned long)s_window.total, (unsigned)xPortGetFreeHeapSize(),
                 (unsigned)xPortGetMinimumEverFreeHeapSize());
        }
        if(xQueueReceive(s_requests, &request, pdMS_TO_TICKS(20)) == pdTRUE && s_available && !usb_mode) {
            if(request.select) s_shuffle_reset = true;
            if(request.select && update_window(request.select_index, true, s_window.total) != FR_OK) continue;
            update_window(request.index, false, s_window.total);
        }
        if(!playback_status(&status) || !s_available || usb_mode || status.serial != s_selection) continue;
        if(s_callbacks.playback)
            s_callbacks.playback(status.index, &status.info, status.elapsed, status.state == AUDIO_PLAYING);
        if(status.state == AUDIO_ERROR && s_callbacks.error) s_callbacks.error();
        if(status.state != AUDIO_FINISHED || status.serial == s_finished || status.serial != s_selection) continue;
        s_finished = status.serial;
        if(s_callbacks.finished) s_callbacks.finished(status.index);
        if(status.serial != s_selection) continue;
        /* 完成回调或用户已有选曲请求时，优先执行显式命令。 */
        if(xQueuePeek(s_requests, &request, 0) == pdTRUE && request.select) continue;
        uint32_t next = status.index;
        if(s_mode == PLAYER_PLAY_ONCE) continue;
        if(s_mode == PLAYER_PLAY_LIST) next = (next + 1U) % s_window.total;
        if(s_mode == PLAYER_SHUFFLE) {
            if(s_shuffle_reset) {
                player_shuffle_reset(s_window.total, status.index, xTaskGetTickCount());
                s_shuffle_reset = false;
            }
            next = player_shuffle_next(status.index);
        }
        if(update_window(next, true, s_window.total) == FR_OK && s_callbacks.window)
            s_callbacks.window(&s_window, next, true);
    }
}

bool player_init(const player_callbacks_t *callbacks)
{
    if(s_requests) return true;
    s_callbacks = callbacks ? *callbacks : (player_callbacks_t){0};
    s_requests = xQueueCreate(1, sizeof(music_request_t));
    if(!s_requests || !playback_init()) goto failed;
    if(!xTaskCreateStatic(music_task, "music", MUSIC_STACK_WORDS, NULL, 1,
                         s_music_stack, &s_music_task_storage)) goto failed;
    return true;
failed:
    playback_deinit();
    if(s_requests) vQueueDelete(s_requests);
    s_requests = NULL;
    return false;
}

void player_toggle(void)
{
    if(!s_available) return;
    uint32_t serial = playback_toggle();
    if(serial) s_selection = serial;
}
void player_adjust_volume(int direction) { playback_adjust_volume(direction); }
void player_set_mode(player_mode_t mode)
{
    s_mode = mode;
    s_shuffle_reset = true;
}
void player_set_repeat(bool repeat_one) { player_set_mode(repeat_one ? PLAYER_REPEAT_ONE : PLAYER_PLAY_ONCE); }

void player_request(uint32_t index, bool select)
{
    bool active;
    if(!s_available || playback_usb(&active)) return;
    music_request_t request = {.index = index, .select_index = index, .select = select};
    music_request_t pending;
    /* 浏览可以合并，但不能覆盖尚未处理的选曲确认。 */
    taskENTER_CRITICAL();
    if(!select && xQueuePeek(s_requests, &pending, 0) == pdTRUE && pending.select) {
        request.select = true;
        request.select_index = pending.select_index;
    }
    xQueueOverwrite(s_requests, &request);
    taskEXIT_CRITICAL();
}
