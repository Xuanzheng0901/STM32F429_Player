#include "FreeRTOS.h"
typedef struct test_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned length, unsigned size);
int xQueueReceive(QueueHandle_t, void *, uint32_t);
int xQueuePeek(QueueHandle_t, void *, uint32_t);
int xQueueOverwrite(QueueHandle_t, const void *);
void vQueueDelete(QueueHandle_t);

int xQueueReset(QueueHandle_t);
