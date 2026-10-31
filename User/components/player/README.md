# 播放器

本模块包含播放控制与解码、曲库扫描和标签解析。music_library.c 管理隐藏路径索引和 15 首窗口，music_tags.c 解析 ID3、WAV INFO 和 FLAC Vorbis Comment；各文件保留独立职责，共享 music.h。

app_main 初始化日志、SSD1685 UI、ES9018K2M，再启动播放器。共享歌曲结构在 music.h，路径为 FatFs R0.16 UTF-8；UI 通过 player_request、player_toggle、player_set_repeat 发命令，播放器通过回调发布曲库窗口、播放状态和介质状态。

- music 任务：优先级 1、16 KiB 栈和 TCB 静态放在普通 SRAM。每 20 ms 采样卡检测，负责挂载、扫描、15 首窗口及 UI 通知。
- audio 任务：优先级 7、8 KiB 栈。独占当前 FIL、解码器和 I2S2；不获取 LVGL 锁。播放时由 DMA 半缓冲完成通知唤醒，停止或暂停时每 20 ms 检查卡状态。
- 两个 12 KiB 音频半缓冲、39 KiB 解码工作区、FIL 位于普通 SRAM。其余动态任务栈、队列和 FatFs LFN 临时区使用 CCMRAM 中的 40 KiB FreeRTOS 堆；LVGL 对象池独占 CCMRAM 的另外 24 KiB。SDIO 通过 SRAM 中转区访问 CCMRAM 数据。

LVGL 内部的 swdraw 线程保留 16 KiB 栈，不能只统计 music/audio/LVGL/log 四个应用任务。入口和曲库挂载完成后打印 RTOS 堆的当前及历史最小余量；启动任务被删除后，栈和 TCB 仍要等 Idle 运行才回收。
- 支持 WAV 16/24/32 位整数 PCM（含常见扩展 PCM）、MP3、FLAC 16/24/32 位，单声道扩为双声道。采样率按 audio_i2s.c 表配置，最高 96 kHz；32 位 FLAC 块长上限 4608 帧，其余位深受 39 KiB 工作区容量限制。
- 音频结束后停止；开启单曲循环则重播。上下首环绕切换，列表浏览不更改当前曲目，按编码器确认选曲；启动和重新插卡只选第一首，不自动播放。
- 起播先发送零数据并等待 DAC 锁定，锁定后补入歌曲开头。DMA 超时、补数迟到、文件错误会停止并关闭文件；保留必要的文件边界和 DMA 相位判断。

拔卡后，SDIO 锁存当前介质失效，快速重插也不能复用旧文件。音频任务停止 DMA 并关闭文件，music 任务等待停止确认后再卸载卷、清空队列和 UI。连续三次检测有卡后重新挂载、扫描；失败显示加载失败，重新插卡可再试，不做后台反复挂载。

硬件：I2S2 PB12/PB13/PB15，DAC I2C2 PB10/PB11；编码器 TIM3 PB4/PB5，确认键 PB3，卡检测 PA15 低有效。PB14 EAR_DET 低电平表示插入；PD9 MAX97220_MUTE 低电平静音、高电平启用。耳机拔出由 EXTI14 立即静音，音频任务做 30 ms 消抖；仅耳机已插入且音源正在播放时启用耳放。未实现电量采样，UI 显示 --。

构建：cmake --preset Debug，然后 cmake --build --preset Debug -j 8；Release 同理。主机测试见 tests/audio_player、tests/audio_io、tests/player_control、tests/fatfs_port 和 tests/music_ui。主机验证不能代替板上 SDIO 吞吐、插拔时序、显示及音频输出验证。

USB 音频由 usb_audio.c 负责，USB ISR 只写入 PCM FIFO、记录主机活动及流/静音状态；audio_dma.c 分派 I2S2 的半满、全满和错误回调，audio_output.c 管理耳机与耳放。I2S2 启停和 DAC 格式仍只由 audio 任务操作。

收到主机复位或 SOF 后切到 USB 模式，先静音并停止 SD 播放、关闭文件，再建立 USB I2S 输出；USB 模式下屏蔽 SD 播放控制。SD 卡仍可插拔、挂载和扫描，卡卸载的停止确认不影响 USB DMA。连续 500 ms 没有主机活动或收到断开回调后退出；无 VBUS 检测时主机挂起也会按断开处理。退出后从当前曲库窗口选一首并保持暂停，不自动恢复播放。

USB 描述符固定为 UAC1、48 kHz、16 位、双声道，adaptive OUT，不声明反馈端点。每包直接写 FIFO，缓冲为 2048 帧 FIFO（8192 B）和两个 192 帧 DMA 半缓冲（共 1536 B），启动等待 DAC 锁定和至少 384 帧数据；欠载补零并重新预缓冲。FIFO 积压时每次补数丢一帧补偿晶振漂移，此路径不保证逐样本无损。原 ST 类驱动大环形缓冲改为收包暂存（768 B），不再让短包累计偏移越界。USB 主机静音命令同时门控 PCM 和耳放。

未新增 RTOS 任务、动态队列或 RTOS 定时器；事件通过静态环形队列交给 music 任务，UI 使用 LVGL 定时器依次显示三秒的卡、USB 和耳机通知。耳机物理静音不等待 UI 通知。
