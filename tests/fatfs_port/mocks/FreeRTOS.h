#ifndef TEST_FREERTOS_H
#define TEST_FREERTOS_H
#include <stdint.h>
#include <stddef.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;
typedef struct { uint32_t unused[23]; } StaticTask_t;
typedef struct { int held; } StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
typedef struct { uint32_t items[4]; unsigned head, count, size; uint8_t *data; } StaticQueue_t;
typedef StaticQueue_t *QueueHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
extern unsigned mock_yields;
#define portYIELD_FROM_ISR(woken) (mock_yields += ((woken) != 0))
void *pvPortMalloc(size_t size);
void vPortFree(void *memory);
#endif
