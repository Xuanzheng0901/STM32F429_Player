#include "music_tags.h"
#include <string.h>

enum { TEXT_LATIN1, TEXT_UTF16, TEXT_UTF16BE, TEXT_UTF8, TEXT_AUTO, TEXT_GBK, TEXT_ID3 };

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint32_t sync32(const uint8_t *p)
{
    if((p[0] | p[1] | p[2] | p[3]) & 0x80) return UINT32_MAX;
    return (uint32_t)p[0] << 21 | (uint32_t)p[1] << 14 | (uint32_t)p[2] << 7 | p[3];
}

static FRESULT read_at(FIL *file, uint32_t offset, void *buffer, UINT count)
{
    if(offset > f_size(file) || count > f_size(file) - offset) return FR_INT_ERR;
    FRESULT result = f_lseek(file, offset);
    if(result != FR_OK) return result;
    UINT read = 0;
    result = f_read(file, buffer, count, &read);
    return result == FR_OK && read != count ? FR_INT_ERR : result;
}

static uint32_t utf8_char(const uint8_t *p, unsigned left, unsigned *bytes)
{
    unsigned n = p[0] < 0x80 ? 1 : p[0] >= 0xC2 && p[0] <= 0xDF ? 2 :
                 p[0] >= 0xE0 && p[0] <= 0xEF ? 3 : p[0] >= 0xF0 && p[0] <= 0xF4 ? 4 : 0;
    *bytes = n;
    if(!n) { *bytes = 1; return UINT32_MAX; }
    if(n > left) { *bytes = 0; return UINT32_MAX; }
    uint32_t c = p[0] & (n == 1 ? 0x7F : (1U << (7 - n)) - 1);
    for(unsigned i = 1; i < n; ++i) {
        if((p[i] & 0xC0) != 0x80) return UINT32_MAX;
        c = c << 6 | (p[i] & 0x3F);
    }
    if((n == 2 && c < 0x80) || (n == 3 && c < 0x800) || (n == 4 && c < 0x10000) ||
       (c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF) return UINT32_MAX;
    return c;
}

static FRESULT read_text(FIL *file, uint32_t offset, uint32_t size,
                         char *dest, size_t capacity, unsigned encoding, bool unsync)
{
    /* 只读取文本开头，封面等大字段直接跳过；输出截断在完整 UTF-8 字符边界。 */
    uint8_t buffer[256];
    unsigned length = size < sizeof(buffer) ? (unsigned)size : sizeof(buffer);
    if(!length) return FR_OK;
    FRESULT result = read_at(file, offset, buffer, length);
    if(result != FR_OK) return result;
    if(unsync) {
        unsigned used = 0;
        for(unsigned i = 0; i < length; ++i) {
            buffer[used++] = buffer[i];
            if(buffer[i] == 0xFF && i + 1 < length && buffer[i + 1] == 0) ++i;
        }
        length = used;
    }
    const uint8_t *text = buffer;
    if(encoding == TEXT_ID3) {
        encoding = *text++;
        --length;
        if(encoding > TEXT_UTF8) return FR_OK;
    }
    if((encoding == TEXT_UTF16 || encoding == TEXT_UTF16BE || encoding == TEXT_AUTO) && length >= 2) {
        if(text[0] == 0xFF && text[1] == 0xFE) { encoding = TEXT_UTF16; text += 2; length -= 2; }
        else if(text[0] == 0xFE && text[1] == 0xFF) { encoding = TEXT_UTF16BE; text += 2; length -= 2; }
    }
    if((encoding == TEXT_UTF8 || encoding == TEXT_AUTO) && length >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3)) {
        text += 3; length -= 3;
    }
    if(encoding == TEXT_AUTO) {
        /* WAV INFO 常见 UTF-8；旧版中文软件写入的 ANSI 使用当前 FatFs CP936 转换。 */
        encoding = TEXT_UTF8;
        for(unsigned i = 0; i < length && text[i];) {
            unsigned bytes;
            uint32_t c = utf8_char(text + i, length - i, &bytes);
            if(!bytes) break;
            if(c == UINT32_MAX) { encoding = TEXT_GBK; break; }
            i += bytes;
        }
    }
    char output[sizeof(((music_info_t *)0)->title)];
    size_t used = 0;
    for(unsigned i = 0; i < length;) {
        uint32_t c;
        if(encoding == TEXT_UTF16 || encoding == TEXT_UTF16BE) {
            if(length - i < 2) break;
            c = encoding == TEXT_UTF16 ? text[i] | (uint32_t)text[i + 1] << 8 : (uint32_t)text[i] << 8 | text[i + 1];
            i += 2;
            if(c >= 0xD800 && c <= 0xDBFF) {
                if(length - i < 2) break;
                uint32_t low = encoding == TEXT_UTF16 ? text[i] | (uint32_t)text[i + 1] << 8 : (uint32_t)text[i] << 8 | text[i + 1];
                if(low < 0xDC00 || low > 0xDFFF) return FR_OK;
                c = 0x10000 + ((c - 0xD800) << 10) + low - 0xDC00;
                i += 2;
            } else if(c >= 0xDC00 && c <= 0xDFFF) return FR_OK;
        } else if(encoding == TEXT_UTF8) {
            unsigned bytes;
            c = utf8_char(text + i, length - i, &bytes);
            if(!bytes) break;
            if(c == UINT32_MAX) return FR_OK;
            i += bytes;
        } else {
            c = text[i++];
            if(encoding == TEXT_GBK && c >= 0x80) {
                if(i == length) break;
                c = ff_oem2uni((WCHAR)(c << 8 | text[i++]), FF_CODE_PAGE);
                if(!c) return FR_OK;
            }
        }
        if(!c) break; /* 多值标签只取第一项，不拼接重复内容。 */
        unsigned bytes = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
        if(used + bytes >= capacity || used + bytes >= sizeof(output)) break;
        if(bytes == 1) output[used++] = (char)c;
        else {
            output[used++] = (char)((bytes == 2 ? 0xC0 : bytes == 3 ? 0xE0 : 0xF0) | (c >> (6 * (bytes - 1))));
            for(unsigned j = bytes - 1; j > 0; --j) output[used++] = (char)(0x80 | ((c >> (6 * (j - 1))) & 0x3F));
        }
    }
    while(used && output[used - 1] == ' ') --used;
    if(used) { output[used] = 0; memcpy(dest, output, used + 1); }
    return FR_OK;
}

