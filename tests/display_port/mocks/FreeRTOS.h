#ifndef PORT_TEST_FREERTOS_H
#define PORT_TEST_FREERTOS_H
#include <stdint.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef void *xQueueHandle;
typedef void *xSemaphoreHandle;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
extern BaseType_t test_yield;
#define portYIELD_FROM_ISR(woken) (test_yield = (woken))
xQueueHandle xQueueCreate(unsigned, unsigned);
BaseType_t xQueueReceive(xQueueHandle, void *, TickType_t);
BaseType_t xQueueSend(xQueueHandle, const void *, TickType_t);
xSemaphoreHandle xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreGive(xSemaphoreHandle);
BaseType_t xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
TickType_t xTaskGetTickCount(void);
void vTaskNotifyGiveFromISR(TaskHandle_t, BaseType_t *);
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t);
void vTaskDelay(TickType_t);
#endif
