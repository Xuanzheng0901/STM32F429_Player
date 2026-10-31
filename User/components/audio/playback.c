#include "playback.h"
#include "sdcard.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "audio_output.h"
#include "usb_audio.h"

typedef struct {
    music_info_t info;
    uint32_t index, serial;
    bool select, autoplay, toggle, stop;
} audio_command_t;

static uint32_t s_serial;
static QueueHandle_t s_commands, s_status;
static TaskHandle_t s_audio_task;
static QueueHandle_t s_stop_ack;
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

uint32_t playback_select(uint32_t index, const music_info_t *info, bool autoplay)
{
    audio_command_t command = {0};
    taskENTER_CRITICAL();
    xQueuePeek(s_commands, &command, 0);
    command.serial = ++s_serial;
    command.info = *info;
    command.index = index;
    command.select = true;
    command.autoplay = autoplay;
    command.toggle = false;
    xQueueOverwrite(s_commands, &command);
    taskEXIT_CRITICAL();
    xTaskNotify(s_audio_task, AUDIO_CONTROL_EVENT, eSetBits);
    return command.serial;
}

static void audio_task(void *argument)
{
    (void)argument;
    audio_command_t command;
    audio_status_t status = {0};
    bool selected = false;
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
            xQueueReset(s_status);
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
            changed = status.serial != command.serial;
            status.serial = command.serial;
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


bool playback_init(void)
{
    s_commands = xQueueCreate(1, sizeof(audio_command_t));
    s_status = xQueueCreate(1, sizeof(audio_status_t));
    s_stop_ack = xQueueCreate(1, sizeof(bool));
    if(s_commands && s_status && s_stop_ack &&
       xTaskCreate(audio_task, "audio", 2048, NULL, 7, &s_audio_task) == pdPASS) return true;
    playback_deinit();
    return false;
}

void playback_deinit(void)
{
    if(s_audio_task) vTaskDelete(s_audio_task);
    if(s_commands) vQueueDelete(s_commands);
    if(s_status) vQueueDelete(s_status);
    if(s_stop_ack) vQueueDelete(s_stop_ack);
    s_audio_task = NULL;
    s_commands = s_status = s_stop_ack = NULL;
}

void playback_stop(void)
{
    audio_command_t command = {.stop = true};
    xQueueOverwrite(s_commands, &command);
    xTaskNotify(s_audio_task, AUDIO_CONTROL_EVENT, eSetBits);
    bool stopped;
    xQueueReceive(s_stop_ack, &stopped, portMAX_DELAY);
    xQueueReset(s_status);
}

bool playback_status(audio_status_t *status) { return xQueueReceive(s_status, status, 0) == pdTRUE; }
bool playback_usb(bool *playing) { *playing = s_usb_playing; return s_usb_mode; }
bool playback_notice(player_notice_t *notice)
{
    taskENTER_CRITICAL();
    bool pending = s_notice_read != s_notice_write;
    if(pending) *notice = s_notices[s_notice_read++ % 8U];
    taskEXIT_CRITICAL();
    return pending;
}

uint32_t playback_toggle(void)
{
    if(usb_audio_connected()) return 0;
    audio_command_t command = {0};
    taskENTER_CRITICAL();
    xQueuePeek(s_commands, &command, 0);
    command.serial = ++s_serial;
    command.toggle = !command.toggle;
    xQueueOverwrite(s_commands, &command);
    taskEXIT_CRITICAL();
    xTaskNotify(s_audio_task, AUDIO_CONTROL_EVENT, eSetBits);
    return command.serial;
}
