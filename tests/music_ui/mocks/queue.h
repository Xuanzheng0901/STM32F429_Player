#include "FreeRTOS.h"
typedef struct test_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned, unsigned);
int xQueueSend(QueueHandle_t, const void *, unsigned);
int xQueueReceive(QueueHandle_t, void *, unsigned);
unsigned uxQueueMessagesWaiting(QueueHandle_t);
void vQueueDelete(QueueHandle_t);
