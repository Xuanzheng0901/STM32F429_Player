#include "mock_platform.h"
#include "queue.h"
#include "task.h"
#include "semphr.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

SD_HandleTypeDef hsd;
_Alignas(4) uint8_t mock_sram[MOCK_SRAM_SIZE];
uint8_t mock_card[MOCK_SECTORS * 512U];
int mock_mode, mock_present = 1;
int mock_init_fail, mock_wide_fail, mock_busy;
unsigned mock_busy_reads, mock_post_dma_busy;
unsigned mock_dma_calls, mock_max_blocks, mock_aborts, mock_init_calls, mock_wide_calls, mock_yields;
void *mock_last_buffer;
TickType_t mock_ticks;
void (*mock_wait_hook)(void);
void (*mock_dma_hook)(void);
unsigned mock_task_calls;
static void (*detect_entry)(void *);
static jmp_buf detect_exit;
static unsigned detect_samples;
void mock_detect_samples(unsigned samples)
{
    detect_samples = samples + 1;
    if(setjmp(detect_exit) == 0)
        detect_entry(NULL);
}
BaseType_t xTaskCreate(void (*entry)(void *), const char *name, unsigned stack,
                      void *argument, unsigned priority, TaskHandle_t *handle)
{
    (void)entry; (void)name; (void)stack; (void)argument; (void)priority;
    detect_entry = entry;
    mock_task_calls++;
    *handle = (void *)1;
    return pdPASS;
}

TaskHandle_t xTaskCreateStatic(void (*entry)(void *), const char *name, unsigned stack,
                              void *argument, unsigned priority, StackType_t *memory,
                              StaticTask_t *storage)
{
    assert(memory && storage);
    TaskHandle_t task;
    xTaskCreate(entry, name, stack, argument, priority, &task);
    return task;
}

