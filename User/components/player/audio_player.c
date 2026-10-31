#include "audio_player.h"
#include "mp3_decoder.h"
#include "flac_decoder.h"
#include "i2s.h"
#include "audio_i2s.h"
#include "audio_dma.h"
#include "es9018.h"
#include "FreeRTOS.h"
#include "task.h"
#include "log.h"
#include <string.h>

#define AUDIO_HALF_BYTES 12288U

static FIL s_file;
/* 缓冲字节数固定；24/32 位使用 32 位槽，每半缓冲的帧数减半。 */
static union {
    int16_t pcm16[AUDIO_HALF_BYTES / 2U];
    int32_t pcm32[AUDIO_HALF_BYTES / 4U];
    uint16_t words[AUDIO_HALF_BYTES / 2U];
    uint8_t bytes[AUDIO_HALF_BYTES];
} s_audio[2];
static TaskHandle_t s_player;
static volatile uint32_t s_dma_edges;
static uint32_t s_remaining;
static uint16_t s_channels;
static unsigned s_bits, s_source_bytes;
static uint32_t s_half_frames;
static uint32_t s_valid_frames[2];
static uint32_t s_played_frames, s_sample_rate;
static unsigned s_half;
static uint32_t s_expected_edge;
static bool s_open, s_dma_started;
static music_type_t s_type;
static bool s_eof;
static bool s_wait_lock;
static uint32_t s_lock_frames;
static audio_state_t s_state;

static void audio_notify(uint32_t event)
{
    BaseType_t wake = pdFALSE;
    ++s_dma_edges;
    xTaskNotifyFromISR(s_player, event, eSetBits, &wake);
    portYIELD_FROM_ISR(wake);
}

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
               (s_channels != 1U && s_channels != 2U) ||
               wav_u16(header + 12U) != s_channels * s_source_bytes ||
               !audio_i2s_sample_rate_supported(*sample_rate))
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

static bool audio_fill_half(unsigned half)
{
    memset(&s_audio[half], 0, sizeof(s_audio[half]));
    if(s_type == MUSIC_MP3 || s_type == MUSIC_FLAC) {
        bool result = s_type == MUSIC_MP3 ? mp3_decoder_read(s_audio[half].pcm16, s_half_frames, &s_valid_frames[half]) :
                                           flac_decoder_read(&s_audio[half], s_half_frames, &s_valid_frames[half]);
        if(!result) return false;
        /* SPI 数据寄存器为 16 位：高半字先发，不能直接发送小端 int32_t。 */
        if(s_bits > 16U) {
            for(uint32_t i = 0; i < s_valid_frames[half] * 2U; ++i) {
                uint32_t sample = (uint32_t)s_audio[half].pcm32[i];
                s_audio[half].words[i * 2U] = (uint16_t)(sample >> 16U);
                s_audio[half].words[i * 2U + 1U] = (uint16_t)sample;
            }
        }
        s_eof = s_valid_frames[half] < s_half_frames;
        return true;
    }
    UINT request = s_half_frames * s_channels * s_source_bytes;
    if(request > s_remaining)
        request = (UINT)s_remaining;
    s_valid_frames[half] = request / (s_channels * s_source_bytes);
    if(request == 0U)
        return true;

    UINT read;
    FRESULT result = f_read(&s_file, &s_audio[half], request, &read);
    if(result != FR_OK || read != request)
    {
        LOGE("WAV", "read failed: FatFs=%d, got=%u, want=%u", (int)result, read, request);
        return false;
    }
    s_remaining -= read;
    s_eof = s_remaining == 0;
    if(s_bits > 16U)
    {
        /* 从后向前将紧凑的小端 PCM 展开为左右声道，24 位左对齐且补零。 */
        for(uint32_t frame = s_valid_frames[half]; frame > 0U; --frame) {
            const uint8_t *p = s_audio[half].bytes + (frame - 1U) * s_channels * s_source_bytes;
            uint32_t left = s_source_bytes == 3U ? ((uint32_t)p[0] << 8U) |
                ((uint32_t)p[1] << 16U) | ((uint32_t)p[2] << 24U) : wav_u32(p);
            p += s_source_bytes;
            uint32_t right = s_channels == 1U ? left : s_source_bytes == 3U ?
                ((uint32_t)p[0] << 8U) | ((uint32_t)p[1] << 16U) | ((uint32_t)p[2] << 24U) : wav_u32(p);
            uint16_t *out = s_audio[half].words + (frame - 1U) * 4U;
            out[0] = (uint16_t)(left >> 16U); out[1] = (uint16_t)left;
            out[2] = (uint16_t)(right >> 16U); out[3] = (uint16_t)right;
        }
    }
    else if(s_channels == 1U)
    {
        /* 从后向前原地展开，单声道复制为左右相同的数据。 */
        for(UINT i = read / 2U; i > 0U; --i)
        {
            int16_t sample = s_audio[half].pcm16[i - 1U];
            s_audio[half].pcm16[(i - 1U) * 2U] = sample;
            s_audio[half].pcm16[(i - 1U) * 2U + 1U] = sample;
        }
    }
    return true;
}

