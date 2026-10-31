#include "FreeRTOS.h"
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);

typedef void *TaskHandle_t;
BaseType_t xTaskCreate(void (*entry)(void *), const char *name, unsigned stack,
                       void *argument, unsigned priority, TaskHandle_t *handle);
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)

#define eSetBits 1
BaseType_t xTaskNotify(TaskHandle_t task, uint32_t value, int action);
void vTaskDelete(TaskHandle_t task);
