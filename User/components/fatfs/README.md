# FatFs R0.16 适配

原版 ff.c、ffunicode.c，加上工程配置 ffconf.h、FreeRTOS 适配 ffsystem.c、磁盘适配 diskio.c 和 SDIO 块设备 sdmmc.c，组成文件系统组件。旧版 FatFs 和 CubeMX 生成适配文件已移除。

调用关系：player 模块内的 music_library.c / music_tags.c 使用 FatFs，sdcard 管理卷；fatfs 模块内由 diskio.c → sdmmc.c → HAL 完成磁盘访问。全部用户组件由 User/components/CMakeLists.txt 纳入主程序。diskio 只转换状态和返回值。

卷由 sdcard.c 私有持有。所有挂载、扫描和卸载在曲库任务中进行；音频任务持有播放 FIL。卸载前，曲库任务向音频任务发出停止命令并等待关闭确认，随后才调用 f_mount(NULL)。不创建额外的卡检测任务，不在定时器回调里读写文件。

FatFs 原生使用 UTF-8 路径。卷互斥、文件锁表锁由 ffsystem.c 提供；长文件名工作区使用 CCMRAM 中的 FreeRTOS 堆，SDIO 通过普通 SRAM 中转区读写这些内存。

disk_ioctl 支持 CTRL_SYNC、GET_SECTOR_COUNT、GET_SECTOR_SIZE、GET_BLOCK_SIZE。扇区固定 512 字节，擦除块返回 1 扇区，TRIM 关闭。

| 配置 | 值 |
| --- | --- |
| FFCONF_DEF | 80386，R0.16 |
| FF_FS_READONLY / FF_FS_MINIMIZE | 0 / 0 |
| FF_CODE_PAGE | 936 |
| FF_USE_LFN / FF_MAX_LFN | 3 / 255 |
| FF_LFN_UNICODE | 2，UTF-8 char 路径 |
| FF_LFN_BUF / FF_SFN_BUF | 765 / 34 字节 |
| FF_USE_STRFUNC / FF_STRF_ENCODE | 2 / 3 |
| FF_USE_MKFS / FF_USE_FASTSEEK / FF_USE_CHMOD | 1 / 1 / 1 |
| FF_VOLUMES | 1 |
| FF_MIN_SS / FF_MAX_SS | 512 / 512 |
| FF_FS_TINY / FF_FS_EXFAT / FF_LBA64 | 0 / 0 / 0 |
| FF_FS_REENTRANT / FF_FS_LOCK | 1 / 8 |
| FF_FS_TIMEOUT | 1000 ticks，当前为 1000 ms |
| FF_FS_NORTC | 1，时间固定为 2026-01-01 |

CubeMX FatFs 保持关闭；Core/Src/sdio.c 只填写句柄配置，实际卡初始化由任务中的 f_mount 发起。重新生成外设代码时，仍需保证启动阶段不调用 HAL_SD_Init 或 HAL_SD_ConfigWideBusOperation 进入无卡错误死循环。

应用已接入正常启动入口，无需强制保留符号来验证链接。构建及资源占用见 [应用说明](../README.md)，主机验证见 [文件模块测试](../../../tests/fatfs_port/README.md)。

## SDIO 块设备

SDIO：PC8～PC11 为 D0～D3，PC12 为 CK，PD2 为 CMD，4 bit；卡检测 PA15，输入上拉，低有效。DMA2 Stream6 / Channel4 共用于收发，IRQ 优先级 5。

sdmmc.c 管理 HAL SD 初始化、扇区读写、DMA 完成队列和传输互斥。Core/Src/sdio.c 只填写 hsd 配置，调度器启动后第一次 f_mount 才调用 sdmmc_initialize，不在开机无卡时进入 Error_Handler。

直接 DMA 要求 4 字节对齐且整个缓冲落在 0x20000000～0x20030000 普通 SRAM；其余地址经静态对齐的 4096 字节 s_bounce 中转，包括 CCMRAM 中的任务栈和 FatFs LFN 工作区。直接 DMA 最多 511 扇区一批，中转最多 8 扇区一批。

传输等待每 10 ms 检查卡状态，读取到拔卡电平即锁存当前会话失效。错误/拔卡后先停止 DMA，再归还缓冲。上层也可用三次无卡采样确认移除；新卡由上层在关闭所有旧文件并卸载后调用 sdmmc_new_session，再重新初始化。该层不挂载文件系统，不调用 UI 或播放器。

具体错误、超时、地址边界、分批和快速重插验证见 tests/fatfs_port/test_diskio.c。
