#include "FreeRTOS.h"
#include <stdio.h>

/* 当前 ARM ELF 的 sizeof：TCB=92、Queue=80、Timer=44。
 * 主机 heap_4 块头为 16 B，比 ARM 的 8 B 更保守；不模拟任务栈使用深度。 */
static void *allocate(size_t bytes)
{
    void *memory = pvPortMalloc(bytes);
    assert(memory);
    return memory;
}
static void task(unsigned bytes)
{
    allocate(bytes);
    allocate(92);
}
int main(void)
{
    assert(TEST_HEAP_BYTES == 40U * 1024U);
    void *boot_stack = allocate(4096), *boot_tcb = allocate(92);
    allocate(80 + 10 * 16); /* 系统定时器命令队列，Idle/Timer 栈与 TCB 静态分配。 */
    task(1024);             /* 日志 */
    allocate(80 + 16 * 4);
    allocate(80 + 16 * 4);
    for(unsigned i = 0; i < 5; ++i) allocate(80); /* 显示 3 个锁、LVGL 2 个锁。 */
    task(16 * 1024);        /* LVGL 内部 swdraw，不能漏计。 */
    task(4 * 1024);         /* LVGL 管理任务 */
    allocate(44);          /* 临时侧键与上方五键共用的扫描定时器 */
    allocate(80 + 8);      /* 编码器按键队列 */
    allocate(80 + 12);
    allocate(80 + 1180);
    allocate(80 + 1184);
    allocate(80 + 4);      /* 浏览、命令、状态、停止确认队列 */
    task(8 * 1024);        /* 音频，曲库任务改为普通 SRAM 静态分配。 */
    size_t boot_free = xPortGetFreeHeapSize();
    assert(boot_free >= 2048);

    /* 删除启动任务后，其内存还要等 Idle 回收；挂载可能先发生。 */
    void *volume_lock = allocate(80), *file_lock = allocate(80);
    void *lfn1 = allocate(512), *lfn2 = allocate(512);
    size_t mount_free = xPortGetFreeHeapSize();
    assert(mount_free >= 1024);
    vPortFree(lfn1);
    vPortFree(lfn2);
    vPortFree(boot_stack);
    vPortFree(boot_tcb);
    size_t steady_free = xPortGetFreeHeapSize();
    for(unsigned cycle = 0; cycle < 32; ++cycle) {
        vPortFree(volume_lock);
        vPortFree(file_lock);
        volume_lock = allocate(80);
        file_lock = allocate(80);
        lfn1 = allocate(512);
        lfn2 = allocate(512);
        assert(xPortGetFreeHeapSize() >= 4096);
        vPortFree(lfn1);
        vPortFree(lfn2);
        assert(xPortGetFreeHeapSize() == steady_free);
    }
    printf("40 KiB heap budget: boot=%zu, mount before Idle=%zu, steady=%zu, min=%zu B\n",
           boot_free, mount_free, steady_free, xPortGetMinimumEverFreeHeapSize());
}
