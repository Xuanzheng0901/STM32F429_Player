#include "flac_decoder.h"
#include "decoder_memory.h"
#include "log.h"
#include "FreeRTOS.h"
#include "task.h"
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_SIMD
#define DR_FLAC_NO_PICTURE_METADATA_MALLOC
#define DR_FLAC_BUFFER_SIZE 1024
/* 在 Cortex-M4 上用紧凑乘加循环，避免每个样本反复判断 LPC 阶数。 */
#define DR_FLAC_PREDICTION_LOOP
/* 32 位立体声的差分声道可能有 33 位，额外一位按位保存。 */
static uint8_t *s_side_bits;
#define DR_FLAC_32BIT_SIDE_BITS s_side_bits
#define DR_FLAC_32BIT_MAX_FRAMES 4608U
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac/dr_flac.h"

static drflac *s_decoder;
static FIL *s_file;
static FRESULT s_result;
static uint64_t s_decoded;
static TickType_t s_read_ticks;
static uint8_t *s_input;
static uint32_t s_position, s_input_start, s_input_size, s_input_capacity;
static uint32_t s_read_calls, s_read_bytes;

static size_t flac_read(void *user, void *output, size_t bytes)
{
    (void)user;
    if(s_result != FR_OK || bytes > UINT32_MAX || s_position >= f_size(s_file)) return 0;
    size_t total = 0;
    while(bytes) {
        UINT read = 0;
        if(!s_input || s_position < s_input_start || s_position - s_input_start >= s_input_size) {
            TickType_t start = xTaskGetTickCount();
            /* 从扇区边界预读到对齐 SRAM，避免非对齐小读退化为逐扇区暂存。 */
            s_input_start = s_position & ~511U;
            s_input_size = 0;
            if(s_input) s_result = f_lseek(s_file, s_input_start);
            if(s_result == FR_OK)
                s_result = f_read(s_file, s_input ? s_input : output,
                                  s_input ? s_input_capacity : (UINT)bytes, &read);
            s_read_ticks += xTaskGetTickCount() - start;
            ++s_read_calls;
            s_read_bytes += read;
            if(s_result != FR_OK) return 0;
            if(!s_input) { s_position += read; return read; }
            s_input_size = read;
            if(s_position - s_input_start >= read) break;
        }
        size_t available = s_input_size - (s_position - s_input_start);
        if(available > bytes) available = bytes;
        memcpy((uint8_t *)output + total, s_input + s_position - s_input_start, available);
        s_position += (uint32_t)available;
        total += available;
        bytes -= available;
    }
    return total;
}

static drflac_bool32 flac_seek(void *user, int offset, drflac_seek_origin origin)
{
    (void)user;
    if(s_result != FR_OK) return DRFLAC_FALSE;
    int64_t position = (origin == DRFLAC_SEEK_SET ? 0 : origin == DRFLAC_SEEK_END ?
                        (int64_t)f_size(s_file) : (int64_t)s_position) + offset;
    if(position < 0 || (uint64_t)position > f_size(s_file)) return DRFLAC_FALSE;
    if(!s_input) s_result = f_lseek(s_file, (DWORD)position);
    if(s_result == FR_OK) s_position = (uint32_t)position;
    return s_result == FR_OK;
}

static drflac_bool32 flac_tell(void *user, drflac_int64 *position)
{
    (void)user;
    *position = s_position;
    return s_result == FR_OK;
}

static void *flac_alloc(size_t size, void *user)
{
    (void)user;
    void *memory = decoder_memory_alloc(size);
    if(!memory) LOGW("FLAC", "decoder workspace exhausted: need=%lu bytes", (unsigned long)size);
    return memory;
}

static void flac_free(void *memory, void *user) { (void)memory; (void)user; }

void flac_decoder_close(void)
{
    if(s_decoder) drflac_close(s_decoder);
    s_decoder = NULL;
    s_side_bits = NULL;
    s_input = NULL;
    s_input_size = s_input_capacity = 0;
}

