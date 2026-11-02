# F429 播放器应用

从 F407 迁移的应用已经接入 StartDefaultTask → app_main，当前固定使用 SSD1685、TIM3 实体编码器、I2S2 和 ES9018K2M。F407 工程保持原状。

| 组件 | 职责 |
| --- | --- |
| main | 初始化模块、连接 UI 回调 |
| UI | 绘制 360×184 播放器、处理编码器操作；只发送控制命令 |
| player | 前端命令、曲库和标签、卡挂载流程及 UI 事件转发 |
| decoder | 文件打开/关闭、WAV/MP3/FLAC 解码和标准双声道 PCM |
| audio | 音频任务、SD/USB 仲裁、PCM 缓冲、I2S/DMA、DAC 格式和耳放静音 |
| battery | 每 30 秒读取 ADC1/PB1，平均、还原电池电压并估算电量，通过回调发布 |
| sdcard | 私有 FATFS 卷、挂载/卸载、插拔事件 |
| fatfs | FatFs R0.16、FreeRTOS 锁和内存、磁盘适配、SDIO 初始化与 DMA、卡检测及介质失效锁存 |
| display / es9018 / log | 当前 F429 硬件适配 |

UI 使用左侧五行歌单、右上方五个控制图标、右侧歌曲信息及进度、底部状态栏的布局。歌单高亮固定在中间行，浏览不改变右侧当前曲目信息；控制图标依次为循环、上一首、播放/暂停、下一首、返回歌单。唱片为 67×67 的 I1 资源，图标在解码时逐行转换为 ARGB8888，屏幕缓冲保持 I1。

player/music_library.c 负责递归扫描、隐藏索引和 15 首窗口，player/music_tags.c 负责 ID3、WAV INFO 和 FLAC Vorbis Comment；fatfs/sdmmc.c 负责块设备通信。共享数据在 player/music.h，UTF-8 路径容量 1024 字节。扫描根目录及最多四层子目录；每条索引为完整 music_info_t（当前 1168 字节，包含路径及建库时读取的标签），不在 RAM 中保存整库。显示文本截断保留完整 UTF-8 字符。

卡检测由曲库任务每 20 ms 轮询，连续三次有卡后挂载和重建索引。开机无卡显示未插卡，插入后自动扫描；拔卡时 SDIO 立即锁存介质失效，音频任务停止 DMA、关闭文件，曲库任务等到停止确认后卸载并清空 UI。重新插入选择第一首但不自动播放。挂载失败不后台反复重试，再次插卡时重新尝试。

控制、补数和 UI 更新分别处理；音频任务不持有 LVGL 锁。只保留必要的文件边界、格式能力、DMA 地址/相位和停止同步检查。保留上游解码器及其本地补丁，未移入旧 diskio、串口控制台、其他显示器和独立示例。

构建：

    cmake --preset Debug
    cmake --build --preset Debug -j 8
    cmake --preset Release
    cmake --build --preset Release -j 8
    cmake --preset Optimized
    cmake --build --preset Optimized -j 8

构建输出为 build/对应预设/F429_Player_remake.elf。所有用户组件输出 .su 栈报告；Debug 已避免整块 music_window_t 清零引入约 17 KiB 的栈临时量。audio_task 自身栈帧 2408 B，music_task 1248 B；这不是完整调用链的最高栈占用。

`Optimized` 继承 Release，通过 `PLAYER_AUDIO_O3=ON` 只将以下计算热点设为 `-O3`，
其余代码和汇编使用 `-Os`。Debug、Release 保留原来的优化配置。

| `-O3` 源文件 | 用途 |
| --- | --- |
| decoder/flac_decoder.c | FLAC 帧解码 |
| decoder/mp3_decoder.c | MP3 帧解码 |
| decoder/file_decoder.c | WAV/PCM 读取、格式转换 |
| audio/audio_player.c | 文件 PCM 转换、DMA 补数 |
| audio/usb_audio.c | USB 32 位 PCM、漂移插值、DMA 补数 |

保留解码器的 `-fno-strict-aliasing` 和所有组件的栈报告，不启用 `-ffast-math` 或 LTO。
已核对 compile_commands.json：新预设 5 个编译单元为 `-O3`，其余 207 个为 `-Os`。
主机 `-O3` 的 28 项音频和 6 项 USB/耳机测试通过；实际 MCU 性能仍需上板测量。

当前链接结果：

| 配置 | SRAM | CCMRAM（LVGL＋RTOS） | Flash 地址跨度 |
| --- | ---: | ---: | ---: |
| Debug | 187,560 B | 24+40 KiB | 925,512 B |
| Release | 187,560 B | 24+40 KiB | 862,812 B |
| Optimized | 187,560 B | 24+40 KiB | 875,048 B |

