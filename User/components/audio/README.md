# 音频输出

playback.h 提供选曲、暂停、循环、停止确认以及状态/事件读取接口；命令、状态、停止确认队列均为模块私有。自然结束仅发布 AUDIO_FINISHED，列表/循环/洗牌由 player 决定；状态携带命令编号，前端过滤过期快照。playback.c 内的 audio 任务优先级 7、栈 8 KiB，统一管理 SD/USB 音源切换、耳放门控和事件投递。复用原有任务和队列，不新增 RTOS 动态分配。

- audio_player.c：向 decoder/file_decoder.h 拉取 PCM，管理两个 12 KiB SRAM 半缓冲、I2S DMA 相位、DAC 锁定、播放进度和暂停。这里不解析文件、不调用 FatFs。
- audio_i2s.c：I2S2 采样率及 16/24/32 位槽配置。
- audio_dma.c：分派唯一 I2S DMA 接收者的半满、全满和错误事件；换源先停止旧 DMA。
- usb_audio.c：USB PCM FIFO、预缓冲、欠载补零和晶振漂移插值补偿。
- audio_output.c：PB14 耳机检测和 PD9 MAX97220 静音。

文件起播先送零数据建立 BCLK/LRCLK，等待 DAC 锁定后消耗 PCM。DMA 超时、补数迟到或解码错误时停止输出并关闭文件。音频任务不调用 UI、不持有 LVGL 锁；前端通过状态队列和静态事件环读取结果。

USB 收到主机复位或 SOF 后独占 I2S2，先停止并关闭 SD 文件。固定 UAC1、48 kHz/16 位双声道、adaptive OUT；ISR 只收包及更新状态，不操作 I2S/I2C。FIFO 2048 帧（8192 B），DMA 两个 192 帧半缓冲（1536 B），均在普通 SRAM。DAC 锁定且积累至少 384 帧后输出；积压时线性插值将 193 帧压成 192 帧补偿漂移，此路径不保证逐样本无损。主机静音同时门控 PCM 和耳放。

连续 500 ms 无主机活动或收到断开回调后退出 USB。PA9 为串口 TX、未启用 VBUS 检测，因此主机挂起也按断开处理。USB 期间 SD 仍可挂载/卸载，SD 控制不能抢占输出。

PB14 低电平插入，插入消抖 30 ms；拔出由 EXTI14 立即拉低 PD9 静音，不等待 SD 读文件或 UI。耳机稳定插入且播放有效时才拉高 PD9。实际电气时序与模拟声音仍需上板验证。

playback_stop 返回 bool：只有当前停止命令得到确认才返回 true，最多等待 2 秒。调用方收到 false 必须保留卷和文件系统对象，不能继续卸载。确认队列存放命令编号，避免旧确认误用。
