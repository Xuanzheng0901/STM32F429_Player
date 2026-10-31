#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
int xTaskCreate(TaskFunction_t, const char *, unsigned, void *, unsigned, TaskHandle_t *);
void vTaskDelete(TaskHandle_t);
int xTaskNotify(TaskHandle_t, uint32_t, eNotifyAction);
