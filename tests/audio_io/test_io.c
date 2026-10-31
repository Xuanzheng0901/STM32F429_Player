#include "main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../User/components/player/usb_audio.c"
#include "../../User/components/player/audio_output.c"

I2S_HandleTypeDef hi2s2;
GPIO_TypeDef test_gpio;
static uint32_t now, notification;
static unsigned critical, phase, stops;
static bool clock_ok = true, dma_ok = true, locked = true;
static GPIO_PinState ear = GPIO_PIN_SET, amp;

uint32_t HAL_GetTick(void) { return now; }
TickType_t xTaskGetTickCount(void) { return now; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void *)1; }
void test_enter_critical(void) { ++critical; }
void test_exit_critical(void) { assert(critical); --critical; }
BaseType_t xTaskNotifyWait(uint32_t entry, uint32_t exit, uint32_t *value, uint32_t timeout)
{
    (void)entry; (void)exit;
    assert(!critical);
    *value = notification; notification = 0;
    if(!*value) now += timeout;
    return *value ? pdTRUE : pdFALSE;
}
BaseType_t xTaskNotifyFromISR(TaskHandle_t task, uint32_t bits, eNotifyAction action, BaseType_t *wake)
{
    assert(task == (void *)1 && action == eSetBits);
    notification |= bits; *wake = pdFALSE; return pdTRUE;
}
HAL_StatusTypeDef audio_i2s_configure(I2S_HandleTypeDef *i2s, uint32_t rate, unsigned bits)
{ assert(i2s == &hi2s2 && rate == 48000 && bits == 16); return clock_ok ? HAL_OK : HAL_ERROR; }
HAL_StatusTypeDef ES9018_set_format(ES9018_SerialFormat format, ES9018_WordLength bits)
{ assert(format == ES9018_FORMAT_I2S && bits == ES9018_WORD_LENGTH_16); return HAL_OK; }
bool ES9018_is_locked(void) { return locked; }
HAL_StatusTypeDef HAL_I2S_Transmit_DMA(I2S_HandleTypeDef *i2s, uint16_t *data, uint16_t count)
{
    assert(i2s == &hi2s2 && data == (uint16_t *)s_dma && count == HALF_FRAMES * 4U);
    phase = 0; return dma_ok ? HAL_OK : HAL_ERROR;
}
HAL_StatusTypeDef HAL_I2S_DMAStop(I2S_HandleTypeDef *i2s)
{ assert(i2s == &hi2s2); ++stops; return HAL_OK; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{ assert(port == &test_gpio && pin == EAR_DET_Pin); return ear; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level)
{ assert(port == &test_gpio && pin == MAX97220_MUTE_Pin); amp = level; }

static void packet(uint32_t *next)
{
    uint32_t data[48];
    for(unsigned i = 0; i < 48; ++i) data[i] = (*next)++;
    usb_audio_receive((uint8_t *)data, sizeof(data));
}
static void edge(void)
{
    if(phase++ % 2U) HAL_I2S_TxCpltCallback(&hi2s2);
    else HAL_I2S_TxHalfCpltCallback(&hi2s2);
    now += 4;
    usb_audio_poll(20);
}
static void begin(void)
{
    assert(!usb_audio_connected());
    usb_audio_host_activity(); assert(usb_audio_connected());
    now += 499; assert(usb_audio_connected());
    ++now; assert(!usb_audio_connected());
    usb_audio_host_activity();
    usb_audio_set_stream(true);
    assert(usb_audio_start());
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    if(!strcmp(argv[1], "headphone")) {
        audio_output_init(); audio_output_set_active(true); assert(amp == GPIO_PIN_RESET);
        ear = GPIO_PIN_RESET; assert(audio_output_poll() == -1);
        now += 29; assert(audio_output_poll() == -1);
        audio_output_set_active(true); assert(amp == GPIO_PIN_RESET);
        ++now; assert(audio_output_poll() == 1);
        audio_output_set_active(true); assert(amp == GPIO_PIN_SET);
        ear = GPIO_PIN_SET;
        HAL_GPIO_EXTI_Callback(EAR_DET_Pin); assert(amp == GPIO_PIN_RESET);
        audio_output_set_active(true); assert(amp == GPIO_PIN_RESET);
        assert(audio_output_poll() == -1); now += 30; assert(audio_output_poll() == 0);
        ear = GPIO_PIN_RESET; assert(audio_output_poll() == -1); now += 30;
        assert(audio_output_poll() == 1);
        audio_output_set_active(false); assert(amp == GPIO_PIN_RESET);
        audio_output_set_active(true); assert(amp == GPIO_PIN_SET);
        now = UINT32_MAX - 10; ear = GPIO_PIN_SET; audio_output_poll();
        now = 20; assert(audio_output_poll() == 0);
        puts("PASS: headphone debounce, immediate IRQ mute, playback gate and tick wrap");
        return 0;
    }
    begin();
    uint32_t next = 1;
    if(!strcmp(argv[1], "transport")) {
        packet(&next); edge(); assert(!usb_audio_audible());
        for(unsigned i = 0; i < 7; ++i) packet(&next);
        edge(); assert(usb_audio_audible());
        for(unsigned i = 0; i < HALF_FRAMES; ++i) assert(s_dma[1][i] == i + 1);
        edge(); assert(s_dma[0][0] == HALF_FRAMES + 1);
        edge(); assert(!usb_audio_audible());
        for(unsigned i = 0; i < HALF_FRAMES; ++i) assert(!s_dma[1][i]);
        for(unsigned i = 0; i < 8; ++i) packet(&next);
        usb_audio_set_mute(true); edge(); assert(!usb_audio_audible());
        for(unsigned i = 0; i < HALF_FRAMES; ++i) assert(!s_dma[0][i]);
        usb_audio_set_mute(false); edge(); assert(usb_audio_audible());
        usb_audio_set_stream(false); edge(); assert(!usb_audio_audible());
        usb_audio_set_stream(true);
        uint32_t data[48];
        for(unsigned i = 0; i < 48; ++i)
            data[i] = (uint16_t)(i % 2 ? 30000 : -30000) |
                      ((uint32_t)(uint16_t)(i % 2 ? -25000 : 25000) << 16);
        for(unsigned i = 0; i < 32; ++i) usb_audio_receive((uint8_t *)data, sizeof(data));
        unsigned half = s_half;
        edge();
        assert(s_read == HALF_FRAMES + 1U);
        assert((int16_t)s_dma[half][0] == -30000 && (int16_t)(s_dma[half][0] >> 16) == 25000);
        assert(s_dma[half][96] == 0);
        assert((int16_t)s_dma[half][191] == -29687 && (int16_t)(s_dma[half][191] >> 16) == 24739);
        usb_audio_disconnect(); assert(!usb_audio_connected());
        usb_audio_stop(); assert(!usb_audio_running() && stops == 1);
        HAL_I2S_ErrorCallback(&hi2s2); assert(!notification);
    }
    else if(!strcmp(argv[1], "faults")) {
        HAL_I2S_ErrorCallback(&hi2s2); usb_audio_poll(20); assert(!usb_audio_running());
        clock_ok = false; assert(!usb_audio_start()); clock_ok = true;
        dma_ok = false; assert(!usb_audio_start()); dma_ok = true;
        assert(usb_audio_start()); locked = false;
        for(unsigned i = 0; i < 250; ++i) edge();
        assert(!usb_audio_running()); locked = true;
        assert(usb_audio_start());
        HAL_I2S_TxHalfCpltCallback(&hi2s2); HAL_I2S_TxCpltCallback(&hi2s2);
        usb_audio_poll(20); assert(!usb_audio_running());
        assert(usb_audio_start()); usb_audio_poll(20); assert(!usb_audio_running());
        assert(usb_audio_start());
        unsigned write = s_write;
        usb_audio_receive((uint8_t *)&next, 3); assert(s_write == write);
        for(unsigned i = 0; i < 42; ++i) packet(&next);
        write = s_write; packet(&next); assert(s_write == write);
    }
    else {
        assert(!strcmp(argv[1], "drift"));
        for(unsigned i = 0; i < 8; ++i) packet(&next);
        for(unsigned i = 0; i < 100000; ++i) {
            for(unsigned n = 0; n < 4; ++n) packet(&next);
            if(i % 1000 == 0) packet(&next); /* 输入时钟略快，累计漂移。 */
            edge(); usb_audio_host_activity();
            assert(usb_audio_running() && usb_audio_audible());
            assert(s_write - s_read < FIFO_FRAMES);
        }
        assert(s_write - s_read < HALF_FRAMES * 7U);
    }
    puts("PASS: USB PCM buffering, source lifecycle and DMA ownership");
    return 0;
}