Flash 数值来自实际 LOAD 段地址范围，含 4 字节段间对齐，不包含 BSS 或 CCMRAM 堆。

UI 的 Fusion Pixel 12px 字库由 5534 个字符扩充到 18570 个，基本区汉字由
5200 个扩充到 17637 个，额外占用约 313 KiB Flash，静态 RAM 配额不变。
生成命令、范围和源字体的覆盖限制见 [字体说明](display/README.md)。

普通 SRAM 中 s_audio 为 48 KiB、s_memory 为 39 KiB、s_bounce 为 4 KiB，均已通过 ELF 符号地址确认处于 0x20000000～0x20030000；LVGL 24 KiB 对象池位于 CCMRAM 的 0x10000000，FreeRTOS 40 KiB 堆位于 0x10006000，均为 NOLOAD。music 任务的 16 KiB 栈及 TCB 静态放在普通 SRAM，保留原栈容量，当时净释放普通 SRAM 8096 B。

FreeRTOS 动态堆中的长期任务栈合计 29 KiB，包含 audio 8 KiB、LVGL 管理 4 KiB、日志 1 KiB 和内部 swdraw 绘制线程 16 KiB；不能漏计最后一项。启动任务另占 4 KiB，删除后等 Idle 回收。当前分配预算见 [heap_budget](../../tests/heap_budget/README.md)，板上通过启动和挂载后的 RTOS heap free/min 日志核对。

主机验证见 [测试说明](../../tests/README.md)。实际卡座插拔、SDIO 吞吐、DAC/耳放输出和墨水屏效果仍需上板验证。

UI 对插拔 SD 卡、USB 主机连接/断开、耳机插拔依次显示三秒提示。USB 收到主机复位或 SOF 时独占 I2S2，固定 96 kHz/32 位整数 PCM 双声道；连续 500 ms 无总线活动退出，主机挂起也按断开处理。SD 卡可在 USB 模式继续挂载/卸载，不能抢占音频输出；退出 USB 后选曲保持暂停。PB14 低电平插入，PD9 低电平静音，拔出由 EXTI14 立即静音，插入消抖 30 ms。实现和缓冲策略见 [音频输出说明](audio/README.md)。

USB 模块未增加 RTOS 动态分配。USB FIFO 为 16384 B、DMA 为 6144 B，均在普通 SRAM；ST USB 类接收缓冲为单个最大包 776 B。96k/32b 相比原 48k/16b 额外占用约 12.5 KiB SRAM，Release 链接后普通 SRAM 余量 9048 B。

模块边界：UI → player → audio/playback → audio_player → decoder/file_decoder → FatFs；USB 直接向 audio/usb_audio 写 PCM。解码层不依赖音频硬件，player 不直接接触 DMA 或播放中的 FIL。

播放完成由 player_callbacks_t.finished 通知，player 负责列表环绕、单曲循环和整轮不重复洗牌；UI 通过三种黑白图标显示播放模式。播放策略不再放在音频任务内。

上方五键按物理位置对应 UI 按钮：UP1～5 为列表、下一首、播放/暂停、上一首、模式。
实体编码器 TIM3/PB3 负责浏览和确认，已删除侧键模拟编码器的代码。侧边 SIDE1（下）
减小音量、SIDE3（上）增大音量，每次寄存器变化 8（实际 4 dB），SIDE2（中）短按切换静音、长按 800 ms 全刷一次。
音量继承启动值，在 64～192 限幅；SD/USB 共用，I2C 写入统一由音频任务执行。
完成音量设置后由曲库任务通知 UI，右侧留白显示无文字音量条，最后一次调节后 2 秒
隐藏；静音显示空条，连按重置计时，不新增 RTOS 队列或任务。
寄存器已为 192 时再减音量进入静音；静音时加音量取消静音并增加一档。
中键切换静音保留衰减值，长按松开不触发短按，全刷后恢复原刷新模式。
输入映射和定时器见 [输入说明](display/README.md)。

曲库索引在建库后持续只读打开，卸载时关闭。翻页直接读取缓存的路径与标签，不再对 15 首音乐逐一打开、查目录和跳转解析标签；FatFs 锁超时会记录持锁任务及持续时间。

文件播放半缓冲扩为 24 KiB，96 kHz 宽格式的补数预算从 16 ms 增至 32 ms；文件音频输出单独启用 -O2。新增约 24 KiB 普通 SRAM 占用，RTOS/LVGL 的 CCMRAM 配额和任务栈容量不变。

电池任务使用 2 KiB 静态栈，不增加 RTOS 动态堆分配。上电及每 30 秒读取 16 次 ADC，
使用 3.3 V 参考和二分之一分压还原电压，按 ADI 典型单节锂电池表插值估算百分比。
UI 底栏显示电压、百分比及电池填充条，细节和表格来源见 [电池说明](battery/README.md)。
