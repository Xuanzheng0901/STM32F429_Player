#ifndef ENCODER_TEST_MAIN_H
#define ENCODER_TEST_MAIN_H
#include <stdint.h>
typedef struct { uint32_t counter; int running; } TIM_HandleTypeDef;
typedef struct { int unused; } GPIO_TypeDef;
extern GPIO_TypeDef test_gpioe;
#define GPIOE (&test_gpioe)
#define KNOB_BUTTON_GPIO_Port GPIOE
#define KNOB_BUTTON_Pin GPIO_PIN_3
#define GPIO_PIN_3 ((uint16_t)0x0008)
#define TIM_CHANNEL_ALL 3
#define __HAL_TIM_GET_COUNTER(timer) ((timer)->counter)
static inline int HAL_TIM_Encoder_Start(TIM_HandleTypeDef *t, int channel)
{ (void)channel; t->running = 1; return 0; }
static inline int HAL_TIM_Encoder_Stop(TIM_HandleTypeDef *t, int channel)
{ (void)channel; t->running = 0; return 0; }
#endif
