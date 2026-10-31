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
extern GPIO_TypeDef test_gpiod, test_gpioc;
#define BUTTON_SIDE1_GPIO_Port (&test_gpiod)
#define BUTTON_SIDE2_GPIO_Port (&test_gpiod)
#define BUTTON_SIDE3_GPIO_Port (&test_gpiod)
#define BUTTON_UP1_GPIO_Port (&test_gpiod)
#define BUTTON_UP2_GPIO_Port (&test_gpiod)
#define BUTTON_UP3_GPIO_Port (&test_gpiod)
#define BUTTON_UP4_GPIO_Port (&test_gpioc)
#define BUTTON_UP5_GPIO_Port (&test_gpioc)
#define BUTTON_SIDE1_Pin (1U << 10)
#define BUTTON_SIDE2_Pin (1U << 11)
#define BUTTON_SIDE3_Pin (1U << 12)
#define BUTTON_UP1_Pin (1U << 13)
#define BUTTON_UP2_Pin (1U << 14)
#define BUTTON_UP3_Pin (1U << 15)
#define BUTTON_UP4_Pin (1U << 6)
#define BUTTON_UP5_Pin (1U << 7)
#define GPIO_PIN_RESET 0
static inline int HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{ return (port->unused & pin) ? 0 : 1; }
#endif
