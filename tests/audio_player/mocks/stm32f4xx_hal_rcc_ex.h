#ifndef TEST_RCC_EX_H
#define TEST_RCC_EX_H
#include "i2s.h"
#define RCC_PERIPHCLK_I2S 1U
typedef struct {
    uint32_t PeriphClockSelection;
    struct { uint32_t PLLI2SN, PLLI2SR; } PLLI2S;
} RCC_PeriphCLKInitTypeDef;
void HAL_RCCEx_GetPeriphCLKConfig(RCC_PeriphCLKInitTypeDef *);
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *);
#endif
