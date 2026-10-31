#ifndef TEST_AUDIO_IO_MAIN_H
#define TEST_AUDIO_IO_MAIN_H
#include "../../audio_player/mocks/main.h"
typedef int GPIO_TypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
extern GPIO_TypeDef test_gpio;
#define EAR_DET_GPIO_Port (&test_gpio)
#define MAX97220_MUTE_GPIO_Port (&test_gpio)
#define EAR_DET_Pin 0x4000U
#define MAX97220_MUTE_Pin 0x0200U
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *, uint16_t);
void HAL_GPIO_WritePin(GPIO_TypeDef *, uint16_t, GPIO_PinState);
uint32_t HAL_GetTick(void);
void test_enter_critical(void);
void test_exit_critical(void);
#define taskENTER_CRITICAL() test_enter_critical()
#define taskEXIT_CRITICAL() test_exit_critical()
#endif
