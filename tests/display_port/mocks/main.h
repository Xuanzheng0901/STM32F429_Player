#ifndef PORT_TEST_MAIN_H
#define PORT_TEST_MAIN_H
#include "FreeRTOS.h"
typedef struct { uint32_t counter; int running; } TIM_HandleTypeDef;
typedef struct { int unused; } UART_HandleTypeDef;
extern UART_HandleTypeDef huart1;
#define HAL_OK 0
#define TIM_CHANNEL_ALL 3
#define __HAL_TIM_GET_COUNTER(timer) ((timer)->counter)
static inline int HAL_TIM_Encoder_Start(TIM_HandleTypeDef *t, int ch)
{ (void)ch; t->running = 1; return HAL_OK; }
static inline int HAL_TIM_Encoder_Stop(TIM_HandleTypeDef *t, int ch)
{ (void)ch; t->running = 0; return HAL_OK; }
int HAL_UART_Transmit_DMA(UART_HandleTypeDef *, const uint8_t *, uint16_t);
#endif