void mock_clear_counters(void)
{
    mock_dma_calls = mock_max_blocks = mock_aborts = 0;
    mock_last_buffer = NULL;
}
void *pvPortMalloc(size_t size) { return malloc(size); }
void vPortFree(void *memory) { free(memory); }
TickType_t xTaskGetTickCount(void) { return mock_ticks; }
void vTaskDelay(TickType_t ticks)
{
    if(detect_samples && --detect_samples == 0)
        longjmp(detect_exit, 1);
    mock_ticks += ticks;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return calloc(1, sizeof(StaticSemaphore_t)); }
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *mutex)
{ mutex->held = 0; return mutex; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait)
{
    (void)wait;
    assert(mutex && !mutex->held);
    mutex->held = 1;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex)
{ assert(mutex && mutex->held); mutex->held = 0; return pdTRUE; }
void vSemaphoreDelete(SemaphoreHandle_t mutex) { assert(mutex && !mutex->held); free(mutex); }
QueueHandle_t xQueueCreateStatic(unsigned count, unsigned size, uint8_t *buffer, StaticQueue_t *queue)
{
    (void)buffer;
    assert(count == 4 && size == sizeof(uint32_t));
    memset(queue, 0, sizeof(*queue));
    queue->data = (uint8_t *)queue->items;
    queue->size = size;
    return queue;
}
BaseType_t xQueueReset(QueueHandle_t queue)
{ queue->head = queue->count = 0; return pdTRUE; }
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait)
{
    if(!queue->count)
    {
        mock_ticks += wait;
        if(wait && mock_wait_hook) mock_wait_hook();
        if(!queue->count) return pdFALSE;
    }
    memcpy(item, queue->data + queue->head * queue->size, queue->size);
    queue->head = (queue->head + 1) % (queue->data == (uint8_t *)queue->items ? 4 : 1);
    queue->count--;
    return pdTRUE;
}
BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *item, BaseType_t *woken)
{
    if(queue->count == 4) return pdFALSE;
    queue->items[(queue->head + queue->count++) % 4] = *(const uint32_t *)item;
    *woken = pdTRUE;
    return pdTRUE;
}
int HAL_GPIO_ReadPin(void *port, unsigned pin)
{ assert(port == SDIO_CD_GPIO_Port && pin == SDIO_CD_Pin); return mock_present ? GPIO_PIN_RESET : 1; }
HAL_StatusTypeDef HAL_SD_Init(SD_HandleTypeDef *handle)
{ assert(handle == &hsd); mock_init_calls++; return mock_init_fail ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_SD_ConfigWideBusOperation(SD_HandleTypeDef *handle, unsigned width)
{ assert(handle == &hsd && width == SDIO_BUS_WIDE_4B); mock_wide_calls++; return mock_wide_fail ? HAL_ERROR : HAL_OK; }
int HAL_SD_GetCardState(SD_HandleTypeDef *handle)
{
    assert(handle == &hsd);
    if(mock_busy_reads) { mock_busy_reads--; return 0; }
    return mock_busy ? 0 : HAL_SD_CARD_TRANSFER;
}
HAL_StatusTypeDef HAL_SD_GetCardInfo(SD_HandleTypeDef *handle, HAL_SD_CardInfoTypeDef *info)
{ assert(handle == &hsd); info->LogBlockNbr = MOCK_SECTORS; return HAL_OK; }
static HAL_StatusTypeDef transfer(SD_HandleTypeDef *handle, uint8_t *buffer, uint32_t sector, uint32_t count, int write)
{
    assert(handle == &hsd && ((uintptr_t)buffer & 3) == 0);
    assert(count && count <= 511 && sector + count <= MOCK_SECTORS);
    mock_dma_calls++;
    if(count > mock_max_blocks) mock_max_blocks = count;
    mock_last_buffer = buffer;
    if(mock_dma_hook) mock_dma_hook();
    switch(mock_mode)
    {
        case MOCK_START_FAIL: return HAL_ERROR;
        case MOCK_DMA_ERROR: HAL_SD_ErrorCallback(handle); return HAL_OK;
        case MOCK_DMA_ABORT: HAL_SD_AbortCallback(handle); return HAL_OK;
        case MOCK_TIMEOUT: return HAL_OK;
        case MOCK_WRONG_EVENT:
            if(write) HAL_SD_RxCpltCallback(handle); else HAL_SD_TxCpltCallback(handle);
            return HAL_OK;
    }
    if(write) memcpy(mock_card + sector * 512U, buffer, count * 512U);
    else memcpy(buffer, mock_card + sector * 512U, count * 512U);
    mock_busy_reads = mock_post_dma_busy;
    if(write) HAL_SD_TxCpltCallback(handle); else HAL_SD_RxCpltCallback(handle);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_SD_ReadBlocks_DMA(SD_HandleTypeDef *handle, uint8_t *buffer, uint32_t sector, uint32_t count)
{ return transfer(handle, buffer, sector, count, 0); }
HAL_StatusTypeDef HAL_SD_WriteBlocks_DMA(SD_HandleTypeDef *handle, uint8_t *buffer, uint32_t sector, uint32_t count)
{ return transfer(handle, buffer, sector, count, 1); }
HAL_StatusTypeDef HAL_SD_Abort(SD_HandleTypeDef *handle)
{ assert(handle == &hsd); mock_aborts++; return HAL_OK; }

QueueHandle_t xQueueCreate(unsigned count, unsigned size)
{
    assert(count == 1);
    QueueHandle_t queue = calloc(1, sizeof(*queue) + size);
    queue->data = (uint8_t *)(queue + 1);
    queue->size = size;
    return queue;
}
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item)
{
    memcpy(queue->data, item, queue->size);
    queue->head = 0;
    queue->count = 1;
    return pdTRUE;
}
BaseType_t xQueuePeek(QueueHandle_t queue, void *item, TickType_t wait)
{
    (void)wait;
    if(!queue->count) return pdFALSE;
    memcpy(item, queue->data + queue->head * queue->size, queue->size);
    return pdTRUE;
}
void vQueueDelete(QueueHandle_t queue) { free(queue); }
BaseType_t xTaskNotify(TaskHandle_t task, uint32_t value, int action)
{ (void)task; (void)value; (void)action; return pdTRUE; }
void vTaskDelete(TaskHandle_t task) { (void)task; }
