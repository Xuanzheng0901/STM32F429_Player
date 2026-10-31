#include "sdcard.h"
#include "sdmmc.h"
#include "music_library.h"

static FATFS s_filesystem;
static uint32_t s_total;
static bool s_ready, s_inserted;
static unsigned s_insert_samples;

sdcard_event_t sdcard_poll(void)
{
    if(sdmmc_poll_removal()) {
        s_inserted = false;
        s_insert_samples = 0;
        return SDCARD_REMOVED;
    }
    if(!sdmmc_detected()) s_insert_samples = 0;
    else if(!s_inserted && ++s_insert_samples == 3) {
        s_inserted = true;
        return SDCARD_INSERTED;
    }
    return SDCARD_UNCHANGED;
}

bool sdcard_is_removed(void)
{
    return sdmmc_status() == SDMMC_REMOVED;
}

void sdcard_unmount(void)
{
    s_ready = false;
    s_total = 0;
    f_mount(NULL, "0:", 0);
}

FRESULT sdcard_mount_and_scan(void)
{
    sdcard_unmount();
    sdmmc_new_session();
    FRESULT result = f_mount(&s_filesystem, "0:", 1);
    if(result == FR_OK) result = music_library_rebuild(&s_total);
    s_ready = result == FR_OK;
    if(!s_ready) sdcard_unmount();
    return result;
}

FRESULT sdcard_load_music_window(uint32_t center, music_window_t *window)
{
    if(s_ready && !sdcard_is_removed()) return music_library_load(s_total, center, window);
    window->count = 0;
    window->first_index = window->total = 0;
    return FR_NOT_READY;
}
