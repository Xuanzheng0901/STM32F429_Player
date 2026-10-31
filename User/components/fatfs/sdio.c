#include "diskio.h"
#include "sdio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <string.h>

#define TRANSFER_TIMEOUT pdMS_TO_TICKS(30000)
#define REMOVAL_POLL pdMS_TO_TICKS(10)
#define BOUNCE_SECTORS 8U
#define DMA_SECTORS 511U /* NDTR 为 16 位，每扇区 128 个 word。 */
#define EVENT_COUNT 4U
#ifndef SD_DMA_RAM_START
#define SD_DMA_RAM_START ((uintptr_t)0x20000000U)
#define SD_DMA_RAM_END   ((uintptr_t)0x20030000U)
#endif

enum { MEDIA_ACTIVE, MEDIA_LOST, MEDIA_REPORTED };
enum { DMA_READ = 1, DMA_WRITE, DMA_ERROR };
static unsigned s_media, s_absent_samples;
static bool s_initialized;
static SemaphoreHandle_t s_mutex;
static StaticSemaphore_t s_mutex_storage;
static QueueHandle_t s_events;
static StaticQueue_t s_queue_storage;
static uint8_t s_queue_buffer[EVENT_COUNT * sizeof(uint32_t)];
static _Alignas(4) uint8_t s_bounce[BOUNCE_SECTORS * SDMMC_SECTOR_SIZE];

static bool card_present(void)
{
    taskENTER_CRITICAL();
    if(s_media == MEDIA_ACTIVE && HAL_GPIO_ReadPin(SDIO_CD_GPIO_Port, SDIO_CD_Pin) != GPIO_PIN_RESET)
        s_media = MEDIA_LOST;
    bool present = s_media == MEDIA_ACTIVE;
    taskEXIT_CRITICAL();
    return present;
}

void sdmmc_invalidate(void)
{
    taskENTER_CRITICAL();
    if(s_media == MEDIA_ACTIVE) s_media = MEDIA_LOST;
    taskEXIT_CRITICAL();
}

void sdmmc_new_session(void)
{
    taskENTER_CRITICAL();
    s_media = MEDIA_ACTIVE;
    s_absent_samples = 0;
    s_initialized = false;
    taskEXIT_CRITICAL();
}

bool sdmmc_poll_removal(void)
{
    taskENTER_CRITICAL();
    if(HAL_GPIO_ReadPin(SDIO_CD_GPIO_Port, SDIO_CD_Pin) == GPIO_PIN_RESET)
        s_absent_samples = 0;
    else if(s_absent_samples < 3 && ++s_absent_samples == 3 && s_media == MEDIA_ACTIVE)
        s_media = MEDIA_LOST;
    bool removed = s_media == MEDIA_LOST;
    if(removed) s_media = MEDIA_REPORTED;
    taskEXIT_CRITICAL();
    return removed;
}

sdmmc_state_t sdmmc_status(void)
{
    if(!card_present()) return SDMMC_REMOVED;
    return s_initialized ? SDMMC_READY : SDMMC_UNINITIALIZED;
}

static bool wait_ready(void)
{
    TickType_t started = xTaskGetTickCount();
    while(card_present() && (TickType_t)(xTaskGetTickCount() - started) < TRANSFER_TIMEOUT)
    {
        if(HAL_SD_GetCardState(&hsd) == HAL_SD_CARD_TRANSFER) return true;
        vTaskDelay(1);
    }
    return false;
}

sdmmc_state_t sdmmc_initialize(void)
{
    if(!s_mutex)
    {
        s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
        s_events = xQueueCreateStatic(EVENT_COUNT, sizeof(uint32_t), s_queue_buffer, &s_queue_storage);
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_initialized = card_present() && HAL_SD_Init(&hsd) == HAL_OK &&
                    HAL_SD_ConfigWideBusOperation(&hsd, SDIO_BUS_WIDE_4B) == HAL_OK && wait_ready();
    xSemaphoreGive(s_mutex);
    return sdmmc_status();
}

bool sdmmc_dma_capable(const void *buffer, size_t size)
{
    uintptr_t address = (uintptr_t)buffer;
    return !(address & 3U) && address >= SD_DMA_RAM_START && address < SD_DMA_RAM_END && size <= SD_DMA_RAM_END - address;
}

static bool transfer_dma(uint8_t *buffer, uint32_t sector, unsigned count, bool write)
{
    if(!wait_ready()) return false;
    xQueueReset(s_events);
    HAL_StatusTypeDef started = write ? HAL_SD_WriteBlocks_DMA(&hsd, buffer, sector, count) :
                                       HAL_SD_ReadBlocks_DMA(&hsd, buffer, sector, count);
    if(started != HAL_OK) return false;
    TickType_t tick = xTaskGetTickCount();
    while(card_present() && (TickType_t)(xTaskGetTickCount() - tick) < TRANSFER_TIMEOUT)
    {
        uint32_t event;
        if(xQueueReceive(s_events, &event, REMOVAL_POLL) == pdTRUE)
            return event == (write ? DMA_WRITE : DMA_READ) && wait_ready();
    }
    return false;
}

sdmmc_result_t sdmmc_transfer(void *data, uint32_t sector, unsigned count, bool write)
{
    uint8_t *buffer = data;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    sdmmc_result_t result = SDMMC_NOT_READY;
    if(sdmmc_status() != SDMMC_READY) goto done;
    bool direct = sdmmc_dma_capable(buffer, (size_t)count * SDMMC_SECTOR_SIZE);
    unsigned limit = direct ? DMA_SECTORS : BOUNCE_SECTORS;
    while(count)
    {
        unsigned batch = count < limit ? count : limit;
        size_t bytes = batch * SDMMC_SECTOR_SIZE;
        uint8_t *dma = direct ? buffer : s_bounce;
        if(!direct && write) memcpy(dma, buffer, bytes);
        if(!transfer_dma(dma, sector, batch, write))
        {
            /* 必须停止 DMA 后才能归还缓冲；失败后的访问需要重新初始化。 */
            HAL_SD_Abort(&hsd);
            s_initialized = false;
            result = card_present() ? SDMMC_IO_ERROR : SDMMC_NOT_READY;
            goto done;
        }
        if(!direct && !write) memcpy(buffer, dma, bytes);
        buffer += bytes;
        sector += batch;
        count -= batch;
    }
    result = SDMMC_OK;
done:
    xSemaphoreGive(s_mutex);
    return result;
}

sdmmc_result_t sdmmc_sync(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    sdmmc_result_t result = SDMMC_NOT_READY;
    if(sdmmc_status() == SDMMC_READY)
        result = wait_ready() ? SDMMC_OK : SDMMC_IO_ERROR;
    xSemaphoreGive(s_mutex);
    return result;
}

uint32_t sdmmc_sector_count(void)
{
    HAL_SD_CardInfoTypeDef info;
    HAL_SD_GetCardInfo(&hsd, &info);
    return info.LogBlockNbr;
}

static void post_event(uint32_t event)
{
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_events, &event, &woken);
    portYIELD_FROM_ISR(woken);
}

void HAL_SD_RxCpltCallback(SD_HandleTypeDef *handle) { (void)handle; post_event(DMA_READ); }
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *handle) { (void)handle; post_event(DMA_WRITE); }
void HAL_SD_ErrorCallback(SD_HandleTypeDef *handle) { (void)handle; post_event(DMA_ERROR); }
void HAL_SD_AbortCallback(SD_HandleTypeDef *handle) { (void)handle; post_event(DMA_ERROR); }
