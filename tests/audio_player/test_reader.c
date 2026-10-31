#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "flac_decoder.c"

static FIL file;
static uint8_t input[20037], output[8192];
static uint32_t cache[1024];
static unsigned reads, seeks, fail_read, fail_seek;

TickType_t xTaskGetTickCount(void) { return 0; }
void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
bool audio_i2s_sample_rate_supported(uint32_t rate) { return rate == 48000; }
FRESULT f_lseek(FIL *f, FSIZE_t offset)
{
    assert(f == &file && offset <= sizeof(input));
    ++seeks;
    if(fail_seek) return FR_DISK_ERR;
    f->fptr = offset;
    return FR_OK;
}
FRESULT f_read(FIL *f, void *p, UINT bytes, UINT *read)
{
    assert(f == &file);
    if(s_input) assert(!(f->fptr & 511) && !((uintptr_t)p & 3) && bytes == s_input_capacity);
    ++reads;
    *read = 0;
    if(fail_read) return FR_DISK_ERR;
    if(bytes > sizeof(input) - f->fptr) bytes = sizeof(input) - f->fptr;
    memcpy(p, input + f->fptr, bytes);
    f->fptr += bytes;
    *read = bytes;
    return FR_OK;
}

static void check_read(unsigned position, unsigned size)
{
    memset(output, 0xa5, sizeof(output));
    unsigned expected = size < sizeof(input) - position ? size : sizeof(input) - position;
    assert(flac_read(NULL, output, size) == expected);
    assert(!memcmp(output, input + position, expected));
    assert(output[expected] == 0xa5);
    drflac_int64 logical;
    assert(flac_tell(NULL, &logical) && logical == position + expected);
}

int main(void)
{
    for(unsigned i = 0; i < sizeof(input); ++i) input[i] = (uint8_t)(i * 131U + (i >> 6));
    file.obj.objsize = sizeof(input);
    s_file = &file;
    s_input = (uint8_t *)cache;
    s_input_capacity = sizeof(cache);
    s_position = 1003;
    check_read(1003, 100);
    assert(reads == 1 && file.fptr == 4608); /* 逻辑位置不等于预读后的 FatFs 位置。 */
    check_read(1103, 1024);
    assert(reads == 1);
    assert(flac_seek(NULL, -100, DRFLAC_SEEK_CUR));
    check_read(2027, 0);
    assert(reads == 1);
    check_read(2027, 6000); /* 跨缓存窗口，包含非扇区对齐的请求长度。 */
    assert(reads == 2);
    assert(flac_seek(NULL, 523, DRFLAC_SEEK_SET));
    check_read(523, 17);
    assert(reads == 3);
    assert(flac_seek(NULL, -31, DRFLAC_SEEK_END));
    check_read(sizeof(input) - 31, 128);
    unsigned before = reads;
    check_read(sizeof(input), 10);
    assert(reads == before);
    assert(!flac_seek(NULL, -1, DRFLAC_SEEK_SET));
    assert(!flac_seek(NULL, 1, DRFLAC_SEEK_END));

    assert(flac_seek(NULL, 0, DRFLAC_SEEK_SET));
    fail_read = 1;
    assert(flac_read(NULL, output, 1) == 0 && s_result == FR_DISK_ERR && s_position == 0);
    before = reads;
    assert(flac_read(NULL, output, 1) == 0 && reads == before);
    fail_read = 0; s_result = FR_OK; fail_seek = 1;
    assert(flac_read(NULL, output, 1) == 0 && reads == before && s_result == FR_DISK_ERR);

    fail_seek = 0; s_result = FR_OK;
    flac_decoder_close();
    assert(!s_input && !s_input_capacity && !s_input_size);
    assert(flac_seek(NULL, 777, DRFLAC_SEEK_SET));
    check_read(777, 111); /* 工作区不足时仍可直接读取，位置和错误语义一致。 */
    assert(file.fptr == 888 && seeks > 0);
    puts("PASS: sector-aligned read-ahead, cache hits/crossing, logical seeks/tell, EOF, errors and uncached fallback");
    return 0;
}
