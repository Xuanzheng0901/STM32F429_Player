#include "FreeRTOS.h"
QueueHandle_t xQueueCreateStatic(unsigned count, unsigned size, uint8_t *buffer, StaticQueue_t *queue);
BaseType_t xQueueReset(QueueHandle_t queue);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait);
BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *item, BaseType_t *woken);

QueueHandle_t xQueueCreate(unsigned count, unsigned size);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item);
BaseType_t xQueuePeek(QueueHandle_t queue, void *item, TickType_t wait);
void vQueueDelete(QueueHandle_t queue);
