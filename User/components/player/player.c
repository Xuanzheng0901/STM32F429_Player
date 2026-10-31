#include "player.h"
#include "sdcard.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "log.h"
#include "audio_player.h"

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
    while(1) {
        audio_player_poll(0);
        /* 拔卡立即停止；即使暂停，也会周期读取卡状态。 */
        if(selected && sdcard_is_removed()) {
            audio_player_stop();
            selected = false;
        }
        bool changed = false, start = false, pause_after_start = false;
        /* 合并控制命令，浏览不接触正在播放的曲目副本。 */
        if(xQueueReceive(s_commands, &command, 0) == pdTRUE) {
            if(command.stop) {
                audio_player_stop();
                selected = false;
                bool stopped = true;
                xQueueOverwrite(s_stop_ack, &stopped);
                continue;
            }
            if(sdcard_is_removed()) continue;
            if(command.set_repeat) repeat = command.repeat_one;
            if(command.select) {
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
    while(1) {
        sdcard_event_t event = sdcard_poll();
        if(event == SDCARD_REMOVED) {
            stop_audio();
            sdcard_unmount();
            s_window.count = 0;
            s_window.first_index = s_window.total = 0;
            if(s_callbacks.media) s_callbacks.media(PLAYER_NO_CARD);
            LOGI("SD", "card removed");
        }
        if(event == SDCARD_INSERTED) {
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
            LOGI("SD", "library ready: %lu songs", (unsigned long)s_window.total);
        }
        if(xQueueReceive(s_requests, &request, pdMS_TO_TICKS(20)) == pdTRUE && s_available) {
            if(request.select && update_window(request.select_index, true, s_window.total) != FR_OK) continue;
            update_window(request.index, false, s_window.total);
        }
        if(xQueueReceive(s_status, &status, 0) != pdTRUE || !s_available) continue;
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
    if(xTaskCreate(music_task, "music", 4096, NULL, 1, NULL) != pdPASS) goto failed;
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
    if(!s_available) return;
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
    if(!s_available) return;
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
    if(!s_available) return;
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
