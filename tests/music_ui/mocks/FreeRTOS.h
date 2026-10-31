#ifndef ENCODER_TEST_FREERTOS_H
#define ENCODER_TEST_FREERTOS_H
#include <stdint.h>
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (ms)
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#endif
