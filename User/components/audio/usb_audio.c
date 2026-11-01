#include "usb_audio.h"
#include "usb_audio_format.h"
#include "audio_dma.h"
#include "audio_i2s.h"
#include "i2s.h"
#include "es9018.h"
#include "FreeRTOS.h"
#include "task.h"
#include "log.h"
#include <string.h>

#define FIFO_FRAMES 2048U
#define HALF_FRAMES (USB_AUDIO_FRAMES_PER_MS * 4U)
#define PREFILL_FRAMES (HALF_FRAMES * 2U)
#define HOST_TIMEOUT_MS 500U

typedef struct { int32_t left, right; } pcm_frame_t;

/* USB 小端 int32_t；I2S 按左右声道依次发送高半字、低半字。缓冲均在普通 SRAM。 */
static pcm_frame_t s_fifo[FIFO_FRAMES];
static uint16_t s_dma[2][HALF_FRAMES * 4U];
static volatile uint32_t s_write, s_read, s_last_host, s_last_packet;
static volatile uint32_t s_stream_serial;
static volatile uint32_t s_packets, s_packet_bytes, s_bad_packets;
static uint32_t s_report_tick;
static const char *s_last_fault = "none";
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
    ++s_stream_serial;
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
    ++s_packets;
    s_packet_bytes = bytes;
    if(!s_stream || !bytes) return;
    if(bytes > USB_AUDIO_MAX_PACKET_BYTES || bytes % USB_AUDIO_FRAME_BYTES) {
        ++s_bad_packets;
        return;
    }
    uint32_t frames = bytes / USB_AUDIO_FRAME_BYTES;
    uint32_t write = s_write;
    if(frames > FIFO_FRAMES - (write - s_read)) return;
    for(uint32_t i = 0; i < frames; ++i)
        memcpy(&s_fifo[(write + i) & (FIFO_FRAMES - 1U)], data + i * USB_AUDIO_FRAME_BYTES, USB_AUDIO_FRAME_BYTES);
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
    if(audio_i2s_configure(&hi2s2, USB_AUDIO_SAMPLE_RATE, USB_AUDIO_BITS) != HAL_OK) {
        s_last_fault = "i2s-config";
        return false;
    }
    ES9018_set_format(ES9018_FORMAT_I2S, ES9018_WORD_LENGTH_32);
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
        s_last_fault = "dma-start";
        audio_dma_set_handler(NULL);
        return false;
    }
    s_running = true;
    return true;
}

void usb_audio_poll(uint32_t timeout_ticks)
{
    /* 只在音频任务中低频输出；USB 中断只更新计数，不打印日志。 */
    uint32_t now = HAL_GetTick();
    if(now - s_report_tick >= 2000U) {
        s_report_tick = now;
        taskENTER_CRITICAL();
        uint32_t queued = s_write - s_read, packets = s_packets;
        uint32_t bytes = s_packet_bytes, bad = s_bad_packets;
        taskEXIT_CRITICAL();
        LOGI("USB", "alt=%u rx=%lu size=%lu bad=%lu fifo=%lu dma=%lu lock=%u mute=%u last=%s",
             (unsigned)s_stream, (unsigned long)packets, (unsigned long)bytes,
             (unsigned long)bad, (unsigned long)queued, (unsigned long)s_edges,
             (unsigned)s_locked, (unsigned)s_muted, s_last_fault);
    }
    uint32_t events = 0;
    if(xTaskNotifyWait(0, UINT32_MAX, &events, timeout_ticks) != pdTRUE) {
        if(timeout_ticks && s_running) {
            s_last_fault = "dma-timeout";
            usb_audio_stop();
        }
        return;
    }
    events &= ~AUDIO_CONTROL_EVENT;
    if(!events || !s_running) return;
    if(events != (1U << s_half) || s_edges != s_expected) {
        s_last_fault = "dma-event";
        usb_audio_stop();
        return;
    }
    memset(s_dma[s_half], 0, sizeof(s_dma[s_half]));
    if(!s_locked) s_locked = ES9018_is_locked();
    if(!s_locked && (uint32_t)(HAL_GetTick() - s_lock_start) >= 1000U) {
        s_last_fault = "dac-lock";
        usb_audio_stop();
        return;
    }
    /* 快照 FIFO 状态，转换和 64 位插值不关中断；完成后再释放已消费的槽位。 */
    taskENTER_CRITICAL();
    uint32_t read = s_read, serial = s_stream_serial;
    uint32_t available = s_write - read;
    bool stream = s_stream;
    taskEXIT_CRITICAL();
    if(!stream || !s_locked || available < HALF_FRAMES) s_primed = false;
    if(stream && s_locked && available >= PREFILL_FRAMES) s_primed = true;
    if(s_primed) {
        bool adjust = available > HALF_FRAMES * 3U;
        for(unsigned i = 0; i < HALF_FRAMES; ++i) {
            pcm_frame_t sample = s_fifo[(read + i) & (FIFO_FRAMES - 1U)];
            if(adjust) {
                pcm_frame_t next = s_fifo[(read + i + 1U) & (FIFO_FRAMES - 1U)];
                sample.left = (int32_t)(((int64_t)sample.left * (HALF_FRAMES - i) +
                                         (int64_t)next.left * i) / HALF_FRAMES);
                sample.right = (int32_t)(((int64_t)sample.right * (HALF_FRAMES - i) +
                                          (int64_t)next.right * i) / HALF_FRAMES);
            }
            uint16_t *output = &s_dma[s_half][i * 4U];
            output[0] = s_muted ? 0 : (uint16_t)((uint32_t)sample.left >> 16);
            output[1] = s_muted ? 0 : (uint16_t)sample.left;
            output[2] = s_muted ? 0 : (uint16_t)((uint32_t)sample.right >> 16);
            output[3] = s_muted ? 0 : (uint16_t)sample.right;
        }
        taskENTER_CRITICAL();
        bool current = serial == s_stream_serial;
        if(current) s_read = read + HALF_FRAMES + adjust;
        taskEXIT_CRITICAL();
        /* USB alt 切换可以在转换途中重置 FIFO，不能提交旧流的读位置或声音。 */
        if(!current) {
            s_primed = false;
            memset(s_dma[s_half], 0, sizeof(s_dma[s_half]));
        }
    }
    if(s_edges != s_expected) {
        s_last_fault = "refill-late";
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
