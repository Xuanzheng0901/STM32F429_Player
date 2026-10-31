#include "mock_platform.h"
#include "music_library.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t work[4096];
    FATFS filesystem;
    MKFS_PARM options = {.fmt = FM_FAT, .au_size = 512};
    assert(f_mkfs("0:", &options, work, sizeof(work)) == FR_OK);
    assert(f_mount(&filesystem, "0:", 1) == FR_OK);
    FIL file;
    assert(f_open(&file, "0:/Title - Artist.wav", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    assert(f_close(&file) == FR_OK);
    uint32_t total = 99;
    assert(music_library_rebuild(&total) == FR_OK && total == 1);
    music_window_t window;
    assert(music_library_load(total, 0, &window) == FR_OK);
    assert(window.total == 1 && window.count == 1);
    assert(strcmp(window.items[0].title, "Title") == 0);
    assert(strcmp(window.items[0].artist, "Artist") == 0);
    assert(window.items[0].type == MUSIC_WAV);
    assert(f_open(&file, "0:/Left - - Right.wav", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    assert(f_close(&file) == FR_OK);
    assert(music_library_rebuild(&total) == FR_OK && total == 2);
    assert(music_library_load(total, 0, &window) == FR_OK && window.count == 2);
    unsigned found = 0;
    for(unsigned i = 0; i < window.count; ++i)
        if(strcmp(window.items[i].path, "0:/Left - - Right.wav") == 0) {
            assert(strcmp(window.items[i].title, "Left -") == 0);
            assert(strcmp(window.items[i].artist, "Right") == 0);
            ++found;
        }
    assert(found == 1);
    assert(mock_task_calls == 0);
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    assert(music_library_rebuild(&total) == FR_NOT_ENABLED && total == 0);
    assert(music_library_load(1, 0, &window) == FR_NOT_ENABLED);
    assert(window.count == 0 && window.total == 0);
    puts("library runs independently of volume lifecycle and creates no tasks");
}
