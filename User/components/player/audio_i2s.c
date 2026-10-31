#include "audio_i2s.h"
#include <stddef.h>
#include "stm32f4xx_hal_rcc_ex.h"

typedef struct {
    uint32_t sample_rate;
    uint32_t plli2s_n;
    uint32_t plli2s_r;
} i2s_clock_config_t;

/* 移自 F429_Player/AUDIO_DECODER/utils.c；适用于 1 MHz PLLI2S 输入。 */
static const i2s_clock_config_t s_i2s_clock_configs[] = {
    {I2S_AUDIOFREQ_8K,  256U, 5U},
    {I2S_AUDIOFREQ_11K, 429U, 4U},
    {12000U,           384U, 5U},
    {I2S_AUDIOFREQ_16K, 213U, 4U},
    {I2S_AUDIOFREQ_22K, 429U, 4U},
    {24000U,           384U, 5U},
    {I2S_AUDIOFREQ_32K, 426U, 4U},
    {I2S_AUDIOFREQ_44K, 271U, 6U},
    {I2S_AUDIOFREQ_48K, 258U, 3U},
    {88200U,           271U, 2U},
    {I2S_AUDIOFREQ_96K, 344U, 2U},
};

static const i2s_clock_config_t *find_i2s_clock_config(uint32_t sample_rate)
{
    for(size_t i = 0; i < sizeof(s_i2s_clock_configs) / sizeof(s_i2s_clock_configs[0]); ++i)
        if(s_i2s_clock_configs[i].sample_rate == sample_rate) return &s_i2s_clock_configs[i];
    return NULL;
}

bool audio_i2s_sample_rate_supported(uint32_t sample_rate)
{
    return find_i2s_clock_config(sample_rate) != NULL;
}

HAL_StatusTypeDef audio_i2s_configure(I2S_HandleTypeDef *hi2s, uint32_t sample_rate, unsigned bits)
{
    if(hi2s == NULL || HAL_I2S_GetState(hi2s) != HAL_I2S_STATE_READY) return HAL_BUSY;
    const i2s_clock_config_t *config = find_i2s_clock_config(sample_rate);
    if(config == NULL || (bits != 16 && bits != 24 && bits != 32)) return HAL_ERROR;

    __HAL_I2S_DISABLE(hi2s);
    RCC_PeriphCLKInitTypeDef clock = {0};
    HAL_RCCEx_GetPeriphCLKConfig(&clock);
    clock.PeriphClockSelection = RCC_PERIPHCLK_I2S;
    clock.PLLI2S.PLLI2SN = config->plli2s_n;
    clock.PLLI2S.PLLI2SR = config->plli2s_r;
    if(HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) return HAL_ERROR;

    hi2s->Init.Mode = I2S_MODE_MASTER_TX;
    hi2s->Init.Standard = I2S_STANDARD_PHILIPS;
    hi2s->Init.DataFormat = bits == 16 ? I2S_DATAFORMAT_16B :
                           bits == 24 ? I2S_DATAFORMAT_24B : I2S_DATAFORMAT_32B;
    hi2s->Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
    hi2s->Init.AudioFreq = sample_rate;
    hi2s->Init.CPOL = I2S_CPOL_LOW;
    hi2s->Init.ClockSource = I2S_CLOCK_PLL;
    hi2s->Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
    return HAL_I2S_Init(hi2s);
}
