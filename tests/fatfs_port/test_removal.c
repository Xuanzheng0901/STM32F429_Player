#include "mock_platform.h"
#include "ff.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "../../User/components/player/player.c"

static jmp_buf audio_exit, music_exit;
static unsigned windows, absent, starts, closes;
static bool file_open, removed, paused_case, quick_case;
static audio_state_t state;
static FIL file;

bool audio_player_start(music_info_t *info)
{
    assert(!file_open && !sdcard_is_removed());
    assert(f_open(&file, info->path, FA_READ) == FR_OK);
    file_open = true;
    state = AUDIO_PLAYING;
    ++starts;
    return true;
}
void audio_player_stop(void)
{
    if(file_open) {
        /* 卷必须保持有效，直到文件所有者完成关闭。 */
        assert(file.obj.fs->fs_type != 0);
        FRESULT result = f_close(&file);
        assert(result == FR_OK || result == FR_INVALID_OBJECT);
        file_open = false;
        ++closes;
    }
    state = AUDIO_STOPPED;
}
bool audio_player_toggle(void)
{
    state = state == AUDIO_PLAYING ? AUDIO_PAUSED : AUDIO_PLAYING;
    return true;
}
audio_state_t audio_player_state(void) { return state; }
uint32_t audio_player_elapsed(void) { return 0; }
audio_state_t audio_player_poll(uint32_t timeout)
{
    if(!timeout) return state;
    if(s_available && windows == 1 && !removed) {
        if(state == AUDIO_STOPPED) {
            player_toggle();
            return state;
        }
        if(paused_case && state == AUDIO_PLAYING) {
            player_toggle();
            return state;
        }
        assert(file_open);
        removed = true;
        mock_present = 0;
        assert(sdcard_is_removed());
        if(quick_case) mock_present = 1;
        return state;
    }
    longjmp(audio_exit, 1);
}
static void schedule(void)
{
    if(setjmp(audio_exit) == 0) audio_task(NULL);
}
static void observed_media(player_media_t media)
{
    if(media != PLAYER_NO_CARD) return;
    ++absent;
    FILINFO info;
    assert(f_stat("0:/track.wav", &info) == FR_NOT_ENABLED);
    assert(!file_open && !s_available);
    player_toggle();
    player_set_repeat(true);
    player_request(0, true);
    assert(!s_commands->count && !s_requests->count);
    mock_present = 1;
}
static bool window_changed(const music_window_t *window, uint32_t index, bool initial)
{
    assert(window->count == 1 && index == 0 && initial);
    if(++windows == 2) {
        assert(absent == 2 && starts == 1 && closes == 1);
        assert(!file_open && state == AUDIO_STOPPED);
        longjmp(music_exit, 1);
    }
    return true;
}
static bool playback_changed(uint32_t index, const music_info_t *info, uint32_t elapsed, bool playing)
{
    (void)index; (void)info; (void)elapsed; (void)playing;
    return true;
}
static bool failed(void) { assert(!"unexpected media error"); return false; }

int main(int argc, char **argv)
{
    paused_case = argc > 1 && strcmp(argv[1], "paused") == 0;
    quick_case = argc > 1 && strcmp(argv[1], "quick") == 0;
    uint8_t work[4096];
    FATFS filesystem;
    MKFS_PARM options = {.fmt = FM_FAT, .au_size = 512};
    assert(f_mkfs("0:", &options, work, sizeof(work)) == FR_OK);
    assert(f_mount(&filesystem, "0:", 1) == FR_OK);
    FIL setup;
    assert(f_open(&setup, "0:/track.wav", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    assert(f_close(&setup) == FR_OK);
    assert(f_mount(NULL, "0:", 0) == FR_OK);

    s_requests = xQueueCreate(1, sizeof(music_request_t));
    s_commands = xQueueCreate(1, sizeof(audio_command_t));
    s_status = xQueueCreate(1, sizeof(audio_status_t));
    s_stop_ack = xQueueCreate(1, sizeof(bool));
    s_audio_task = (void *)1;
    s_callbacks = (player_callbacks_t){window_changed, playback_changed, failed, observed_media};
    mock_present = 0;
    mock_wait_hook = schedule;
    if(setjmp(music_exit) == 0) music_task(NULL);
    sdcard_unmount();
    assert(!s_stop_ack->count);
    vQueueDelete(s_requests);
    vQueueDelete(s_commands);
    vQueueDelete(s_status);
    vQueueDelete(s_stop_ack);
    puts("absent boot -> insert -> play/pause -> remove -> close -> unmount -> reinsert passed");
}
