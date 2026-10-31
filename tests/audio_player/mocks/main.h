#ifndef TEST_MAIN_H
#define TEST_MAIN_H
#include <stdint.h>
#include <stddef.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define configTICK_RATE_HZ 1000U
typedef void *TaskHandle_t;
typedef enum { eSetBits } eNotifyAction;
#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
#define portYIELD_FROM_ISR(wake) ((void)(wake))
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TickType_t xTaskGetTickCount(void);
BaseType_t xTaskNotifyWait(uint32_t, uint32_t, uint32_t *, uint32_t);
BaseType_t xTaskNotifyFromISR(TaskHandle_t, uint32_t, eNotifyAction, BaseType_t *);
#endif
