#include "main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../User/components/audio/usb_audio.c"
#include "../../User/components/audio/audio_output.c"

I2S_HandleTypeDef hi2s2;
GPIO_TypeDef test_gpio;
static uint32_t now, notification;
static unsigned critical, phase, stops;
static bool restart_on_snapshot;
static void packet(uint32_t *next);
static bool clock_ok = true, dma_ok = true, locked = true;
static GPIO_PinState ear = GPIO_PIN_SET, amp;

uint32_t HAL_GetTick(void) { return now; }
TickType_t xTaskGetTickCount(void) { return now; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void *)1; }
void test_enter_critical(void) { ++critical; }
void test_exit_critical(void)
{
    assert(critical); --critical;
    if(!critical && restart_on_snapshot) {
        restart_on_snapshot = false;
        usb_audio_set_stream(false);
        usb_audio_set_stream(true);
        uint32_t next = 1000;
        for(unsigned i = 0; i < 8; ++i) packet(&next);
    }
}
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
{ assert(i2s == &hi2s2 && rate == 96000 && bits == 32); return clock_ok ? HAL_OK : HAL_ERROR; }
HAL_StatusTypeDef ES9018_set_format(ES9018_SerialFormat format, ES9018_WordLength bits)
{ assert(format == ES9018_FORMAT_I2S && bits == ES9018_WORD_LENGTH_32); return HAL_OK; }
bool ES9018_is_locked(void) { return locked; }
HAL_StatusTypeDef HAL_I2S_Transmit_DMA(I2S_HandleTypeDef *i2s, uint16_t *data, uint16_t count)
{
    assert(i2s == &hi2s2 && data == (uint16_t *)s_dma && count == HALF_FRAMES * 4U);
    /* HAL 的 32 位格式把 Size 翻倍成 16 位 DMA 传输数。 */
    assert(count * 2U * sizeof(uint16_t) == sizeof(s_dma));
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
    pcm_frame_t data[96];
    for(unsigned i = 0; i < 96; ++i) {
        uint32_t value = (*next)++ * 0x00100001U;
        data[i] = (pcm_frame_t){(int32_t)value, (int32_t)~value};
    }
    usb_audio_receive((uint8_t *)data, sizeof(data));
}
static void check_frame(unsigned half, unsigned index, int32_t left, int32_t right)
{
    assert(s_dma[half][index * 4] == (uint16_t)((uint32_t)left >> 16));
    assert(s_dma[half][index * 4 + 1] == (uint16_t)left);
    assert(s_dma[half][index * 4 + 2] == (uint16_t)((uint32_t)right >> 16));
    assert(s_dma[half][index * 4 + 3] == (uint16_t)right);
}
static void check_silence(unsigned half)
{
    for(unsigned i = 0; i < HALF_FRAMES; ++i) check_frame(half, i, 0, 0);
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
        for(unsigned i = 0; i < HALF_FRAMES; ++i) {
            uint32_t sample = (i + 1) * 0x00100001U;
            check_frame(1, i, (int32_t)sample, (int32_t)~sample);
        }
        edge();
        check_frame(0, 0, (HALF_FRAMES + 1) * 0x00100001U,
                    (int32_t)~((HALF_FRAMES + 1) * 0x00100001U));
        edge(); assert(!usb_audio_audible());
        check_silence(1);
        for(unsigned i = 0; i < 8; ++i) packet(&next);
        usb_audio_set_mute(true); edge(); assert(!usb_audio_audible());
        check_silence(0);
        usb_audio_set_mute(false); edge(); assert(usb_audio_audible());
        usb_audio_set_stream(false); edge(); assert(!usb_audio_audible());
        usb_audio_set_stream(true);
        pcm_frame_t data[96];
        for(unsigned i = 0; i < 96; ++i)
            data[i] = (pcm_frame_t){i % 2 ? INT32_MAX : INT32_MIN, i % 2 ? INT32_MIN : INT32_MAX};
        for(unsigned i = 0; i < 16; ++i) usb_audio_receive((uint8_t *)data, sizeof(data));
        unsigned half = s_half;
        edge();
        assert(s_read == HALF_FRAMES + 1U);
        /* 满幅正负跳变，验证 64 位中间量不溢出，并检查左右声道的半字顺序。 */
        for(unsigned i = 0; i < HALF_FRAMES; ++i) {
            pcm_frame_t a = data[i % 96], b = data[(i + 1) % 96];
            int32_t left = (int32_t)(((int64_t)a.left * (HALF_FRAMES - i) + (int64_t)b.left * i) / HALF_FRAMES);
            int32_t right = (int32_t)(((int64_t)a.right * (HALF_FRAMES - i) + (int64_t)b.right * i) / HALF_FRAMES);
            check_frame(half, i, left, right);
        }
        check_frame(half, 0, INT32_MIN, INT32_MAX);
        check_frame(half, HALF_FRAMES / 2, 0, 0);
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
        usb_audio_receive((uint8_t *)&next, 4); assert(s_write == write);
        uint8_t data[USB_AUDIO_MAX_PACKET_BYTES + 8] = {0};
        usb_audio_receive(data, sizeof(data)); assert(s_write == write);
        for(unsigned i = 0; i < 22; ++i) packet(&next);
        write = s_write; packet(&next); assert(s_write == write);
        usb_audio_set_stream(true);
        /* 非四字节对齐输入、短包及 97 帧包均保持完整的 8 字节立体声帧。 */
        data[1] = 0x78; data[2] = 0x56; data[3] = 0x34; data[4] = 0x12;
        data[5] = 0xef; data[6] = 0xcd; data[7] = 0xab; data[8] = 0x90;
        usb_audio_receive(data + 1, 8);
        assert(s_write == 1 && s_fifo[0].left == 0x12345678 && (uint32_t)s_fifo[0].right == 0x90abcdef);
        usb_audio_receive(data, 760); usb_audio_receive(data, 776);
        assert(s_write == 1 + 95 + 97);
    }
    else if(!strcmp(argv[1], "restart")) {
        for(unsigned i = 0; i < 8; ++i) packet(&next);
        restart_on_snapshot = true;
        edge();
        assert(s_read == 0 && s_write == PREFILL_FRAMES && !s_primed);
        check_silence(0);
        edge(); assert(usb_audio_audible());
        uint32_t sample = 1000U * 0x00100001U;
        check_frame(1, 0, (int32_t)sample, (int32_t)~sample);
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
        assert(s_write - s_read < HALF_FRAMES * 4U);
    }
    puts("PASS: USB PCM buffering, source lifecycle and DMA ownership");
    return 0;
}
