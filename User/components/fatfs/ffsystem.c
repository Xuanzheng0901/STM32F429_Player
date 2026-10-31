#include "ff.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "log.h"

#if FF_USE_LFN == 3
void *ff_memalloc(UINT size)
{
    return pvPortMalloc(size);
}

void ff_memfree(void *memory)
{
    vPortFree(memory);
}
#endif

#if FF_FS_REENTRANT
/* 每卷一个互斥锁，额外一个供文件锁表使用。 */
static SemaphoreHandle_t s_fatfs_mutexes[FF_VOLUMES + 1];
static const char *s_owners[FF_VOLUMES + 1];
static TickType_t s_locked_at[FF_VOLUMES + 1];

int ff_mutex_create(int volume)
{
    s_fatfs_mutexes[volume] = xSemaphoreCreateMutex();
    return s_fatfs_mutexes[volume] != NULL;
}

void ff_mutex_delete(int volume)
{
    vSemaphoreDelete(s_fatfs_mutexes[volume]);
    s_fatfs_mutexes[volume] = NULL;
}

int ff_mutex_take(int volume)
{
    if(xSemaphoreTake(s_fatfs_mutexes[volume], FF_FS_TIMEOUT) != pdTRUE) {
        LOGE("FatFs", "lock timeout: volume=%d owner=%s held=%lu ticks", volume,
             s_owners[volume] ? s_owners[volume] : "?",
             (unsigned long)(xTaskGetTickCount() - s_locked_at[volume]));
        return 0;
    }
    s_owners[volume] = pcTaskGetName(NULL);
    s_locked_at[volume] = xTaskGetTickCount();
    return 1;
}

void ff_mutex_give(int volume)
{
    s_owners[volume] = NULL;
    xSemaphoreGive(s_fatfs_mutexes[volume]);
}
#endif
