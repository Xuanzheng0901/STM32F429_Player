# F429 墨水屏音乐播放器

基于 STM32F429VIT6 的便携音乐播放器，支持 SD 卡本地播放和 USB 声卡模式。工程包含播放器固件、板卡设计和主机测试。

## 主要功能

- **SD 卡播放**：播放 WAV、MP3 和 FLAC，插卡自动扫描曲库并读取歌曲标签。支持列表循环、单曲循环和整轮不重复的随机播放。
- **USB 声卡**：连接 USB 主机后切换到 USB Audio 模式，接收固定 96 kHz / 32 位双声道整数 PCM。
- **墨水屏界面**：显示曲库、歌曲信息、播放进度、电池电压和估算电量，并提示 SD 卡、USB 和耳机的插拔状态。
- **实体操作**：旋转编码器浏览曲库，按下确认。顶部五键对应屏幕控件，侧边上下键调节音量，中键短按切换静音、长按全刷。拔出耳机时自动静音耳放。

## 硬件与软件

| 项目 | 配置 |
| --- | --- |
| 主控 | STM32F429VIT6 |
| 显示 | 360 × 184 黑白墨水屏，SSD1685 驱动，LVGL 界面 |
| 音频 | I²S 输出，ES9018K2M DAC，MAX97220 耳放 |
| 存储与连接 | SDIO、FatFs R0.16、USB Full Speed（UAC1） |
| 运行环境 | FreeRTOS、STM32 HAL |

板卡设计见 [Hardware](Hardware)，引脚、时钟和内存布局见 [外设配置](PERIPHERALS.md)。

## 构建固件

准备 CMake 3.22 或更高版本、Ninja 和 Arm GNU Toolchain，并将工具链加入 `PATH`。在项目根目录执行：

```sh
git submodule update --init --recursive
cmake --preset Release
cmake --build --preset Release
```

固件位于 `build/Release/F429_Player_remake.elf`。也可使用 `Debug` 预设调试，或使用 `Optimized` 预设为音频热点启用 `-O3`，其余代码使用 `-Os`。

## 工程文档

- [播放器与曲库](User/components/player/README.md) · [文件解码](User/components/decoder/README.md) · [音频输出与 USB](User/components/audio/README.md)
- [显示与按键](User/components/display/README.md) · [SD 卡与 FatFs](User/components/fatfs/README.md) · [电池检测](User/components/battery/README.md)
- [主机测试](tests/README.md)
