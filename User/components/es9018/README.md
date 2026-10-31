# ES9018K2M

F429 使用 I2C2 PB10/PB11，7 位地址 0x48；硬件复位 PD8，初始化低电平 100 tick、释放后等待 100 tick。I2S2 PB12/PB13/PB15 发送 Philips 立体声，不输出 MCLK，DAC 工作于 PCM 从机模式。

在任务中调用 ES9018_init，再调用 ES9018_set_volume。音量参数是半 dB 衰减原始值，70 表示 -35 dB，不是百分比。初始化先静音配置寄存器，最后解除静音。

- ES9018_set_format：I2S / 左对齐，16/24/32 位槽宽；切换前停止 I2S。STM32 24 位格式对应 DAC 的 32 位槽。
- ES9018_set_volume：两个声道设置相同衰减。
- ES9018_set_mute：DAC 软静音，不操作耳放 GPIO。
- ES9018_set_filter：快滚降、慢滚降或最小相位。
- ES9018_is_locked：读取状态寄存器 0x40 的 bit0；通信失败返回 false。

设置接口为 void，I2C 失败由驱动日志记录。音频任务先发送零数据，等 is_locked 返回 true 才消耗歌曲，约 1 秒仍未锁定则停止。初始化阶段没有音频时未锁定属于正常情况。

耳放由 audio/audio_output.c 控制：PB14 EAR_DET 低电平插入，PD9 MAX97220_MUTE 低电平静音。拔出中断立即静音，插入消抖后仅在有效播放时启用；模拟输出仍需上板验证。

寄存器依据见同目录 ES9018K2M.pdf，任务串行使用本驱动。
