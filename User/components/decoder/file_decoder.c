#include "file_decoder.h"
#include "mp3_decoder.h"
#include "flac_decoder.h"
#include "log.h"
#include <string.h>

static FIL s_file;
static music_type_t s_type;
static bool s_open;
static uint32_t s_remaining;
static uint16_t s_channels;
static unsigned s_bits, s_source_bytes;

static uint16_t wav_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t wav_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static bool wav_read_header(uint32_t *sample_rate)
{
    uint8_t header[40];
    UINT read;
    bool format_found = false;
    if(f_read(&s_file, header, 12U, &read) != FR_OK || read != 12U ||
       memcmp(header, "RIFF", 4U) || memcmp(header + 8U, "WAVE", 4U))
        return false;

    while(f_size(&s_file) - f_tell(&s_file) >= 8U)
    {
        if(f_read(&s_file, header, 8U, &read) != FR_OK || read != 8U)
            return false;
        uint32_t size = wav_u32(header + 4U);
        uint32_t offset = f_tell(&s_file);
        if(size > f_size(&s_file) - offset)
            return false;

        if(memcmp(header, "fmt ", 4U) == 0)
        {
            if(size < 16U || f_read(&s_file, header, 16U, &read) != FR_OK || read != 16U)
                return false;
            s_channels = wav_u16(header + 2U);
            *sample_rate = wav_u32(header + 4U);
            unsigned format = wav_u16(header);
            s_bits = wav_u16(header + 14U);
            s_source_bytes = s_bits / 8U;
            /* 扩展 PCM 校验子格式 GUID；支持常见的 24 位数据放在 32 位容器。 */
            if(format == 0xFFFEU) {
                static const uint8_t pcm_guid[16] = {1,0,0,0,0,0,0x10,0,0x80,0,0,0xAA,0,0x38,0x9B,0x71};
                if(size < 40U || f_read(&s_file, header + 16U, 24U, &read) != FR_OK || read != 24U ||
                   wav_u16(header + 16U) < 22U || memcmp(header + 24U, pcm_guid, 16U)) return false;
                unsigned valid = wav_u16(header + 18U);
                if(valid != s_bits && !(s_bits == 32U && valid == 24U)) return false;
                s_bits = valid;
            } else if(format != 1U) return false;
            if((s_bits != 16U && s_bits != 24U && s_bits != 32U) ||
               (s_channels != 1U && s_channels != 2U) || *sample_rate == 0 ||
               wav_u16(header + 12U) != s_channels * s_source_bytes)
                return false;
            format_found = true;
        }
        else if(memcmp(header, "data", 4U) == 0)
        {
            if(!format_found || size == 0U || size % (s_channels * s_source_bytes) != 0U)
                return false;
            s_remaining = size;
            return true;
        }

        /* 奇数长度的 RIFF 数据块末尾有一个填充字节。 */
        if(size + (size & 1U) > f_size(&s_file) - offset ||
           f_lseek(&s_file, offset + size + (size & 1U)) != FR_OK)
            return false;
    }
    return false;
}

void file_decoder_close(void)
{
    if(s_type == MUSIC_FLAC) flac_decoder_close();
    if(s_open) f_close(&s_file);
    s_open = false;
}

bool file_decoder_open(music_info_t *info)
{
    file_decoder_close();
    if(!info || (info->type != MUSIC_WAV && info->type != MUSIC_MP3 && info->type != MUSIC_FLAC)) return false;
    s_type = info->type;
    info->sample_rate_hz = info->duration_seconds = info->bits_per_sample = 0;
    FRESULT result = f_open(&s_file, info->path, FA_READ);
    if(result != FR_OK) {
        LOGE("AUDIO", "open failed: FatFs=%d", (int)result);
        goto failed;
    }
    s_open = true;
    if(s_type == MUSIC_MP3) {
        if(!mp3_decoder_open(&s_file, info)) goto failed;
    } else if(s_type == MUSIC_FLAC) {
        if(!flac_decoder_open(&s_file, info)) goto failed;
    } else {
        if(!wav_read_header(&info->sample_rate_hz)) {
            LOGW("WAV", "skip: need 16/24/32-bit integer PCM mono/stereo at a supported sample rate");
            goto failed;
        }
        info->bits_per_sample = s_bits;
        info->duration_seconds = s_remaining / (s_channels * s_source_bytes) / info->sample_rate_hz;
        LOGI("WAV", "%lu Hz, %u channel(s), %u-bit, data=%lu bytes",
             (unsigned long)info->sample_rate_hz, (unsigned)s_channels, s_bits, (unsigned long)s_remaining);
    }
    s_bits = info->bits_per_sample;
    return true;
failed:
    file_decoder_close();
    return false;
}

bool file_decoder_read(void *stereo, uint32_t frames, uint32_t *decoded)
{
    if(s_type == MUSIC_MP3) return mp3_decoder_read(stereo, frames, decoded);
    if(s_type == MUSIC_FLAC) return flac_decoder_read(stereo, frames, decoded);
    UINT request = frames * s_channels * s_source_bytes;
    if(request > s_remaining)
        request = (UINT)s_remaining;
    *decoded = request / (s_channels * s_source_bytes);
    if(request == 0U)
        return true;

    UINT read;
    FRESULT result = f_read(&s_file, stereo, request, &read);
    if(result != FR_OK || read != request)
    {
        LOGE("WAV", "read failed: FatFs=%d, got=%u, want=%u", (int)result, read, request);
        return false;
    }
    s_remaining -= read;

    if(s_bits > 16U)
    {
        /* 从后向前将紧凑的小端 PCM 展开为左右声道，24 位左对齐且补零。 */
        for(uint32_t frame = *decoded; frame > 0U; --frame) {
            const uint8_t *p = ((uint8_t *)stereo) + (frame - 1U) * s_channels * s_source_bytes;
            uint32_t left = s_source_bytes == 3U ? ((uint32_t)p[0] << 8U) |
                ((uint32_t)p[1] << 16U) | ((uint32_t)p[2] << 24U) : wav_u32(p);
            p += s_source_bytes;
            uint32_t right = s_channels == 1U ? left : s_source_bytes == 3U ?
                ((uint32_t)p[0] << 8U) | ((uint32_t)p[1] << 16U) | ((uint32_t)p[2] << 24U) : wav_u32(p);
            int32_t *out = (int32_t *)stereo + (frame - 1U) * 2U;
            out[0] = (int32_t)left; out[1] = (int32_t)right;
        }
    }
    else if(s_channels == 1U)
    {
        /* 从后向前原地展开，单声道复制为左右相同的数据。 */
        for(UINT i = read / 2U; i > 0U; --i)
        {
            int16_t sample = ((int16_t *)stereo)[i - 1U];
            ((int16_t *)stereo)[(i - 1U) * 2U] = sample;
            ((int16_t *)stereo)[(i - 1U) * 2U + 1U] = sample;
        }
    }
    return true;
}
