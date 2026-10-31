#ifndef BATTERY_TEST_FREERTOS_H
#define BATTERY_TEST_FREERTOS_H
#include <stddef.h>
#include <stdint.h>
typedef uint32_t StackType_t;
typedef uint32_t TickType_t;
typedef struct { unsigned dummy; } StaticTask_t;
typedef StaticTask_t *TaskHandle_t;
#define pdMS_TO_TICKS(ms) (ms)
#endif