bool flac_decoder_open(FIL *file, music_info_t *info)
{
    flac_decoder_close();
    decoder_memory_reset();
    s_file = file;
    s_result = FR_OK;
    s_decoded = 0;
    s_position = 0;
    /* 先检查 STREAMINFO，避免多声道或超大分块申请工作区。 */
    uint8_t header[42];
    UINT read;
    s_result = f_read(file, header, sizeof(header), &read);
    if(s_result != FR_OK || read != sizeof(header) ||
       memcmp(header, "fLaC", 4) || (header[4] & 0x7F) != 0 ||
       header[5] || header[6] || header[7] != 34) goto failed;
    uint64_t stream = 0;
    for(unsigned i = 18; i < 26; ++i) stream = stream << 8 | header[i];
    unsigned channels = (unsigned)((stream >> 41) & 7) + 1;
    unsigned bits = (unsigned)((stream >> 36) & 31) + 1;
    uint32_t rate = (uint32_t)(stream >> 44);
    if(channels > 2 || (bits != 16 && bits != 24 && bits != 32) || rate == 0) {
        LOGW("FLAC", "unsupported stream: %lu Hz, %u channels, %u bits", (unsigned long)rate, channels, bits);
        goto failed;
    }
    if(bits == 32) {
        unsigned block = ((unsigned)header[10] << 8) | header[11];
        if(!block || block > DR_FLAC_32BIT_MAX_FRAMES) goto failed;
        s_side_bits = decoder_memory_alloc((block + 7U) / 8U);
        if(!s_side_bits) goto failed;
    }
    s_result = f_lseek(file, 0);
    if(s_result != FR_OK) goto failed;
    const drflac_allocation_callbacks allocation = {NULL, flac_alloc, NULL, flac_free};
    s_decoder = drflac_open(flac_read, flac_seek, flac_tell, NULL, &allocation);
    if(!s_decoder || s_result != FR_OK) goto failed;
    /* 使用解码工作区剩余空间，不新增静态 RAM 或占用 CCM 任务堆。 */
    for(uint32_t capacity = 4096; capacity >= 512; capacity /= 2) {
        s_input = decoder_memory_alloc(capacity);
        if(s_input) { s_input_capacity = capacity; break; }
    }
    info->sample_rate_hz = s_decoder->sampleRate;
    info->duration_seconds = (uint32_t)(s_decoder->totalPCMFrameCount / s_decoder->sampleRate);
    info->bits_per_sample = bits;
    LOGI("FLAC", "%lu Hz, %u channel(s), %u-bit PCM, block=%u, read cache=%lu bytes",
         (unsigned long)rate, channels, bits, s_decoder->maxBlockSizeInPCMFrames, (unsigned long)s_input_capacity);
    return true;
failed:
    LOGW("FLAC", "open failed: invalid/unsupported stream or workspace limit, FatFs=%d", (int)s_result);
    flac_decoder_close();
    return false;
}

bool flac_decoder_read(void *stereo, uint32_t frames, uint32_t *decoded)
{
    TickType_t start = xTaskGetTickCount();
    s_read_ticks = 0;
    s_read_calls = s_read_bytes = 0;
    bool wide = s_decoder->bitsPerSample > 16;
    *decoded = 0;
    if(wide) *decoded = (uint32_t)drflac_read_pcm_frames_s32(s_decoder, frames, stereo);
    else {
        /* 复用同一条 32 位解码路径，16 位文件只移除库补齐的低 16 个零位。 */
        drflac_int32 pcm[64];
        while(*decoded < frames) {
            uint32_t count = frames - *decoded;
            if(count > 32) count = 32;
            uint32_t read = (uint32_t)drflac_read_pcm_frames_s32(s_decoder, count, pcm);
            for(uint32_t i = 0; i < read * s_decoder->channels; ++i)
                ((int16_t *)stereo)[*decoded * s_decoder->channels + i] = (int16_t)(pcm[i] >> 16);
            *decoded += read;
            if(read < count) break;
        }
    }
    s_decoded += *decoded;
    if(s_result != FR_OK || (*decoded < frames && s_decoder->totalPCMFrameCount &&
                            s_decoded != s_decoder->totalPCMFrameCount)) {
        LOGE("FLAC", "read failed or truncated/corrupt stream: FatFs=%d", (int)s_result);
        return false;
    }
    if(s_decoder->channels == 1) {
        for(uint32_t i = *decoded; i > 0; --i) {
            if(wide) {
                int32_t *pcm = stereo;
                pcm[(i - 1) * 2] = pcm[(i - 1) * 2 + 1] = pcm[i - 1];
            } else {
                int16_t *pcm = stereo;
                pcm[(i - 1) * 2] = pcm[(i - 1) * 2 + 1] = pcm[i - 1];
            }
        }
    }
    TickType_t elapsed = xTaskGetTickCount() - start;
    if((uint64_t)elapsed * s_decoder->sampleRate > (uint64_t)frames * configTICK_RATE_HZ)
        LOGW("FLAC", "refill=%lu ms, file read=%lu ms, budget=%lu ms, reads=%lu bytes=%lu",
             (unsigned long)(elapsed * 1000U / configTICK_RATE_HZ),
             (unsigned long)(s_read_ticks * 1000U / configTICK_RATE_HZ),
             (unsigned long)(frames * 1000U / s_decoder->sampleRate),
             (unsigned long)s_read_calls, (unsigned long)s_read_bytes);
    return true;
}
