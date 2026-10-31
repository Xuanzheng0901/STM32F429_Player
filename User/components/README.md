# F429 播放器应用

从 F407 迁移的应用已经接入 StartDefaultTask → app_main，当前固定使用 SSD1685、TIM3 编码器、I2S2 和 ES9018K2M。F407 工程保持原状。

| 组件 | 职责 |
| --- | --- |
| main | 初始化模块、连接 UI 回调 |
| UI | 绘制 360×184 播放器、处理编码器操作；只发送控制命令 |
| player | 曲库扫描与索引、标签解析、解码播放、命令合并、状态发布及卸载前的停止确认 |
| sdcard | 私有 FATFS 卷、挂载/卸载、插拔事件 |
| fatfs | FatFs R0.16、FreeRTOS 锁和内存、磁盘适配、SDIO 初始化与 DMA、卡检测及介质失效锁存 |
| display / es9018 / log | 当前 F429 硬件适配 |

player/music_library.c 负责递归扫描、隐藏索引和 15 首窗口，player/music_tags.c 负责 ID3、WAV INFO 和 FLAC Vorbis Comment；fatfs/sdmmc.c 负责块设备通信。共享数据在 player/music.h，UTF-8 路径容量 1024 字节。扫描根目录及最多四层子目录；每条索引固定 1024 字节，不在 RAM 中保存整库。显示文本截断保留完整 UTF-8 字符。

卡检测由曲库任务每 20 ms 轮询，连续三次有卡后挂载和重建索引。开机无卡显示未插卡，插入后自动扫描；拔卡时 SDIO 立即锁存介质失效，音频任务停止 DMA、关闭文件，曲库任务等到停止确认后卸载并清空 UI。重新插入选择第一首但不自动播放。挂载失败不后台反复重试，再次插卡时重新尝试。

控制、补数和 UI 更新分别处理；音频任务不持有 LVGL 锁。只保留必要的文件边界、格式能力、DMA 地址/相位和停止同步检查。保留上游解码器及其本地补丁，未移入旧 diskio、串口控制台、其他显示器和独立示例。

构建：

    cmake --preset Debug
    cmake --build --preset Debug -j 8
    cmake --preset Release
    cmake --build --preset Release -j 8

构建输出为 build/Debug 或 build/Release 中的 F429_Player_remake.elf。所有用户组件输出 .su 栈报告；Debug 已避免整块 music_window_t 清零引入约 17 KiB 的栈临时量。audio_task 自身栈帧 2400 B，music_task 1224 B；这不是完整调用链的最高栈占用。

当前链接结果：

| 配置 | SRAM | CCMRAM（LVGL＋RTOS） | Flash 地址跨度 |
| --- | ---: | ---: | ---: |
| Debug | 152,056 B | 24+40 KiB | 591,780 B |
| Release | 152,040 B | 24+40 KiB | 532,416 B |

Flash 数值来自实际 LOAD 段地址范围，含 4 字节段间对齐，不包含 BSS 或 CCMRAM 堆。

普通 SRAM 中 s_audio 为 24 KiB、s_memory 为 39 KiB、s_bounce 为 4 KiB，均已通过 ELF 符号地址确认处于 0x20000000～0x20030000；LVGL 24 KiB 对象池位于 CCMRAM 的 0x10000000，FreeRTOS 40 KiB 堆位于 0x10006000，均为 NOLOAD。music 任务的 16 KiB 栈及 TCB 静态放在普通 SRAM，保留原栈容量，净释放普通 SRAM 8096 B。

FreeRTOS 动态堆中的长期任务栈合计 29 KiB，包含 audio 8 KiB、LVGL 管理 4 KiB、日志 1 KiB 和内部 swdraw 绘制线程 16 KiB；不能漏计最后一项。启动任务另占 4 KiB，删除后等 Idle 回收。当前分配预算见 [heap_budget](../../tests/heap_budget/README.md)，板上通过启动和挂载后的 RTOS heap free/min 日志核对。

主机验证见 [测试说明](../../tests/README.md)。实际卡座插拔、SDIO 吞吐、DAC/耳放输出和墨水屏效果仍需上板验证。
