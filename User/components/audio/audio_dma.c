#include "audio_dma.h"
#include "i2s.h"

static void (*volatile s_handler)(uint32_t);

void audio_dma_set_handler(void (*handler)(uint32_t)) { s_handler = handler; }

static void dispatch(I2S_HandleTypeDef *i2s, uint32_t event)
{
    void (*handler)(uint32_t) = s_handler;
    if(i2s == &hi2s2 && handler) handler(event);
}

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *i2s) { dispatch(i2s, 1U); }
void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *i2s) { dispatch(i2s, 2U); }
void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *i2s) { dispatch(i2s, 4U); }
