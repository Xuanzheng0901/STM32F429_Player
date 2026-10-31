#ifndef PORT_TEST_TIMERS_H
#define PORT_TEST_TIMERS_H
#include "FreeRTOS.h"
typedef struct test_timer *TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t);
TimerHandle_t xTimerCreate(const char *, unsigned, int, void *, TimerCallbackFunction_t);
int xTimerStart(TimerHandle_t, unsigned);
int xTimerStop(TimerHandle_t, unsigned);
int xTimerDelete(TimerHandle_t, unsigned);
#endif
