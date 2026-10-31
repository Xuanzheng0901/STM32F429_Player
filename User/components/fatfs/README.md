# FatFs R0.16 适配

`fatfs_port` 静态库由 R0.16 原版 `ff.c`、`ffunicode.c`、工程配置 `ffconf.h`、FreeRTOS 适配 `ffsystem.c` 和磁盘接口 `diskio.c` 组成。旧版参考文件保留在 `legacy/`，不参与构建。

调用关系：`FatFs → diskio.c → sdmmc → HAL`。`diskio.c` 只转换盘号 0 的状态和返回值，不操作 GPIO、DMA、队列或卡检测。硬件配置及 DMA 策略见 [sdmmc](../sdmmc/README.md)。

`disk_ioctl()` 支持 CTRL_SYNC、GET_SECTOR_COUNT、GET_SECTOR_SIZE、GET_BLOCK_SIZE，扇区固定 512 字节，擦除块按 FatFs 约定返回 1 个扇区，TRIM 关闭。

卷由 `sdcard.c` 私有持有，调度器运行后调用 `f_mount()` 挂载。FatFs 文件直接使用 R0.16 的 UTF-8 API；文件所有者持有 FIL 并负责关闭。卷互斥、文件锁表锁由 `ffsystem.c` 提供；长文件名工作区使用 FreeRTOS 堆，位于 CCMRAM。

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

CubeMX FatFs 关闭，保留 SDIO、GPIO、DMA 和 FreeRTOS。主工程链接 `user_components → music_library → music_metadata → fatfs_port → sdmmc`，各库显式声明依赖，旧生成的 FatFs 不参与编译。

```powershell
cmake --preset Debug
cmake --build --preset Debug -j 8
cmake --preset Release
cmake --build --preset Release -j 8
```

强制保留 `sdcard_mount_and_scan`、`sdcard_load_music_window`、`disk_ioctl`、`f_mkfs` 的 Debug 完整链接：SRAM 29,912 B，CCMRAM 65,536 B，Flash 276,144 B。`sdmmc.c` 的 `s_bounce` 位于 `0x20005D8C`，4096 B；FreeRTOS `ucHeap` 位于 `0x10000000`，65536 B。普通启动入口暂未调用播放器，完整链接用于防止未接入功能被裁掉而掩盖链接问题。

[主机测试](../../../tests/fatfs_port/README.md) 覆盖 UTF-8 文件读写、标签、曲库、DMA 及拔卡。主机结果不能代替实体 SD 卡和板上验证。
