# FatFs R0.16 适配

原版 ff.c、ffunicode.c，加上工程配置 ffconf.h、FreeRTOS 适配 ffsystem.c 和磁盘适配 diskio.c，组成文件系统组件。旧版 FatFs 和 CubeMX 生成适配文件已移除。

调用链：player → sdcard → library / metadata → FatFs → diskio → sdmmc → HAL。全部用户组件由 User/components/CMakeLists.txt 纳入主程序。diskio 只转换状态和返回值；硬件配置和 DMA 策略见 [sdmmc](../sdmmc/README.md)。

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
