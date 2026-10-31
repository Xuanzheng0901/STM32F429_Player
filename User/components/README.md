# F429 播放器应用

从 F407 迁移的应用已经接入 StartDefaultTask → app_main，当前固定使用 SSD1685、TIM3 编码器、I2S2 和 ES9018K2M。F407 工程保持原状。

| 组件 | 职责 |
| --- | --- |
| main | 初始化模块、连接 UI 回调 |
| UI | 绘制 360×184 播放器、处理编码器操作；只发送控制命令 |
| player | 曲库和音频两个任务、命令合并、状态发布、卸载前的停止确认 |
| sdcard | 私有 FATFS 卷、挂载/卸载、插拔事件 |
| library | 递归扫描、隐藏路径索引、15 首窗口 |
| metadata | ID3、WAV INFO、FLAC Vorbis Comment，显示文本统一 UTF-8 |
| fatfs | FatFs R0.16、FreeRTOS 锁和内存、diskio 返回值转换 |
| sdmmc | PA15 检测、介质失效锁存、SDIO 初始化、DMA 和 SRAM 中转 |
| display / es9018 / log | 当前 F429 硬件适配 |

共享数据在 player/music.h，UTF-8 路径容量 1024 字节。扫描根目录及最多四层子目录；每条索引固定 1024 字节，不在 RAM 中保存整库。显示文本截断保留完整 UTF-8 字符。

卡检测由曲库任务每 20 ms 轮询，连续三次有卡后挂载和重建索引。开机无卡显示未插卡，插入后自动扫描；拔卡时 SDIO 立即锁存介质失效，音频任务停止 DMA、关闭文件，曲库任务等到停止确认后卸载并清空 UI。重新插入选择第一首但不自动播放。挂载失败不后台反复重试，再次插卡时重新尝试。

控制、补数和 UI 更新分别处理；音频任务不持有 LVGL 锁。只保留必要的文件边界、格式能力、DMA 地址/相位和停止同步检查。保留上游解码器及其本地补丁，未移入旧 diskio、串口控制台、其他显示器和独立示例。

构建：

    cmake --preset Debug
    cmake --build --preset Debug -j 8
    cmake --preset Release
    cmake --build --preset Release -j 8

构建输出为 build/Debug 或 build/Release 中的 F429_Player_remake.elf。所有用户组件输出 .su 栈报告；Debug 已避免整块 music_window_t 清零引入约 17 KiB 的栈临时量。audio_task 自身栈帧 2400 B，music_task 1224 B；这不是完整调用链的最高栈占用。

当前链接结果：

| 配置 | SRAM | CCMRAM 预留堆 | Flash 地址跨度 |
| --- | ---: | ---: | ---: |
| Debug | 160,152 B | 65,536 B | 586,676 B |
| Release | 160,144 B | 65,536 B | 527,292 B |

Flash 数值来自实际 LOAD 段地址范围，含 4 字节段间对齐，不包含 BSS 或 CCMRAM 堆。

普通 SRAM 中 s_audio 为 24 KiB、s_memory 为 39 KiB、s_bounce 为 4 KiB，均已通过 ELF 符号地址确认处于 0x20000000～0x20030000；64 KiB FreeRTOS 堆在 0x10000000 的 CCMRAM。CCMRAM 的链接占用是预留堆容量，不是任务已用满堆。

主机验证见 [测试说明](../../tests/README.md)。实际卡座插拔、SDIO 吞吐、DAC/耳放输出和墨水屏效果仍需上板验证。
