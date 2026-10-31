#include "mock_platform.h"
#include "music_library.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool browsing;
FRESULT __real_f_open(FIL *, const TCHAR *, BYTE);
FRESULT __wrap_f_open(FIL *file, const TCHAR *path, BYTE mode)
{
    /* 模拟翻页时目录查找被阻塞：正常浏览不得调用任何 f_open。 */
    if(browsing) return FR_TIMEOUT;
    return __real_f_open(file, path, mode);
}

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
    /* 跨越多个窗口；真实标签必须来自建库缓存，而不是播放时重新打开歌曲。 */
    static const uint8_t tagged[] = {
        'R','I','F','F',30,0,0,0,'W','A','V','E',
        'L','I','S','T',18,0,0,0,'I','N','F','O','I','N','A','M',5,0,0,0,'K','e','p','t',0,0
    };
    for(unsigned i = 0; i < 47; ++i) {
        char path[40];
        snprintf(path, sizeof(path), "0:/Cache%02u.wav", i);
        assert(f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
        if(!i) {
            UINT written;
            assert(f_write(&file, tagged, sizeof(tagged), &written) == FR_OK && written == sizeof(tagged));
        }
        assert(f_close(&file) == FR_OK);
    }
    assert(music_library_rebuild(&total) == FR_OK && total == 49);
    assert(f_unlink("0:/Cache00.wav") == FR_OK);
    browsing = true;
    unsigned cached_tags_seen = 0;
    for(unsigned i = 0; i < 100; ++i) {
        assert(music_library_load(total, i * 17 % total, &window) == FR_OK);
        assert(window.count == MUSIC_WINDOW_COUNT);
        for(unsigned j = 0; j < window.count; ++j)
            if(!strcmp(window.items[j].path, "0:/Cache00.wav")) {
                assert(!strcmp(window.items[j].title, "Kept"));
                ++cached_tags_seen;
            }
    }
    assert(cached_tags_seen);
    browsing = false;
    music_library_close();
    assert(mock_task_calls == 0);
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    assert(music_library_rebuild(&total) == FR_NOT_ENABLED && total == 0);
    assert(music_library_load(1, 0, &window) == FR_NOT_ENABLED);
    assert(window.count == 0 && window.total == 0);
    puts("library runs independently of volume lifecycle and creates no tasks");
}
