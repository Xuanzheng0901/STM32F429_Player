#ifndef TEST_I2S_H
#define TEST_I2S_H
#include "main.h"
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY } HAL_StatusTypeDef;
typedef enum { HAL_I2S_STATE_RESET, HAL_I2S_STATE_READY, HAL_I2S_STATE_BUSY_TX } HAL_I2S_StateTypeDef;
typedef struct {
    struct { uint32_t Mode, Standard, DataFormat, MCLKOutput, AudioFreq, CPOL, ClockSource, FullDuplexMode; } Init;
    HAL_I2S_StateTypeDef State;
} I2S_HandleTypeDef;
#define I2S_AUDIOFREQ_8K 8000U
#define I2S_AUDIOFREQ_11K 11025U
#define I2S_AUDIOFREQ_16K 16000U
#define I2S_AUDIOFREQ_22K 22050U
#define I2S_AUDIOFREQ_32K 32000U
#define I2S_AUDIOFREQ_44K 44100U
#define I2S_AUDIOFREQ_48K 48000U
#define I2S_AUDIOFREQ_96K 96000U
#define I2S_MODE_MASTER_TX 1U
#define I2S_STANDARD_PHILIPS 2U
#define I2S_DATAFORMAT_16B 3U
#define I2S_DATAFORMAT_24B 8U
#define I2S_DATAFORMAT_32B 9U
#define I2S_MCLKOUTPUT_DISABLE 4U
#define I2S_CPOL_LOW 5U
#define I2S_CLOCK_PLL 6U
#define I2S_FULLDUPLEXMODE_DISABLE 7U
void test_i2s_disable(I2S_HandleTypeDef *);
#define __HAL_I2S_DISABLE(hi2s) test_i2s_disable(hi2s)
HAL_I2S_StateTypeDef HAL_I2S_GetState(I2S_HandleTypeDef *);
extern I2S_HandleTypeDef hi2s2;
HAL_StatusTypeDef HAL_I2S_Init(I2S_HandleTypeDef *);
HAL_StatusTypeDef HAL_I2S_Transmit_DMA(I2S_HandleTypeDef *, uint16_t *, uint16_t);
HAL_StatusTypeDef HAL_I2S_DMAStop(I2S_HandleTypeDef *);
HAL_StatusTypeDef HAL_I2S_DMAPause(I2S_HandleTypeDef *);
HAL_StatusTypeDef HAL_I2S_DMAResume(I2S_HandleTypeDef *);
void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *);
void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *);
void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *);
#endif
