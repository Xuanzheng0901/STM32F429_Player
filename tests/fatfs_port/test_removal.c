#include "mock_platform.h"
#include "ff.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "../../User/components/player/player.c"

static jmp_buf audio_exit, music_exit;
static unsigned stopped, started, toggled, removing_events, removed_events, notified;
static bool file_open, inject_controls = true;
static audio_state_t state = AUDIO_PLAYING;
static FIL audio_file;
static FATFS *mounted_filesystem;
static int callback_context;

bool audio_player_start(music_info_t *info)
{ (void)info; started++; return true; }
void audio_player_stop(void)
{
    assert(file_open);
    assert(f_close(&audio_file) == FR_INVALID_OBJECT);
    file_open = false;
    stopped++;
    state = AUDIO_STOPPED;
}
bool audio_player_toggle(void) { toggled++; return true; }
audio_state_t audio_player_state(void) { return state; }
uint32_t audio_player_elapsed(void) { return 0; }
audio_state_t audio_player_poll(uint32_t timeout)
{
    if(timeout)
    {
        if(inject_controls)
        {
            inject_controls = false;
            player_toggle_playback();
            player_set_repeat_one(true);
            player_request_track(0, true);
            assert(!s_audio_command_queue->count && !s_browse_request_queue->count);
            return state;
        }
        longjmp(audio_exit, 1);
    }
    return state;
}
static void run_audio(void)
{
    mock_wait_hook = NULL;
    if(setjmp(audio_exit) == 0)
        player_audio_task(NULL);
}
static void observed_event(sdcard_event_t event, void *context)
{
    assert(context == &callback_context);
    if(event == SDCARD_EVENT_REMOVING)
    {
        assert(mounted_filesystem->fs_type != 0 && file_open);
        assert(sdcard_is_removed());
        removing_events++;
        mock_wait_hook = run_audio;
    }
    else
    {
        assert(mounted_filesystem->fs_type == 0 && !file_open && stopped == 1);
        removed_events++;
    }
    player_handle_sd_event(event, NULL);
}
static void remove_card(void)
{
    mock_wait_hook = NULL;
    mock_present = 0;
    mock_detect_samples(3);
}
static bool window_changed(const music_window_t *window, uint32_t index, bool initial)
{
    (void)index; (void)initial;
    if(window->count)
    {
        DWORD free_clusters;
        assert(f_getfree("0:", &free_clusters, &mounted_filesystem) == FR_OK);
        assert(f_open(&audio_file, window->items[0].path, FA_READ) == FR_OK);
        file_open = true;
    }
    else
    {
        assert(removed_events == 1 && window->total == 0);
        notified++;
    }
    return true;
}
static bool playback_changed(uint32_t index, const music_info_t *info, uint32_t elapsed, bool playing)
{
    (void)index;
    assert(removed_events == 1 && !playing && elapsed == 0 && info->path[0] == 0);
    notified++;
    return true;
}
static bool failed(void)
{
    assert(removed_events == 1 && notified == 2);
    longjmp(music_exit, 1);
}
int main(void)
{
    uint8_t work[4096];
    MKFS_PARM options = {.fmt = FM_FAT, .au_size = 512};
    FATFS setup_filesystem;
    assert(f_mkfs("0:", &options, work, sizeof(work)) == FR_OK);
    assert(f_mount(&setup_filesystem, "0:/", 1) == FR_OK);
    FIL file;
    assert(f_open(&file, "0:/test.wav", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    assert(f_close(&file) == FR_OK);
    assert(f_mount(NULL, "0:/", 0) == FR_OK);
    s_audio_command_queue = xQueueCreate(1, sizeof(audio_command_t));
    s_playback_status_queue = xQueueCreate(1, sizeof(audio_status_t));
    s_browse_request_queue = xQueueCreate(1, sizeof(browse_request_t));
    s_audio_stop_ack_queue = xQueueCreate(1, sizeof(bool));
    s_audio_task_handle = (void *)1;
    s_ui_callbacks = (player_callbacks_t){window_changed, playback_changed, failed};
    sdcard_set_event_callback(observed_event, &callback_context);
    mock_wait_hook = remove_card;
    if(setjmp(music_exit) == 0)
        player_library_task(NULL);
    assert(stopped == 1 && started == 0 && toggled == 0 && !file_open);
    assert(s_audio_stop_ack_queue->count == 0);
    assert(removing_events == 1 && removed_events == 1 && mounted_filesystem->fs_type == 0);
    sdcard_unmount();
    mock_detect_samples(3);
    assert(removing_events == 1 && removed_events == 1);
    mock_present = 1;
    mock_detect_samples(3);
    assert(mounted_filesystem->fs_type == 0 && sdcard_is_removed());
    sdcard_set_event_callback(NULL, NULL);
    assert(sdcard_mount_and_scan() == FR_OK);
    assert(f_mount(NULL, "0:/", 0) == FR_OK);
    vQueueDelete(s_audio_command_queue);
    vQueueDelete(s_playback_status_queue);
    vQueueDelete(s_browse_request_queue);
    vQueueDelete(s_audio_stop_ack_queue);
    puts("diskio -> sdcard events -> player stop acknowledgement -> sdcard unmount -> UI passed");
}
