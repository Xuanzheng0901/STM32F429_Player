#include "FreeRTOS.h"
uint32_t xTaskGetTickCount(void);
typedef void (*TaskFunction_t)(void *);
int xTaskCreate(TaskFunction_t, const char *, unsigned, void *, unsigned, TaskHandle_t *);
TaskHandle_t xTaskCreateStatic(TaskFunction_t, const char *, unsigned, void *, unsigned,
                              StackType_t *, StaticTask_t *);
void vTaskDelete(TaskHandle_t);
int xTaskNotify(TaskHandle_t, uint32_t, eNotifyAction);
