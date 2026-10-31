#ifndef TEST_PLAYER_FREERTOS_H
#define TEST_PLAYER_FREERTOS_H
#include <stdint.h>
#include <stddef.h>
typedef int BaseType_t;
typedef void *TaskHandle_t;
typedef uint32_t StackType_t;
typedef struct { uint32_t unused[23]; } StaticTask_t;
typedef enum { eSetBits } eNotifyAction;
#define pdTRUE 1
#define pdPASS 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
void test_enter_critical(void);
void test_exit_critical(void);
#define taskENTER_CRITICAL() test_enter_critical()
#define taskEXIT_CRITICAL() test_exit_critical()
/* 堆容量由 heap_budget 套件单独验证。 */
static inline size_t xPortGetFreeHeapSize(void) { return 0; }
static inline size_t xPortGetMinimumEverFreeHeapSize(void) { return 0; }
#endif