static FRESULT read_id3(FIL *file, uint32_t start, uint32_t limit, music_info_t *info)
{
    uint8_t header[10];
    if(limit - start < sizeof(header)) return FR_OK;
    FRESULT result = read_at(file, start, header, sizeof(header));
    if(result != FR_OK || memcmp(header, "ID3", 3)) return result;
    unsigned version = header[3];
    uint32_t size = sync32(header + 6);
    if(version < 2 || version > 4 || size > limit - start - 10) return FR_OK;
    /* v2.2 压缩、v2.3 全标签 unsync 不支持，保留文件名或 ID3v1 回退。 */
    if((version == 2 && (header[5] & 0xC0)) || (version == 3 && (header[5] & 0x80))) return FR_OK;
    uint32_t position = start + 10, end = position + size;
    bool unsync = version == 4 && (header[5] & 0x80);
    if(version >= 3 && (header[5] & 0x40)) {
        if(end - position < 4) return FR_OK;
        result = read_at(file, position, header, 4);
        if(result != FR_OK) return result;
        size = version == 4 ? sync32(header) : be32(header);
        if(version == 3) { if(size > UINT32_MAX - 4) return FR_OK; size += 4; }
        if(size < 4 || size > end - position) return FR_OK;
        position += size;
    }
    unsigned header_size = version == 2 ? 6 : 10;
    while(end - position >= header_size) {
        result = read_at(file, position, header, header_size);
        if(result != FR_OK) return result;
        if(!header[0]) break;
        for(unsigned i = 0; i < (version == 2 ? 3U : 4U); ++i)
            if(!((header[i] >= 'A' && header[i] <= 'Z') || (header[i] >= '0' && header[i] <= '9'))) return FR_OK;
        size = version == 2 ? (uint32_t)header[3] << 16 | (uint32_t)header[4] << 8 | header[5] :
               version == 4 ? sync32(header + 4) : be32(header + 4);
        position += header_size;
        if(!size || size > end - position) break;
        char *dest = NULL;
        size_t capacity = 0;
        if(!memcmp(header, version == 2 ? "TT2" : "TIT2", version == 2 ? 3 : 4)) { dest = info->title; capacity = sizeof(info->title); }
        else if(!memcmp(header, version == 2 ? "TP1" : "TPE1", version == 2 ? 3 : 4)) { dest = info->artist; capacity = sizeof(info->artist); }
        else if(!memcmp(header, version == 2 ? "TAL" : "TALB", version == 2 ? 3 : 4)) { dest = info->album; capacity = sizeof(info->album); }
        uint8_t flags = version == 2 ? 0 : header[9];
        /* 跳过压缩、加密、分组和带长度前缀的帧，仅支持普通文本及 v2.4 帧 unsync。 */
        bool supported = version == 3 ? flags == 0 : (flags & ~2U) == 0;
        if(dest && supported) {
            result = read_text(file, position, size, dest, capacity, TEXT_ID3, unsync || (flags & 2));
            if(result != FR_OK) return result;
        }
        position += size;
    }
    return FR_OK;
}

