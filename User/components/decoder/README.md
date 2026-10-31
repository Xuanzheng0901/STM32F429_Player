# 文件解码

file_decoder.h 是统一接口：open 打开文件并填写实际格式，read 输出双声道交错 PCM 和有效帧数，close 释放解码器及 FIL。单实例，仅由音频任务调用。

16 位输出为 int16_t，24/32 位输出为左对齐 int32_t；单声道复制为双声道。调用者提供四字节对齐缓冲，容量至少为 frames × 4 或 frames × 8 字节。短读表示文件结尾；错误返回 false。模块不操作 I2S、DMA、DAC、GPIO 或 UI，不创建任务和队列；输出模块决定硬件支持的采样率及发送半字顺序。

- file_decoder.c：FIL 生命周期、格式分派、WAV 整数 PCM / 扩展 PCM 解析与展开。
- mp3_decoder.c / minimp3：MP3 解码。
- flac_decoder.c / dr_flac：FLAC 16/24/32 位解码。
- decoder_memory.c：MP3/FLAC 共用的 39 KiB 普通 SRAM 工作区。

FLAC 32 位块长上限 4608 帧，其余位深同样受工作区容量限制。输出能力由 audio_i2s.c 配置，目前最高 96 kHz。上游解码器及本地补丁记录随目录保留。tests/audio_player 使用真实文件和独立 PCM 参考逐样本核对，同时验证输出层的 I2S 半字转换。