void audio_player_stop(void)
{
    if(s_dma_started) {
        audio_dma_set_handler(NULL);
        s_player = NULL;
        HAL_I2S_DMAStop(&hi2s2);
        s_dma_started = false;
    }
    if(s_type == MUSIC_FLAC) flac_decoder_close();
    if(s_open) {
        f_close(&s_file);
        s_open = false;
    }
    s_state = AUDIO_STOPPED;
}

bool audio_player_start(music_info_t *info)
{
    audio_player_stop();
    s_played_frames = s_sample_rate = 0;
    if(!info || (info->type != MUSIC_WAV && info->type != MUSIC_MP3 && info->type != MUSIC_FLAC)) return false;
    s_type = info->type;
    s_eof = false;
    info->sample_rate_hz = info->duration_seconds = info->bits_per_sample = 0;
    FRESULT result = f_open(&s_file, info->path, FA_READ);
    if(result != FR_OK) {
        LOGE("AUDIO", "open failed: FatFs=%d", (int)result);
        goto failed;
    }
    s_open = true;
    if(s_type == MUSIC_MP3) {
        if(!mp3_decoder_open(&s_file, info) || !audio_i2s_sample_rate_supported(info->sample_rate_hz)) goto failed;
        s_sample_rate = info->sample_rate_hz;
    } else if(s_type == MUSIC_FLAC) {
        if(!flac_decoder_open(&s_file, info)) goto failed;
        s_sample_rate = info->sample_rate_hz;
    } else {
        if(!wav_read_header(&s_sample_rate)) {
            LOGW("WAV", "skip: need 16/24/32-bit integer PCM mono/stereo at a supported sample rate");
            goto failed;
        }
        info->sample_rate_hz = s_sample_rate;
        info->bits_per_sample = s_bits;
        info->duration_seconds = s_remaining / (s_channels * s_source_bytes) / s_sample_rate;
        LOGI("WAV", "%lu Hz, %u channel(s), %u-bit, data=%lu bytes",
             (unsigned long)s_sample_rate, (unsigned)s_channels, s_bits, (unsigned long)s_remaining);
    }
    s_bits = info->bits_per_sample;
    s_half_frames = AUDIO_HALF_BYTES / (s_bits == 16U ? 4U : 8U);
    if(audio_i2s_configure(&hi2s2, s_sample_rate, s_bits) != HAL_OK) {
        LOGE("AUDIO", "I2S clock/init failed");
        goto failed;
    }
    /* DAC 的 i2s_length 对应槽宽：STM32 的 24 位模式也使用 32 位槽。 */
    ES9018_set_format(ES9018_FORMAT_I2S, s_bits == 16U ? ES9018_WORD_LENGTH_16 : ES9018_WORD_LENGTH_32);
    /* 先送零数据建立连续 BCLK/LRCLK；锁定前不消耗歌曲开头。 */
    memset(s_audio, 0, sizeof(s_audio));
    s_valid_frames[0] = s_valid_frames[1] = 0;
    s_wait_lock = true;
    s_lock_frames = 0;
    uint32_t events;
    xTaskNotifyWait(0, UINT32_MAX, &events, 0);
    s_dma_edges = 0;
    s_half = 0;
    s_expected_edge = 1;
    s_player = xTaskGetCurrentTaskHandle();
    audio_dma_set_handler(audio_notify);
    /* HAL 的 Size 为声道样本数；24/32 位时 HAL 内部会乘二转为半字数。 */
    if(HAL_I2S_Transmit_DMA(&hi2s2, s_audio[0].words, s_half_frames * 4U) != HAL_OK) {
        LOGE("AUDIO", "I2S DMA start failed");
        goto failed;
    }
    s_dma_started = true;
    s_state = AUDIO_PLAYING;
    return true;
failed:
    audio_dma_set_handler(NULL);
    /* 起播失败统一释放文件和 DMA。 */
    audio_player_stop();
    s_state = AUDIO_ERROR;
    return false;
}