static FRESULT read_wave(FIL *file, music_info_t *info)
{
    uint8_t header[12];
    FRESULT result = read_at(file, 0, header, sizeof(header));
    if(result != FR_OK || memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVE", 4)) return result;
    uint32_t size = le32(header + 4);
    if(size < 4 || size > f_size(file) - 8) return FR_OK;
    uint32_t position = 12, end = size + 8;
    while(end - position >= 8) {
        result = read_at(file, position, header, 8);
        if(result != FR_OK) return result;
        size = le32(header + 4);
        position += 8;
        if(size > end - position) break;
        uint32_t next = position + size;
        if(!memcmp(header, "LIST", 4) && size >= 4) {
            result = read_at(file, position, header, 4);
            if(result != FR_OK) return result;
            if(!memcmp(header, "INFO", 4)) {
                uint32_t item = position + 4;
                while(next - item >= 8) {
                    result = read_at(file, item, header, 8);
                    if(result != FR_OK) return result;
                    uint32_t bytes = le32(header + 4);
                    item += 8;
                    if(bytes > next - item) break;
                    char *dest = NULL;
                    size_t capacity = 0;
                    if(!memcmp(header, "INAM", 4)) { dest = info->title; capacity = sizeof(info->title); }
                    else if(!memcmp(header, "IART", 4)) { dest = info->artist; capacity = sizeof(info->artist); }
                    else if(!memcmp(header, "IPRD", 4)) { dest = info->album; capacity = sizeof(info->album); }
                    if(dest) {
                        result = read_text(file, item, bytes, dest, capacity, TEXT_AUTO, false);
                        if(result != FR_OK) return result;
                    }
                    item += bytes;
                    if(bytes & 1) { if(item == next) break; ++item; }
                }
            }
        } else if(!memcmp(header, "id3 ", 4) || !memcmp(header, "ID3 ", 4)) {
            result = read_id3(file, position, next, info);
            if(result != FR_OK) return result;
        }
        position = next;
        if(size & 1) { if(position == end) break; ++position; }
    }
    return FR_OK;
}

static FRESULT read_flac(FIL *file, music_info_t *info)
{
    uint8_t header[16];
    if(f_size(file) < 8) return FR_OK;
    FRESULT result = read_at(file, 0, header, 4);
    if(result != FR_OK || memcmp(header, "fLaC", 4)) return result;
    uint32_t position = 4;
    while(f_size(file) - position >= 4) {
        result = read_at(file, position, header, 4);
        if(result != FR_OK) return result;
        uint8_t type = header[0];
        uint32_t size = (uint32_t)header[1] << 16 | (uint32_t)header[2] << 8 | header[3];
        position += 4;
        if(size > f_size(file) - position) break;
        uint32_t end = position + size;
        if((type & 0x7F) == 4 && size >= 8) {
            /* Vorbis Comment 的长度为小端，字段名不区分大小写，值为 UTF-8。 */
            result = read_at(file, position, header, 4);
            if(result != FR_OK) return result;
            uint32_t vendor = le32(header);
            position += 4;
            if(vendor > end - position || end - position - vendor < 4) break;
            position += vendor;
            result = read_at(file, position, header, 4);
            if(result != FR_OK) return result;
            uint32_t count = le32(header);
            position += 4;
            for(uint32_t i = 0; i < count && end - position >= 4; ++i) {
                result = read_at(file, position, header, 4);
                if(result != FR_OK) return result;
                uint32_t bytes = le32(header);
                position += 4;
                if(bytes > end - position) break;
                unsigned prefix = bytes < sizeof(header) ? (unsigned)bytes : sizeof(header);
                result = read_at(file, position, header, prefix);
                if(result != FR_OK) return result;
                for(unsigned j = 0; j < prefix; ++j)
                    if(header[j] >= 'a' && header[j] <= 'z') header[j] -= 'a' - 'A';
                char *dest = NULL;
                size_t capacity = 0;
                unsigned key = 0;
                if(prefix >= 6 && !memcmp(header, "TITLE=", 6)) { dest = info->title; capacity = sizeof(info->title); key = 6; }
                else if(prefix >= 7 && !memcmp(header, "ARTIST=", 7)) { dest = info->artist; capacity = sizeof(info->artist); key = 7; }
                else if(prefix >= 6 && !memcmp(header, "ALBUM=", 6)) { dest = info->album; capacity = sizeof(info->album); key = 6; }
                if(dest) {
                    result = read_text(file, position + key, bytes - key, dest, capacity, TEXT_UTF8, false);
                    if(result != FR_OK) return result;
                }
                position += bytes;
            }
        }
        position = end;
        if(type & 0x80) break;
    }
    return FR_OK;
}

FRESULT music_tags_read(music_info_t *info)
{
    if(!info) return FR_INVALID_PARAMETER;
    if(info->type != MUSIC_MP3 && info->type != MUSIC_WAV && info->type != MUSIC_FLAC) return FR_OK;
    FIL file;
    FRESULT result = f_open(&file, info->path, FA_READ);
    if(result != FR_OK) return result;
    if(info->type == MUSIC_MP3) {
        /* 先读旧版标签，再由 ID3v2 中非空的对应字段覆盖。 */
        if(f_size(&file) >= 128) {
            uint8_t tag[128];
            result = read_at(&file, f_size(&file) - 128, tag, sizeof(tag));
            if(result == FR_OK && !memcmp(tag, "TAG", 3)) {
                const uint32_t offset = f_size(&file) - 125;
                result = read_text(&file, offset, 30, info->title, sizeof(info->title), TEXT_LATIN1, false);
                if(result == FR_OK) result = read_text(&file, offset + 30, 30, info->artist, sizeof(info->artist), TEXT_LATIN1, false);
                if(result == FR_OK) result = read_text(&file, offset + 60, 30, info->album, sizeof(info->album), TEXT_LATIN1, false);
            }
        }
        if(result == FR_OK) result = read_id3(&file, 0, f_size(&file), info);
    } else if(info->type == MUSIC_FLAC) result = read_flac(&file, info);
    else if(f_size(&file) >= 12) result = read_wave(&file, info);
    FRESULT closed = f_close(&file);
    /* 标签损坏或截断不是音频读卡失败，仍允许按文件名显示和选择播放。 */
    if(result == FR_INT_ERR) result = FR_OK;
    return result == FR_OK ? closed : result;
}
