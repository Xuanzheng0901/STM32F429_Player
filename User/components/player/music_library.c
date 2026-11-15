#include "music_library.h"
#include "music_tags.h"
#include "log.h"
#include <string.h>

#define PATH_CAPACITY MUSIC_PATH_CAPACITY
#define SCAN_MAX_DEPTH 4U /* 根目录 + 最多四层子目录，限制栈及文件锁占用。 */

static const char s_index_path[] = "0:/music.idx";
static FIL s_index;
static bool s_index_open;

void music_library_close(void)
{
    if(s_index_open) f_close(&s_index);
    s_index_open = false;
}

static bool ends_with(const TCHAR *path, const char *suffix)
{
    size_t length = strlen(path), count = strlen(suffix);
    if(length < count) return false;
    for(size_t i = 0; i < count; ++i) {
        TCHAR c = path[length - count + i];
        if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if(c != (TCHAR)suffix[i]) return false;
    }
    return true;
}

static music_type_t music_type(const TCHAR *path)
{
    if(ends_with(path, ".wav")) return MUSIC_WAV;
    if(ends_with(path, ".mp3")) return MUSIC_MP3;
    if(ends_with(path, ".flac")) return MUSIC_FLAC;
    return MUSIC_UNKNOWN;
}

static void make_title(music_info_t *item);

/* 插卡建库时读取标签，路径与显示信息一起写索引；播放中不再打开曲目解析标签。 */
static FRESULT scan_directory(FIL *index, music_info_t *item, unsigned depth, uint32_t *count)
{
    TCHAR *path = item->path;
    DIR directory;
    FILINFO info;
    FRESULT result = f_opendir(&directory, path);
    if(result != FR_OK) {
        LOGE("SD", "open directory failed: depth=%u FatFs=%d", depth, (int)result);
        return result;
    }

    size_t parent_length = strlen(path);
    while(1) {
        result = f_readdir(&directory, &info);
        if(result != FR_OK) {
            LOGE("SD", "read directory failed: depth=%u FatFs=%d", depth, (int)result);
            break;
        }
        if(!info.fname[0]) break;
        /* 排除 ._ 资源文件、隐藏/系统目录与回收站中的副本。 */
        if(info.fname[0] == '.' || (info.fattrib & (AM_HID | AM_SYS))) continue;
        bool is_directory = (info.fattrib & AM_DIR) != 0;
        if(!is_directory && music_type(info.fname) == MUSIC_UNKNOWN) continue;
        size_t name_length = strlen(info.fname);
        size_t start = parent_length;
        if(path[start - 1] != '/') ++start;
        if(start + name_length >= PATH_CAPACITY || (is_directory && depth == SCAN_MAX_DEPTH)) {
            LOGD("SD", "skip: path or directory depth limit");
            continue;
        }
        if(start != parent_length) path[parent_length] = '/';
        memcpy(path + start, info.fname, (name_length + 1) * sizeof(TCHAR));
        if(is_directory) {
            result = scan_directory(index, item, depth + 1, count);
        } else {
            memset(path + start + name_length + 1, 0,
                   (PATH_CAPACITY - start - name_length - 1) * sizeof(TCHAR));
            memset(item->title, 0, sizeof(item->title));
            memset(item->artist, 0, sizeof(item->artist));
            memset(item->album, 0, sizeof(item->album));
            item->type = music_type(path);
            item->sample_rate_hz = item->duration_seconds = item->bits_per_sample = 0;
            make_title(item);
            FRESULT tags = music_tags_read(item);
            if(tags == FR_DISK_ERR || tags == FR_NOT_READY || tags == FR_TIMEOUT) {
                result = tags;
                break;
            }
            UINT written = 0;
            if(*count >= UINT32_MAX / sizeof(*item)) result = FR_DENIED;
            else {
                result = f_write(index, item, sizeof(*item), &written);
                if(result == FR_OK && written != sizeof(*item)) result = FR_DISK_ERR;
                if(result != FR_OK)
                    LOGE("SD", "write index record %lu failed: FatFs=%d bytes=%u",
                         (unsigned long)*count, (int)result, written);
                if(result == FR_OK) ++*count;
            }
        }
        path[parent_length] = 0;
        if(result != FR_OK) break;
    }
    FRESULT closed = f_closedir(&directory);
    return result == FR_OK ? closed : result;
}

