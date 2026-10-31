#include "audio_player.h"
#include "file_decoder.h"
#include "i2s.h"
#include "audio_i2s.h"
#include "audio_dma.h"
#include "es9018.h"
#include "FreeRTOS.h"
#include "task.h"
#include "log.h"
#include <string.h>

#define AUDIO_HALF_BYTES 24576U

/* 每半缓冲 24 KiB：96 kHz 宽格式可用 3072 帧/32 ms，吸收整块 FLAC 解码峰值。 */
static union {
    int16_t pcm16[AUDIO_HALF_BYTES / 2U];
    int32_t pcm32[AUDIO_HALF_BYTES / 4U];
    uint16_t words[AUDIO_HALF_BYTES / 2U];
    uint8_t bytes[AUDIO_HALF_BYTES];
} s_audio[2];
static TaskHandle_t s_player;
static volatile uint32_t s_dma_edges;
static unsigned s_bits;
static uint32_t s_half_frames;
static uint32_t s_valid_frames[2];
static uint32_t s_played_frames, s_sample_rate;
static unsigned s_half;
static uint32_t s_expected_edge;
static bool s_dma_started;
static bool s_eof;
static TickType_t s_decode_ticks;
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

static bool audio_fill_half(unsigned half)
{
    memset(&s_audio[half], 0, sizeof(s_audio[half]));
    TickType_t started = xTaskGetTickCount();
    if(!file_decoder_read(&s_audio[half], s_half_frames, &s_valid_frames[half])) return false;
    s_decode_ticks = xTaskGetTickCount() - started;
    /* I2S 数据寄存器为 16 位，先发送高半字。 */
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

void audio_player_stop(void)
{
    if(s_dma_started) {
        audio_dma_set_handler(NULL);
        s_player = NULL;
        HAL_I2S_DMAStop(&hi2s2);
        s_dma_started = false;
    }
    file_decoder_close();
    s_state = AUDIO_STOPPED;
}

bool audio_player_start(music_info_t *info)
{
    audio_player_stop();
    s_played_frames = s_sample_rate = 0;
    s_eof = false;
    if(!file_decoder_open(info)) goto failed;
    s_sample_rate = info->sample_rate_hz;
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
    s_decode_ticks = 0;
    if(!s_wait_lock && !audio_fill_half(s_half)) goto failed;
    if(s_dma_edges != s_expected_edge) {
        LOGE("AUDIO", "audio refill missed deadline: refill=%lu ms, decode/read=%lu ms, budget=%lu ms",
             (unsigned long)((xTaskGetTickCount() - refill_start) * 1000U / configTICK_RATE_HZ),
             (unsigned long)(s_decode_ticks * 1000U / configTICK_RATE_HZ),
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
