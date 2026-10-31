#include "FreeRTOS.h"
TaskHandle_t xTaskCreateStatic(void (*task)(void *), const char *name, uint32_t words,
                             void *argument, unsigned priority, StackType_t *stack,
                             StaticTask_t *tcb);
TickType_t xTaskGetTickCount(void);
void vTaskDelayUntil(TickType_t *wake, TickType_t period);
