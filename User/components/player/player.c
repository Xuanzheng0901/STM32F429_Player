#include "player.h"
#include "sdcard.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "log.h"
#include "audio_player.h"
#include "audio_output.h"
#include "usb_audio.h"

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

typedef struct {
    music_info_t info;
    uint32_t index;
    bool select, autoplay, toggle, set_repeat, repeat_one, stop;
} audio_command_t;

typedef struct {
    music_info_t info;
    uint32_t index, elapsed;
    audio_state_t state;
} audio_status_t;

static QueueHandle_t s_commands, s_status;
static TaskHandle_t s_audio_task;
static QueueHandle_t s_stop_ack;
static volatile bool s_available;
static volatile bool s_usb_mode, s_usb_playing;
static player_notice_t s_notices[8];
static unsigned s_notice_read, s_notice_write;

/* 音频任务只投递事件，LVGL 锁和墨水屏刷新由曲库任务承担。 */
static void post_notice(player_notice_t notice)
{
    taskENTER_CRITICAL();
    if(s_notice_write - s_notice_read == 8U) ++s_notice_read;
    s_notices[s_notice_write++ % 8U] = notice;
    taskEXIT_CRITICAL();
}

static void audio_select(uint32_t index, const music_info_t *info, bool autoplay)
{
    audio_command_t command = {0};
    taskENTER_CRITICAL();
    xQueuePeek(s_commands, &command, 0);
    command.info = *info;
    command.index = index;
    command.select = true;
    command.autoplay = autoplay;
    command.toggle = false;
    xQueueOverwrite(s_commands, &command);
    taskEXIT_CRITICAL();
    xTaskNotify(s_audio_task, AUDIO_CONTROL_EVENT, eSetBits);
}

static void audio_task(void *argument)
{
    (void)argument;
    audio_command_t command;
    audio_status_t status = {0};
    bool selected = false, repeat = false;
    uint32_t last_elapsed = UINT32_MAX;
    audio_state_t last_state = AUDIO_STOPPED;
    audio_output_init();
    while(1) {
        bool connected = usb_audio_connected();
        if(connected != s_usb_mode) {
            audio_output_set_active(false);
            audio_player_stop();
            usb_audio_stop();
            selected = false;
            s_usb_mode = connected;
            s_usb_playing = false;
            post_notice(connected ? PLAYER_USB_CONNECTED : PLAYER_USB_DISCONNECTED);
        }
        int ear = audio_output_poll();
        if(ear >= 0) post_notice(ear ? PLAYER_EAR_INSERTED : PLAYER_EAR_REMOVED);
        if(!s_usb_mode) audio_player_poll(0);
        /* 拔卡立即停止；即使暂停，也会周期读取卡状态。 */
        if(selected && sdcard_is_removed()) {
            audio_output_set_active(false);
            audio_player_stop();
            selected = false;
        }
        bool changed = false, start = false, pause_after_start = false;
        /* 合并控制命令，浏览不接触正在播放的曲目副本。 */
        if(xQueueReceive(s_commands, &command, 0) == pdTRUE) {
            if(command.stop) {
                if(!s_usb_mode) {
                    audio_output_set_active(false);
                    audio_player_stop();
                }
                selected = false;
                bool stopped = true;
                xQueueOverwrite(s_stop_ack, &stopped);
                continue;
            }
            if(s_usb_mode || sdcard_is_removed()) goto output;
            if(command.set_repeat) repeat = command.repeat_one;
            if(command.select) {
                audio_output_set_active(false);
                audio_player_stop();
                status.info = command.info;
                status.index = command.index;
                selected = changed = true;
                start = command.autoplay;
            }
            if(command.toggle && selected) {
                audio_state_t state = audio_player_state();
                if(start) pause_after_start = true;
                else if(state == AUDIO_PLAYING || state == AUDIO_PAUSED) audio_player_toggle();
                else start = true;
                changed = true;
            }
        }
        if(selected && audio_player_state() == AUDIO_FINISHED && repeat) {
            start = true;
            changed = true;
        }
        /* 统一处理起播和重播，后续格式解码也从这里分派。 */
        if(start) {
            audio_output_set_active(false);
            audio_player_start(&status.info);
            if(pause_after_start && audio_player_state() == AUDIO_PLAYING) audio_player_toggle();
        }
        /* 只投递状态，由曲库任务通知界面，避免屏幕刷新阻塞音频。 */
        status.state = audio_player_state();
        status.elapsed = status.state == AUDIO_STOPPED ? 0 : audio_player_elapsed();
        if(selected && (changed || status.state != last_state || status.elapsed != last_elapsed)) {
            xQueueOverwrite(s_status, &status);
            last_state = status.state;
            last_elapsed = status.elapsed;
        }
output:
        if(s_usb_mode) {
            if(!usb_audio_running()) usb_audio_start();
            usb_audio_poll(pdMS_TO_TICKS(20));
            s_usb_playing = usb_audio_audible();
            audio_output_set_active(s_usb_playing);
            continue;
        }
        audio_output_set_active(audio_player_state() == AUDIO_PLAYING);
        /* 只有音频任务补 DMA；UI 锁和墨水屏刷新不能阻塞这里。 */
        audio_player_poll(status.state == AUDIO_PLAYING ? pdMS_TO_TICKS(1000) : pdMS_TO_TICKS(20));
    }
}

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
        if(select) audio_select(index, &s_window.items[index - s_window.first_index], true);
        else if(s_callbacks.window) s_callbacks.window(&s_window, index, false);
    }
    else if(s_callbacks.error) s_callbacks.error();
    return result;
}

