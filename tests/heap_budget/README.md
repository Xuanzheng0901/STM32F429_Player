# FreeRTOS 堆预算

编译生产 `heap_4.c`，模拟当前 ARM ABI 的分配请求：TCB 92 B、Queue 80 B、Timer 44 B。容量从 `FreeRTOSConfig.h` 读取；主机块头 16 B，比目标的 8 B 更保守。

覆盖启动任务、日志、LVGL 管理任务、16 KiB swdraw 绘制线程、音频任务、锁和队列，以及 Idle 尚未回收启动任务时的首次挂载、两份 LFN 工作区和 32 次卸载/重挂载。曲库栈与 TCB 已改为普通 SRAM 静态分配，不占 RTOS 堆。

```powershell
$env:PATH = 'F:/DevTools/mingw64/bin;' + $env:PATH
cmake -S tests/heap_budget -B build/heap-tests -G Ninja -DCMAKE_C_COMPILER=F:/DevTools/mingw64/bin/gcc.exe
cmake --build build/heap-tests
ctest --test-dir build/heap-tests -V
```

当前模型：创建后台任务后剩余 2448 B，启动栈未回收且挂载/LFN 分配时最低剩余 1200 B，Idle 回收启动任务后已挂载状态剩余 6480 B。它验证已知分配能够放入 40 KiB，不是板上堆余量、碎片或任务栈高水位的实测值。新增线程或分配项时需更新预算；板上使用启动及挂载后的 `RTOS heap free/min` 日志确认。