bool audio_player_toggle(void)
{
    HAL_StatusTypeDef result;
    if(s_state == AUDIO_PLAYING) result = HAL_I2S_DMAPause(&hi2s2);
    else if(s_state == AUDIO_PAUSED) result = HAL_I2S_DMAResume(&hi2s2);
    else return false;
    if(result != HAL_OK) {
        LOGE("AUDIO", "I2S pause/resume failed");
        audio_player_stop();
        s_state = AUDIO_ERROR;
        return false;
    }
    s_state = s_state == AUDIO_PLAYING ? AUDIO_PAUSED : AUDIO_PLAYING;
    return true;
}

audio_state_t audio_player_poll(uint32_t timeout_ticks)
{
    uint32_t events = 0;
    if(xTaskNotifyWait(0, UINT32_MAX, &events, timeout_ticks) != pdTRUE) {
        if(timeout_ticks && s_state == AUDIO_PLAYING) {
            LOGE("AUDIO", "DMA timeout");
            goto failed;
        }
        return s_state;
    }
    events &= ~AUDIO_CONTROL_EVENT;
    if(!events || !s_dma_started) return s_state;
    if(events != (1U << s_half) || s_dma_edges != s_expected_edge) {
        LOGE("AUDIO", "DMA timeout, error or refill overrun: events=0x%lX", (unsigned long)events);
        goto failed;
    }
    /* 即使刚暂停，也处理暂停前已完成的半缓冲，保留正确的 DMA 相位。 */
    s_played_frames += s_valid_frames[s_half];
    s_valid_frames[s_half] = 0;
    if(s_wait_lock) {
        s_lock_frames += s_half_frames;
        if(ES9018_is_locked()) {
            s_wait_lock = false;
            LOGI("AUDIO", "DAC locked after %lu ms",
                 (unsigned long)(s_lock_frames * 1000U / s_sample_rate));
        } else if(s_lock_frames >= s_sample_rate) {
            LOGE("AUDIO", "DAC lock timeout");
            goto failed;
        }
    }
    if(s_eof && !s_valid_frames[s_half ^ 1U]) {
        LOGI("AUDIO", "playback complete");
        audio_player_stop();
        s_state = AUDIO_FINISHED;
        return s_state;
    }
    /* 只补写 DMA 已经消费的半缓冲，并检查补数是否赶上下一轮。 */
    TickType_t refill_start = xTaskGetTickCount();
    if(!s_wait_lock && !audio_fill_half(s_half)) goto failed;
    if(s_dma_edges != s_expected_edge) {
        LOGE("AUDIO", "audio refill missed deadline: refill=%lu ms, budget=%lu ms",
             (unsigned long)((xTaskGetTickCount() - refill_start) * 1000U / configTICK_RATE_HZ),
             (unsigned long)(s_half_frames * 1000U / s_sample_rate));
        goto failed;
    }
    s_half ^= 1U;
    ++s_expected_edge;
    return s_state;
failed:
    audio_player_stop();
    s_state = AUDIO_ERROR;
    return s_state;
}

audio_state_t audio_player_state(void) { return s_state; }
uint32_t audio_player_elapsed(void) { return s_sample_rate ? s_played_frames / s_sample_rate : 0; }