FRESULT music_library_rebuild(uint32_t *total)
{
    music_library_close();
    music_info_t item;
    memset(&item, 0, sizeof(item));
    strcpy(item.path, "0:/");
    FIL index;
    uint32_t count = 0;
    *total = 0;
    FRESULT result = f_open(&index, s_index_path, FA_CREATE_ALWAYS | FA_WRITE);
    if(result != FR_OK) return result;
    result = scan_directory(&index, &item, 0, &count);
    FRESULT closed = f_close(&index);
    if(result == FR_OK) result = closed;
    if(result == FR_OK) result = f_chmod(s_index_path, AM_HID, AM_HID);
    if(result == FR_OK) {
        result = f_open(&s_index, s_index_path, FA_READ);
        s_index_open = result == FR_OK;
    }
    if(result == FR_OK) *total = count;
    return result;
}

/* UTF-8 截断只保留完整字符。 */
static void name_utf8(const char *name, size_t end, char *text, size_t capacity)
{
    size_t count = end < capacity ? end : capacity - 1;
    while(count && ((unsigned char)name[count] & 0xC0) == 0x80) --count;
    memcpy(text, name, count);
    text[count] = 0;
}

static void make_title(music_info_t *item)
{
    const TCHAR *name = item->path;
    for(const TCHAR *p = name; *p; ++p) if(*p == '/') name = p + 1;
    size_t end = strlen(name);
    for(size_t i = end; i > 0; --i) if(name[i - 1] == '.') { end = i - 1; break; }
    /* 无标签时按“音乐名 - 艺术家”回退，优先使用最后一个带空格的分隔符。 */
    size_t split = end;
    for(size_t i = 0; i < end; ++i) {
        if(name[i] != '-') continue;
        if(split == end) split = i;
        if(i && i + 1 < end && name[i - 1] == ' ' && name[i + 1] == ' ') {
            split = i;
        }
    }
    size_t title_end = split, artist_start = split < end ? split + 1 : end;
    while(title_end && name[title_end - 1] == ' ') --title_end;
    while(artist_start < end && name[artist_start] == ' ') ++artist_start;
    while(end > artist_start && name[end - 1] == ' ') --end;
    item->artist[0] = 0;
    if(!title_end || artist_start == end) title_end = end;
    else name_utf8(name + artist_start, end - artist_start, item->artist, sizeof(item->artist));
    name_utf8(name, title_end, item->title, sizeof(item->title));
}

FRESULT music_library_load(uint32_t total, uint32_t center, music_window_t *window)
{
    if(!window) return FR_INVALID_PARAMETER;
    window->count = 0;
    window->first_index = 0;
    window->total = 0;
    if(!total) return center == 0 ? FR_OK : FR_INVALID_PARAMETER;
    if(center >= total) return FR_INVALID_PARAMETER;

    uint32_t first = center > MUSIC_WINDOW_COUNT / 2 ? center - MUSIC_WINDOW_COUNT / 2 : 0;
    uint32_t last_first = total > MUSIC_WINDOW_COUNT ? total - MUSIC_WINDOW_COUNT : 0;
    if(first > last_first) first = last_first;
    UINT count = total - first > MUSIC_WINDOW_COUNT ? MUSIC_WINDOW_COUNT : total - first;
    /* 建库后保持索引打开，翻页不再查目录或打开音乐文件。 */
    if(!s_index_open) {
        FRESULT opened = f_open(&s_index, s_index_path, FA_READ);
        if(opened != FR_OK) return opened;
        s_index_open = true;
    }
    FRESULT result = f_lseek(&s_index, first * sizeof(music_info_t));
    for(UINT i = 0; result == FR_OK && i < count; ++i) {
        music_info_t *item = &window->items[i];
        UINT read;
        /* 每次只读一条记录，及时释放卷锁，让高优先级音频补数插入。 */
        result = f_read(&s_index, item, sizeof(*item), &read);
        if(result != FR_OK) break;
        if(read != sizeof(*item) || item->path[PATH_CAPACITY - 1] != 0 ||
           item->title[sizeof(item->title) - 1] != 0 ||
           item->artist[sizeof(item->artist) - 1] != 0 ||
           item->album[sizeof(item->album) - 1] != 0 ||
           item->type == MUSIC_UNKNOWN || item->type != music_type(item->path)) result = FR_INT_ERR;
    }
    if(result != FR_OK) music_library_close();
    if(result == FR_OK) {
        window->first_index = first;
        window->total = total;
        window->count = count;
    }
    return result;
}
