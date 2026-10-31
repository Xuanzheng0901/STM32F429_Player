# SDIO 块设备

SDIO：PC8～PC11 为 D0～D3，PC12 为 CK，PD2 为 CMD，4 bit；卡检测 PA15，输入上拉，低有效。DMA2 Stream6 / Channel4 共用于收发，IRQ 优先级 5。

sdmmc.c 管理 HAL SD 初始化、扇区读写、DMA 完成队列和传输互斥。Core/Src/sdio.c 只填写 hsd 配置，调度器启动后第一次 f_mount 才调用 sdmmc_initialize，不在开机无卡时进入 Error_Handler。

直接 DMA 要求 4 字节对齐且整个缓冲落在 0x20000000～0x20030000 普通 SRAM；其余地址经静态对齐的 4096 字节 s_bounce 中转，包括 CCMRAM 中的任务栈和 FatFs LFN 工作区。直接 DMA 最多 511 扇区一批，中转最多 8 扇区一批。

传输等待每 10 ms 检查卡状态，读取到拔卡电平即锁存当前会话失效。错误/拔卡后先停止 DMA，再归还缓冲。上层也可用三次无卡采样确认移除；新卡由上层在关闭所有旧文件并卸载后调用 sdmmc_new_session，再重新初始化。该层不挂载文件系统，不调用 UI 或播放器。

具体错误、超时、地址边界、分批和快速重插验证见 tests/fatfs_port/test_diskio.c。
