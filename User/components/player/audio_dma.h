#ifndef AUDIO_DMA_H
#define AUDIO_DMA_H

#include <stdint.h>

#define AUDIO_CONTROL_EVENT 8U

/* 音频任务停掉旧 DMA 后再更换接收者；事件 1/2/4 为半满/全满/错误。 */
void audio_dma_set_handler(void (*handler)(uint32_t event));

#endif
