#include "ff.h"
#include "FreeRTOS.h"
#include "semphr.h"

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
    return xSemaphoreTake(s_fatfs_mutexes[volume], FF_FS_TIMEOUT) == pdTRUE;
}

void ff_mutex_give(int volume)
{
    xSemaphoreGive(s_fatfs_mutexes[volume]);
}
#endif
