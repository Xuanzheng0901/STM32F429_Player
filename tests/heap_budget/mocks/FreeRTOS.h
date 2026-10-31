#ifndef HEAP_TEST_FREERTOS_H
#define HEAP_TEST_FREERTOS_H
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#define configTOTAL_HEAP_SIZE TEST_HEAP_BYTES
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configAPPLICATION_ALLOCATED_HEAP 0
#define configUSE_MALLOC_FAILED_HOOK 0
#define portBYTE_ALIGNMENT 8U
#define portBYTE_ALIGNMENT_MASK 7U
#define portMAX_DELAY SIZE_MAX
#define configASSERT(x) assert(x)
#define mtCOVERAGE_TEST_MARKER() ((void)0)
#define traceMALLOC(p, n) ((void)0)
#define traceFREE(p, n) ((void)0)
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
typedef struct {
    size_t xAvailableHeapSpaceInBytes, xSizeOfLargestFreeBlockInBytes;
    size_t xSizeOfSmallestFreeBlockInBytes, xNumberOfFreeBlocks;
    size_t xMinimumEverFreeBytesRemaining, xNumberOfSuccessfulAllocations;
    size_t xNumberOfSuccessfulFrees;
} HeapStats_t;
void *pvPortMalloc(size_t);
void vPortFree(void *);
size_t xPortGetFreeHeapSize(void);
size_t xPortGetMinimumEverFreeHeapSize(void);
void vPortGetHeapStats(HeapStats_t *);
#endif