/* 关闭文件的确认是卸载卷的唯一同步点，不让 f_mount 与音频读文件并发。 */
static void stop_audio(void)
{
    s_available = false;
    audio_command_t command = {.stop = true};
    xQueueOverwrite(s_commands, &command);
    xTaskNotify(s_audio_task, AUDIO_CONTROL_EVENT, eSetBits);
    bool stopped;
    xQueueReceive(s_stop_ack, &stopped, portMAX_DELAY);
    xQueueReset(s_requests);
    xQueueReset(s_status);
}

static void music_task(void *argument)
{
    (void)argument;
    music_request_t request;
    audio_status_t status;
    bool card_seen = false, usb_mode = false, usb_playing = false;
    while(1) {
        if(usb_mode != s_usb_mode || usb_playing != s_usb_playing) {
            usb_mode = s_usb_mode;
            usb_playing = s_usb_playing;
            if(s_callbacks.usb) s_callbacks.usb(usb_mode, usb_playing);
        }
        while(1) {
            taskENTER_CRITICAL();
            bool pending = s_notice_read != s_notice_write;
            player_notice_t notice = pending ? s_notices[s_notice_read++ % 8U] : 0;
            taskEXIT_CRITICAL();
            if(!pending) break;
            /* 断开事件不能被状态快照合并掉，UI 忙时仍须恢复 SD 选曲。 */
            if(notice == PLAYER_USB_DISCONNECTED && !s_usb_mode && s_available && s_window.count)
                audio_select(s_window.first_index, &s_window.items[0], false);
            if(s_callbacks.notice) s_callbacks.notice(notice);
        }
        sdcard_event_t event = sdcard_poll();
        if(event == SDCARD_REMOVED) {
            if(card_seen && s_callbacks.notice) s_callbacks.notice(PLAYER_CARD_REMOVED);
            card_seen = false;
            stop_audio();
            sdcard_unmount();
            s_window.count = 0;
            s_window.first_index = s_window.total = 0;
            if(s_callbacks.media) s_callbacks.media(PLAYER_NO_CARD);
            LOGI("SD", "card removed");
        }
        if(event == SDCARD_INSERTED) {
            card_seen = true;
            if(s_callbacks.notice) s_callbacks.notice(PLAYER_CARD_INSERTED);
            stop_audio();
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
            if(s_window.count) audio_select(0, &s_window.items[0], false);
            if(s_callbacks.window) s_callbacks.window(&s_window, 0, true);
            s_available = true;
            LOGI("SD", "library ready: %lu songs, RTOS heap free=%u min=%u",
                 (unsigned long)s_window.total, (unsigned)xPortGetFreeHeapSize(),
                 (unsigned)xPortGetMinimumEverFreeHeapSize());
        }
        if(xQueueReceive(s_requests, &request, pdMS_TO_TICKS(20)) == pdTRUE && s_available && !s_usb_mode) {
            if(request.select && update_window(request.select_index, true, s_window.total) != FR_OK) continue;
            update_window(request.index, false, s_window.total);
        }
        if(xQueueReceive(s_status, &status, 0) != pdTRUE || !s_available || s_usb_mode) continue;
        if(s_callbacks.playback)
            s_callbacks.playback(status.index, &status.info, status.elapsed, status.state == AUDIO_PLAYING);
        if(status.state == AUDIO_ERROR && s_callbacks.error) s_callbacks.error();
    }
}

bool player_init(const player_callbacks_t *callbacks)
{
    if(s_requests) return true;
    s_callbacks = callbacks ? *callbacks : (player_callbacks_t){0};
    s_requests = xQueueCreate(1, sizeof(music_request_t));
    s_commands = xQueueCreate(1, sizeof(audio_command_t));
    s_status = xQueueCreate(1, sizeof(audio_status_t));
    s_stop_ack = xQueueCreate(1, sizeof(bool));
    if(!s_requests || !s_commands || !s_status || !s_stop_ack) goto failed;
    if(xTaskCreate(audio_task, "audio", 2048, NULL, 7, &s_audio_task) != pdPASS) goto failed;
    if(!xTaskCreateStatic(music_task, "music", MUSIC_STACK_WORDS, NULL, 1,
                         s_music_stack, &s_music_task_storage)) goto failed;
    return true;
failed:
    if(s_audio_task) vTaskDelete(s_audio_task);
    if(s_requests) vQueueDelete(s_requests);
    if(s_commands) vQueueDelete(s_commands);
    if(s_status) vQueueDelete(s_status);
    if(s_stop_ack) vQueueDelete(s_stop_ack);
    s_audio_task = NULL;
    s_requests = s_commands = s_status = s_stop_ack = NULL;
    return false;
}

void player_toggle(void)
{
    if(!s_available || usb_audio_connected()) return;
    audio_command_t command = {0};
    taskENTER_CRITICAL();
    xQueuePeek(s_commands, &command, 0);
    command.toggle = !command.toggle;
    xQueueOverwrite(s_commands, &command);
    taskEXIT_CRITICAL();
    xTaskNotify(s_audio_task, AUDIO_CONTROL_EVENT, eSetBits);
}

void player_set_repeat(bool repeat_one)
{
    if(!s_available || usb_audio_connected()) return;
    audio_command_t command = {0};
    taskENTER_CRITICAL();
    xQueuePeek(s_commands, &command, 0);
    command.set_repeat = true;
    command.repeat_one = repeat_one;
    xQueueOverwrite(s_commands, &command);
    taskEXIT_CRITICAL();
    xTaskNotify(s_audio_task, AUDIO_CONTROL_EVENT, eSetBits);
}

void player_request(uint32_t index, bool select)
{
    if(!s_available || usb_audio_connected()) return;
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
