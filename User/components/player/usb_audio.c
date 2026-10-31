#include "usb_audio.h"
#include "audio_dma.h"
#include "audio_i2s.h"
#include "i2s.h"
#include "es9018.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#define FIFO_FRAMES 2048U
#define HALF_FRAMES 192U
#define PREFILL_FRAMES (HALF_FRAMES * 2U)
#define HOST_TIMEOUT_MS 500U

/* 一帧为左右两个 int16_t，全部位于 DMA 可访问的普通 SRAM。 */
static uint32_t s_fifo[FIFO_FRAMES];
static uint32_t s_dma[2][HALF_FRAMES];
static volatile uint32_t s_write, s_read, s_last_host, s_last_packet;
static volatile bool s_host_seen, s_stream, s_muted;
static bool s_running, s_locked, s_primed;
static TaskHandle_t s_task;
static volatile uint32_t s_edges;
static uint32_t s_expected, s_lock_start;
static unsigned s_half;

void usb_audio_host_activity(void)
{
    s_last_host = HAL_GetTick();
    s_host_seen = true;
}

void usb_audio_set_stream(bool enabled)
{
    s_stream = false;
    s_write = s_read = 0;
    s_stream = enabled;
}

void usb_audio_disconnect(void)
{
    s_host_seen = false;
    usb_audio_set_stream(false);
}

void usb_audio_set_mute(bool muted) { s_muted = muted; }

bool usb_audio_connected(void)
{
    return s_host_seen && (uint32_t)(HAL_GetTick() - s_last_host) < HOST_TIMEOUT_MS;
}

void usb_audio_receive(const uint8_t *data, uint32_t bytes)
{
    if(!s_stream || !bytes || (bytes & 3U)) return;
    uint32_t frames = bytes / 4U;
    uint32_t write = s_write;
    if(frames > FIFO_FRAMES - (write - s_read)) return;
    for(uint32_t i = 0; i < frames; ++i)
        memcpy(&s_fifo[(write + i) & (FIFO_FRAMES - 1U)], data + i * 4U, 4U);
    s_write = write + frames;
    s_last_packet = HAL_GetTick();
}

static void dma_event(uint32_t event)
{
    BaseType_t wake = pdFALSE;
    ++s_edges;
    xTaskNotifyFromISR(s_task, event, eSetBits, &wake);
    portYIELD_FROM_ISR(wake);
}

void usb_audio_stop(void)
{
    if(!s_running) return;
    audio_dma_set_handler(NULL);
    HAL_I2S_DMAStop(&hi2s2);
    s_running = s_locked = s_primed = false;
}

bool usb_audio_start(void)
{
    if(s_running) return true;
    if(audio_i2s_configure(&hi2s2, 48000U, 16U) != HAL_OK) return false;
    ES9018_set_format(ES9018_FORMAT_I2S, ES9018_WORD_LENGTH_16);
    memset(s_dma, 0, sizeof(s_dma));
    uint32_t events;
    xTaskNotifyWait(0, UINT32_MAX, &events, 0);
    s_task = xTaskGetCurrentTaskHandle();
    s_edges = 0;
    s_expected = 1;
    s_half = 0;
    s_locked = s_primed = false;
    s_lock_start = HAL_GetTick();
    audio_dma_set_handler(dma_event);
    if(HAL_I2S_Transmit_DMA(&hi2s2, (uint16_t *)s_dma, HALF_FRAMES * 4U) != HAL_OK) {
        audio_dma_set_handler(NULL);
        return false;
    }
    s_running = true;
    return true;
}

void usb_audio_poll(uint32_t timeout_ticks)
{
    uint32_t events = 0;
    if(xTaskNotifyWait(0, UINT32_MAX, &events, timeout_ticks) != pdTRUE) {
        if(timeout_ticks) usb_audio_stop();
        return;
    }
    events &= ~AUDIO_CONTROL_EVENT;
    if(!events || !s_running) return;
    if(events != (1U << s_half) || s_edges != s_expected) {
        usb_audio_stop();
        return;
    }
    memset(s_dma[s_half], 0, sizeof(s_dma[s_half]));
    if(!s_locked) s_locked = ES9018_is_locked();
    if(!s_locked && (uint32_t)(HAL_GetTick() - s_lock_start) >= 1000U) {
        usb_audio_stop();
        return;
    }
    /* 积压时把 193 帧平滑压成 192 帧，补偿时钟偏差，避免直接跳帧产生咔声。 */
    taskENTER_CRITICAL();
    uint32_t available = s_write - s_read;
    if(!s_stream || !s_locked || available < HALF_FRAMES) s_primed = false;
    if(s_stream && s_locked && available >= PREFILL_FRAMES) s_primed = true;
    if(s_primed) {
        bool adjust = available > HALF_FRAMES * 6U;
        for(unsigned i = 0; i < HALF_FRAMES; ++i) {
            uint32_t sample = s_fifo[(s_read + i) & (FIFO_FRAMES - 1U)];
            if(adjust) {
                uint32_t next = s_fifo[(s_read + i + 1U) & (FIFO_FRAMES - 1U)];
                int32_t left = (int16_t)sample * (int32_t)(HALF_FRAMES - i) + (int16_t)next * (int32_t)i;
                int32_t right = (int16_t)(sample >> 16) * (int32_t)(HALF_FRAMES - i) +
                                (int16_t)(next >> 16) * (int32_t)i;
                sample = (uint16_t)(left / (int32_t)HALF_FRAMES) |
                         ((uint32_t)(uint16_t)(right / (int32_t)HALF_FRAMES) << 16);
            }
            s_dma[s_half][i] = s_muted ? 0 : sample;
        }
        s_read += HALF_FRAMES + adjust;
    }
    taskEXIT_CRITICAL();
    if(s_edges != s_expected) {
        usb_audio_stop();
        return;
    }
    s_half ^= 1U;
    ++s_expected;
}

bool usb_audio_running(void) { return s_running; }
bool usb_audio_audible(void)
{
    return s_running && s_stream && s_locked && s_primed && !s_muted &&
           (uint32_t)(HAL_GetTick() - s_last_packet) < 100U;
}
