#ifndef F407_PLAYER_AUDIO_I2S_H
#define F407_PLAYER_AUDIO_I2S_H

#include <stdbool.h>
#include "i2s.h"

bool audio_i2s_sample_rate_supported(uint32_t sample_rate);
/* 仅音频任务调用，先停止 DMA；要求 PLL 输入为 1 MHz，I2S 已初始化为 READY。 */
HAL_StatusTypeDef audio_i2s_configure(I2S_HandleTypeDef *hi2s, uint32_t sample_rate, unsigned bits);

#endif
