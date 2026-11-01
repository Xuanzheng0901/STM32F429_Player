# 音频输出

playback.h 提供选曲、暂停、循环、停止确认以及状态/事件读取接口；命令、状态、停止确认队列均为模块私有。自然结束仅发布 AUDIO_FINISHED，列表/循环/洗牌由 player 决定；状态携带命令编号，前端过滤过期快照。playback.c 内的 audio 任务优先级 7、栈 8 KiB，统一管理 SD/USB 音源切换、耳放门控和事件投递。复用原有任务和队列，不新增 RTOS 动态分配。

- audio_player.c：向 decoder/file_decoder.h 拉取 PCM，管理两个 24 KiB SRAM 半缓冲、I2S DMA 相位、DAC 锁定、播放进度和暂停。这里不解析文件、不调用 FatFs。
- audio_i2s.c：I2S2 采样率及 16/24/32 位槽配置。
- audio_dma.c：分派唯一 I2S DMA 接收者的半满、全满和错误事件；换源先停止旧 DMA。
- usb_audio.c：USB PCM FIFO、预缓冲、欠载补零和晶振漂移插值补偿。
- audio_output.c：PB14 耳机检测和 PD9 MAX97220 静音。

文件起播先送零数据建立 BCLK/LRCLK，等待 DAC 锁定后消耗 PCM。DMA 超时、补数迟到或解码错误时停止输出并关闭文件。音频任务不调用 UI、不持有 LVGL 锁；前端通过状态队列和静态事件环读取结果。

USB 收到主机复位或 SOF 后独占 I2S2，先停止并关闭 SD 文件。固定 UAC1、96 kHz/32 位整数 PCM 双声道、adaptive OUT；ISR 只收包及更新状态，不操作 I2S/I2C。描述符、PCM 和 UI 共用 `usb_audio_format.h`，每帧为小端左右两个 int32_t，I2S 按左高半字/左低半字/右高半字/右低半字发送，不截成 16 位。

每毫秒标准 USB 包为 96 帧/768 B，端点最大包为 97 帧/776 B，支持 95/96/97 帧变化。Full Speed 单个等时包上限 1023 B，96k/32b 双声道在此范围内，保留 UAC1。OTG FS 的 320 word 硬件 FIFO 改为 RX 256 + EP0 TX 64，不再给仅 OUT 的 EP1 分配 TX；原 RX 128 word 不能容纳新包。类驱动只用一个 776 B 缓冲暂存当前包，不复制四包环形缓冲。

软件 FIFO 2048 帧（16384 B），DMA 两个 384 帧半缓冲（合计 6144 B），均在普通 SRAM。半缓冲仍为 4 ms；DAC 锁定且积累至少 768 帧后输出。积压超过 1152 帧时用 64 位中间量把 385 帧插值成 384 帧补偿漂移，不截断为 16 位，但插值路径不保证逐样本无损。转换及插值在临界区外，FIFO 槽位到转换完成后才释放；USB alt 切换用流编号丢弃旧流结果，避免重置 FIFO 后提交错误读位置。主机静音同时门控 PCM 和耳放。

`usb_audio.c` 单独启用 -O2，额外静态 SRAM 约 12.5 KiB，RTOS 动态堆不变。Release 普通 SRAM 剩余 9120 B。主机验证覆盖描述符、完整 32 位半字顺序、满幅正负插值、短包/长包/非对齐输入、溢出和欠载、流重启竞态，以及 400 秒模拟漂移。真实主机枚举、USB 丢包、DAC 输出及 4 ms 补数余量仍需上板验证。

刷入后重新插拔 USB，在电脑播放设备中选择双声道 32 位/96000 Hz；当前只声明此固定格式，不提供 48k/16b 备用 alternate setting。

规范参考：[USB Audio Data Formats 1.0](https://www.usb.org/sites/default/files/frmts10.pdf)、[Full Speed 等时带宽](https://source.android.com/docs/core/audio/usb)、[STM32F429 数据手册](https://www.st.com/resource/en/datasheet/stm32f429ie.pdf)。

连续 500 ms 无主机活动或收到断开回调后退出 USB。PA9 为串口 TX、未启用 VBUS 检测，因此主机挂起也按断开处理。USB 期间 SD 仍可挂载/卸载，SD 控制不能抢占输出。

PB14 低电平插入，插入消抖 30 ms；拔出由 EXTI14 立即拉低 PD9 静音，不等待 SD 读文件或 UI。耳机稳定插入且播放有效时才拉高 PD9。实际电气时序与模拟声音仍需上板验证。

playback_stop 返回 bool：只有当前停止命令得到确认才返回 true，最多等待 2 秒。调用方收到 false 必须保留卷和文件系统对象，不能继续卸载。确认队列存放命令编号，避免旧确认误用。

96 kHz、24/32 位采用 32 位槽，半缓冲为 3072 个双声道帧，DMA 补数预算 32 ms；原 12 KiB 半缓冲仅有 1536 帧/16 ms，容易碰到 FLAC 整块解码峰值。文件输出单独使用 -O2 优化 PCM 半字转换，Debug 其余模块配置不变。DMA 边沿/相位和补数截止检查继续保留，超时日志额外给出 decode/read 用时。缓冲固定 48 KiB，16 位文件的半缓冲帧数为 6144，因此低采样率下状态更新和锁定轮询的时间粒度相应变长；控制命令仍用任务通知唤醒。

主机测试用 20 秒的 96k/24b FLAC，在已播放 15 秒后注入一次 20 ms 读卡延迟，完整逐样本比对输出；超过新预算的 80 ms 读卡延迟仍触发错误。此测试不证明真实 SDIO 吞吐和板上该曲目的长期播放能力。
