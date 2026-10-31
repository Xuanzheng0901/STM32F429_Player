#include "mock_platform.h"
#include "ff.h"
#include "diskio.h"
#include "sdcard.h"
#include "music_library.h"
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

/* 独立卷测试中没有音频文件；由调用方处理卸载事件。 */
static void detect_samples(unsigned count)
{
    while(count--)
        if(sdcard_poll() == SDCARD_REMOVED) sdcard_unmount();
}

int main(void)
{
    const char volume[] = "0:";
    const char folder[] = "0:/音乐";
    const char path[] = "0:/音乐/中文 long.flac";
    const char wave_path[] = "0:/音乐/长文件名.wav";
    const uint8_t wave[] = {'R','I','F','F',30,0,0,0,'W','A','V','E',
        'L','I','S','T',18,0,0,0,'I','N','F','O','I','N','A','M',5,0,0,0,
        0xd2,0xf4,0xc0,0xd6,0,0};
    uint8_t work[4096], source[10007], target[10008];
    for(unsigned i = 0; i < sizeof(source); i++) source[i] = (uint8_t)(i * 13 + i / 7);
    FATFS filesystem;
    assert(sizeof(TCHAR) == 1);
    assert(ff_oem2uni(0xd2f4, 936) == 0x97f3);
    assert(ff_uni2oem(0x97f3, 936) == 0xd2f4);
    MKFS_PARM options = {.fmt = FM_FAT, .au_size = 512};
    assert(f_mkfs(volume, &options, work, sizeof(work)) == FR_OK);
    assert(f_mount(&filesystem, volume, 1) == FR_OK);
    assert(f_mkdir(folder) == FR_OK);
    FIL file;
    UINT written, read;
    assert(f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE | FA_READ) == FR_OK);
    assert(f_write(&file, source, sizeof(source), &written) == FR_OK && written == sizeof(source));
    assert(f_sync(&file) == FR_OK);
    assert(f_lseek(&file, 0) == FR_OK);
    assert(f_read(&file, target + 1, sizeof(source), &read) == FR_OK && read == sizeof(source));
    assert(memcmp(target + 1, source, sizeof(source)) == 0);
    assert(f_lseek(&file, 509) == FR_OK);
    assert(f_read(&file, target, 1234, &read) == FR_OK && read == 1234);
    assert(memcmp(target, source + 509, read) == 0);
    assert(f_close(&file) == FR_OK);
    assert(f_chmod(path, AM_HID, AM_HID) == FR_OK);
    FILINFO info;
    assert(f_stat(path, &info) == FR_OK && (info.fattrib & AM_HID));
    assert(f_open(&file, wave_path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    assert(f_write(&file, wave, sizeof(wave), &written) == FR_OK && written == sizeof(wave));
    assert(f_close(&file) == FR_OK);
    char long_path[MUSIC_PATH_CAPACITY], emoji_path[MUSIC_PATH_CAPACITY];
    strcpy(long_path, folder);
    strcat(long_path, "/");
    for(unsigned i = 0; i < 250; i++) strcat(long_path, "\xe9\x9f\xb3");
    strcat(long_path, ".flac");
    assert(strlen(long_path) > 512);
    assert(f_open(&file, long_path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    assert(f_close(&file) == FR_OK);
    strcpy(emoji_path, folder);
    strcat(emoji_path, "/");
    size_t offset = strlen(emoji_path);
    memset(emoji_path + offset, 'A', 61);
    emoji_path[offset + 61] = 0;
    strcat(emoji_path, "\xf0\x9f\x98\x80 - ");
    offset = strlen(emoji_path);
    memset(emoji_path + offset, 'B', 30);
    emoji_path[offset + 30] = 0;
    strcat(emoji_path, "\xf0\x9f\x98\x80.flac");
    assert(f_open(&file, emoji_path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    assert(f_close(&file) == FR_OK);
    assert(f_mount(NULL, volume, 0) == FR_OK);

    /* 直接编译现有曲库源码，验证路径索引、隐藏属性和 GBK 标签转换接口。 */
    assert(sdcard_mount_and_scan() == FR_OK);
    music_window_t window;
    assert(sdcard_load_music_window(0, &window) == FR_OK);
    assert(window.total == 3 && window.count == 3);
    unsigned found = 0;
    for(unsigned i = 0; i < window.count; i++)
    {
        music_info_t *item = &window.items[i];
        if(strcmp(item->path, wave_path) == 0) {
            assert(item->type == MUSIC_WAV);
            assert(strcmp(item->title, "\xe9\x9f\xb3\xe4\xb9\x90") == 0);
            found |= 1;
        } else if(strcmp(item->path, long_path) == 0) {
            assert(strlen(item->title) == 63);
            for(unsigned j = 0; j < 21; j++)
                assert(memcmp(item->title + j * 3, "\xe9\x9f\xb3", 3) == 0);
            found |= 2;
        } else if(strcmp(item->path, emoji_path) == 0) {
            assert(strlen(item->title) == 61 && strlen(item->artist) == 30);
            found |= 4;
        }
    }
    assert(found == 7);
    assert(f_stat("0:/music.idx", &info) == FR_OK);
    assert(info.fsize == 3 * sizeof(music_info_t));
    assert(sdcard_mount_and_scan() == FR_OK);
    assert(sdcard_load_music_window(0, &window) == FR_OK && window.total == 3);
    assert(mock_task_calls == 0);
    mock_present = 0;
    detect_samples(1);
    mock_present = 1;
    detect_samples(1);
    assert(!sdcard_is_removed());
    mock_present = 0;
    detect_samples(1);
    detect_samples(1);
    detect_samples(1);
    assert(sdcard_is_removed());
    detect_samples(1);
    assert(sdcard_load_music_window(0, &window) == FR_NOT_READY);
    assert(window.count == 0 && window.total == 0);
    sdcard_unmount();
    sdcard_unmount();
    mock_present = 1;
    detect_samples(1);
    assert(sdcard_is_removed());
    assert(sdcard_load_music_window(0, &window) == FR_NOT_READY);
    assert(sdcard_mount_and_scan() == FR_OK && !sdcard_is_removed());
    assert(sdcard_load_music_window(0, &window) == FR_OK && window.total == 3);
    assert(mock_task_calls == 0);
    /* A transfer sees removal before the debounce task; a quick reinsert stays invalid. */
    assert(f_open(&file, wave_path, FA_READ) == FR_OK);
    mock_present = 0;
    assert(disk_status(0) & STA_NOINIT);
    mock_present = 1;
    unsigned dma_before = mock_dma_calls;
    assert(f_read(&file, target, 1, &read) == FR_INVALID_OBJECT);
    assert(mock_dma_calls == dma_before);
    assert(f_close(&file) == FR_INVALID_OBJECT);
    detect_samples(1);
    assert(sdcard_is_removed());
    assert(f_stat("0:/music.idx", &info) == FR_NOT_ENABLED);
    sdcard_unmount();
    assert(sdcard_mount_and_scan() == FR_OK);
    assert(f_unlink(path) == FR_OK);
    mock_init_fail = 1;
    assert(sdcard_mount_and_scan() == FR_NOT_READY);
    assert(f_stat("0:/music.idx", &info) == FR_NOT_ENABLED);
    assert(sdcard_load_music_window(0, &window) == FR_NOT_READY && window.count == 0);
    mock_init_fail = 0;
    assert(sdcard_mount_and_scan() == FR_OK);
    assert(sdcard_load_music_window(0, &window) == FR_OK && window.total == 3);

    /* 大曲库窗口在首尾夹紧，大小写扩展名一致识别。 */
    for(unsigned i = 0; i < 17; ++i) {
        char name[40];
        snprintf(name, sizeof(name), "0:/Track %02u.WaV", i);
        assert(f_open(&file, name, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
        assert(f_close(&file) == FR_OK);
    }
    assert(sdcard_mount_and_scan() == FR_OK);
    assert(sdcard_load_music_window(0, &window) == FR_OK);
    assert(window.total == 20 && window.count == 15 && window.first_index == 0);
    assert(sdcard_load_music_window(19, &window) == FR_OK);
    assert(window.total == 20 && window.count == 15 && window.first_index == 5);
    unsigned uppercase_tracks = 0;
    for(unsigned i = 0; i < window.count; ++i)
        if(strstr(window.items[i].path, ".WaV")) {
            assert(window.items[i].type == MUSIC_WAV);
            ++uppercase_tracks;
        }
    assert(uppercase_tracks >= 12);

    /* 损坏记录和短索引不能发布部分窗口；重新扫描可恢复。 */
    music_library_close();
    assert(f_open(&file, "0:/music.idx", FA_WRITE) == FR_OK);
    assert(f_lseek(&file, offsetof(music_info_t, path) + MUSIC_PATH_CAPACITY - 1) == FR_OK);
    assert(f_write(&file, "X", 1, &written) == FR_OK && written == 1);
    assert(f_close(&file) == FR_OK);
    assert(sdcard_load_music_window(0, &window) == FR_INT_ERR);
    assert(window.count == 0 && window.total == 0);
    assert(f_open(&file, "0:/music.idx", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    assert(f_close(&file) == FR_OK);
    assert(sdcard_load_music_window(0, &window) == FR_INT_ERR);
    assert(window.count == 0 && window.total == 0);
    for(unsigned i = 0; i < 17; ++i) {
        char name[40];
        snprintf(name, sizeof(name), "0:/Track %02u.WaV", i);
        assert(f_unlink(name) == FR_OK);
    }
    assert(f_unlink(wave_path) == FR_OK);
    assert(f_unlink(long_path) == FR_OK);
    assert(f_unlink(emoji_path) == FR_OK);
    /* 递归共用一个路径缓冲，深度上限和返回父目录后的兄弟文件保持正确。 */
    const char *directories[] = {"0:/d1", "0:/d1/d2", "0:/d1/d2/d3", "0:/d1/d2/d3/d4", "0:/d1/d2/d3/d4/d5"};
    const char *tracks[] = {"0:/d1/d2/d3/d4/deep.wav", "0:/d1/d2/d3/d4/d5/skip.wav", "0:/sibling.mp3"};
    for(unsigned i = 0; i < 5; ++i) assert(f_mkdir(directories[i]) == FR_OK);
    for(unsigned i = 0; i < 3; ++i) {
        assert(f_open(&file, tracks[i], FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
        assert(f_close(&file) == FR_OK);
    }
    assert(sdcard_mount_and_scan() == FR_OK);
    assert(sdcard_load_music_window(0, &window) == FR_OK && window.total == 2 && window.count == 2);
    unsigned matched = 0;
    for(unsigned i = 0; i < window.count; ++i) {
        if(strcmp(window.items[i].path, tracks[0]) == 0) matched |= 1;
        if(strcmp(window.items[i].path, tracks[2]) == 0) matched |= 2;
    }
    assert(matched == 3);
    for(unsigned i = 0; i < 3; ++i) assert(f_unlink(tracks[i]) == FR_OK);
    for(unsigned i = 5; i > 0; --i) assert(f_unlink(directories[i - 1]) == FR_OK);
    assert(sdcard_mount_and_scan() == FR_OK);
    assert(sdcard_load_music_window(0, &window) == FR_OK);
    assert(window.count == 0 && window.total == 0 && window.first_index == 0);
    assert(f_mount(NULL, volume, 0) == FR_OK);
    assert(mock_dma_calls && mock_yields);
    puts("R0.16 format, UTF-8 LFN, partial I/O and library/tag modules passed");
}
