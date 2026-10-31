#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "audio_i2s.h"
#include "stm32f4xx_hal_rcc_ex.h"

static I2S_HandleTypeDef i2s = {.State = HAL_I2S_STATE_READY};
static RCC_PeriphCLKInitTypeDef current_clock;
static unsigned disabled, clock_calls, init_calls, bits;
static HAL_StatusTypeDef clock_result, init_result;

HAL_I2S_StateTypeDef HAL_I2S_GetState(I2S_HandleTypeDef *handle) { return handle->State; }
void test_i2s_disable(I2S_HandleTypeDef *handle) { assert(handle == &i2s); ++disabled; }
void HAL_RCCEx_GetPeriphCLKConfig(RCC_PeriphCLKInitTypeDef *config) { *config = current_clock; }
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *config)
{
    assert(disabled == clock_calls + 1 && config->PeriphClockSelection == RCC_PERIPHCLK_I2S);
    current_clock = *config; ++clock_calls; return clock_result;
}
HAL_StatusTypeDef HAL_I2S_Init(I2S_HandleTypeDef *handle)
{
    assert(handle == &i2s && handle->State == HAL_I2S_STATE_READY);
    assert(handle->Init.Mode == I2S_MODE_MASTER_TX && handle->Init.Standard == I2S_STANDARD_PHILIPS);
    assert(handle->Init.DataFormat == (bits == 16 ? I2S_DATAFORMAT_16B : bits == 24 ? I2S_DATAFORMAT_24B : I2S_DATAFORMAT_32B));
    assert(handle->Init.MCLKOutput == I2S_MCLKOUTPUT_DISABLE);
    assert(handle->Init.ClockSource == I2S_CLOCK_PLL && handle->Init.CPOL == I2S_CPOL_LOW);
    assert(handle->Init.FullDuplexMode == I2S_FULLDUPLEXMODE_DISABLE);
    ++init_calls; return init_result;
}
int main(void)
{
    const unsigned rates[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000};
    for(bits = 16; bits <= 32; bits += 8) {
        for(unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        memset(&i2s.Init, 0, sizeof(i2s.Init));
        assert(audio_i2s_sample_rate_supported(rates[i]));
        assert(audio_i2s_configure(&i2s, rates[i], bits) == HAL_OK && i2s.Init.AudioFreq == rates[i]);
        unsigned n = current_clock.PLLI2S.PLLI2SN, r = current_clock.PLLI2S.PLLI2SR;
        assert(n >= 192 && n <= 432 && r >= 2 && r <= 7);
        /* 每帧两个声道槽：16 位为 32 BCLK，24/32 位为 64 BCLK。 */
        double clk = 1000000.0 * n / r;
        double frame_bits = bits == 16 ? 32.0 : 64.0;
        unsigned divisor = (unsigned)(clk / (frame_bits * rates[i]) + 0.5);
        assert(divisor >= 4 && divisor <= 511);
        double error = clk / (frame_bits * divisor) / rates[i] - 1.0;
        assert(error > -0.001 && error < 0.001);
        }
    }
    bits = 16;
    assert(clock_calls == 33 && init_calls == 33);
    assert(!audio_i2s_sample_rate_supported(12345));
    assert(audio_i2s_configure(&i2s, 12345, bits) == HAL_ERROR);
    assert(audio_i2s_configure(&i2s, 44100, 20) == HAL_ERROR);
    assert(audio_i2s_configure(NULL, 44100, bits) == HAL_BUSY);
    i2s.State = HAL_I2S_STATE_BUSY_TX;
    assert(audio_i2s_configure(&i2s, 48000, bits) == HAL_BUSY);
    i2s.State = HAL_I2S_STATE_RESET; /* Avoid MSP restoring the fixed boot PLL configuration. */
    assert(audio_i2s_configure(&i2s, 48000, bits) == HAL_BUSY);
    assert(disabled == 33 && clock_calls == 33 && init_calls == 33);
    i2s.State = HAL_I2S_STATE_READY;
    clock_result = HAL_ERROR;
    assert(audio_i2s_configure(&i2s, 48000, bits) == HAL_ERROR && init_calls == 33);
    clock_result = HAL_OK; init_result = HAL_ERROR;
    assert(audio_i2s_configure(&i2s, 48000, bits) == HAL_ERROR && init_calls == 34);
    init_result = HAL_OK;
    assert(audio_i2s_configure(&i2s, 44100, bits) == HAL_OK && init_calls == 35);
    puts("PASS: eleven sample rates, clock precision, configuration order and failure recovery");
    return 0;
}
