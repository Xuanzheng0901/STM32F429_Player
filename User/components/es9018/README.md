# ES9018K2M 驱动

依据同目录 `ES9018K2M.pdf`（v3.7，2021-04-22）编写，使用 STM32 HAL 阻塞式 I2C。
驱动已加入 `user_components` 构建；当前 `app_main()` 已调用初始化。

## 接口与硬件

- 默认使用 `hi2c1`，PB6=SCL、PB7=SDA，当前工程速率为 100 kHz。
- `ES9018_I2C_ADDRESS` 使用 **7 位地址**，默认 `0x48`（ADDR 接低）；ADDR 接高时改为 `0x49`。驱动内部左移一位传给 HAL，对应手册第 10 页的 `0x90` / `0x92`。
- I2S2：PB10=BCLK → DATA_CLK，PB12=LRCLK → DATA1，PC3=SD → DATA2。
- 默认匹配当前 I2S2：STM32 主发送，Philips I2S、16 位声道槽、约 44.1 kHz；DAC 为从机，关闭输入自动识别，启用正常 DPLL/ASRC。
- **DAC 的 XI/MCLK 必须有时钟**。当前 STM32 `MCLKOutput` 关闭，需由板上晶振或外部时钟提供。正常异步 PCM 要求 MCLK > 192 × 采样率；同步 128 × 模式需要另外配置寄存器 10（手册第 7、16 页）。驱动不会改动 STM32 时钟。
- 本板后级集成运放的静音引脚需上拉至高电平；该引脚悬空曾导致模拟输出随机缺波，上拉后已由实测确认恢复正常。
- 按手册第 27 页，上电保持 RESETB 为低，电源和外部 XI/MCLK 稳定至少 1 ms 后才能释放；后续硬复位低电平至少 10 ns。当前 RESETB 连接 PE5，初始化时拉低 100 tick、释放后等待 100 tick；上电时序仍需由板级电路或启动代码满足。

如需更换总线、地址或超时，可在编译时覆盖 `ES9018_I2C_HANDLE`、`ES9018_I2C_ADDRESS`、`ES9018_I2C_TIMEOUT_MS`。

## 调用示例

在 FreeRTOS 任务中、`MX_I2C1_Init()` 完成且上述电源/时钟条件满足后调用（硬复位等待使用 `vTaskDelay`）：

```c
#include "es9018.h"

HAL_StatusTypeDef status = ES9018_init();
if(status == HAL_OK)
    status = ES9018_set_volume(40); /* 左右声道均为 -20 dB */
```

`ES9018_init()` 硬件复位后，在静音状态下设置输入格式、音量渐变、从机模式、左右声道映射、DPLL、软启动和音量，最后解除静音。初始衰减为 0 dB。硬件复位后不再写软件复位位。

| 接口 | 含义 |
| --- | --- |
| `ES9018_set_volume(value)` | 两声道相同衰减，0=0 dB，1=-0.5 dB，255=-127.5 dB；数值越大声音越小，参数不是百分比 |
| `ES9018_set_mute(true/false)` | 两声道软静音/解除静音，保留原音量及滤波设置 |
| `ES9018_set_filter(filter)` | 快滚降、慢滚降、最小相位三种 PCM 滤波，保留静音及 IIR 设置 |
| `ES9018_set_format(format, word_length)` | I2S/左对齐，16/24/32 位声道槽，固定使用 PCM 输入 |
| `ES9018_read_register(reg, &value)` | 读单个 8 位寄存器，例如状态寄存器 `ES9018_REG_CHIP_STATUS` |
| `ES9018_write_register(reg, value)` | 写单个 8 位寄存器；调用方按手册保持保留位 |

格式变更时应先停音频传输，同时调整 STM32 I2S；DAC 的位宽对应**声道槽宽度**，例如 STM32 `I2S_DATAFORMAT_16B_EXTENDED` 对应 32 位槽。
寄存器 10 的 `stop_div` 设为 0，兼容 Rev.W 及超过 96 kHz 的 PCM（手册第 16 页）。

状态寄存器 `0x40`：bit0 为输入锁定，bit1 为自动静音；`(value & ES9018_STATUS_ID_MASK) == ES9018_STATUS_ID` 表示 ES9018K2M（手册第 24 页）。无 I2S 输入时未锁定属于正常情况，初始化不等待锁定。

寄存器访问与配置接口返回 `HAL_StatusTypeDef`，I2C 失败即记录操作、寄存器、HAL 状态和 I2C 错误码并返回原始错误；`0x01` 为 BERR 总线错误，`0x04` 为 AF 无应答，`0x20` 为超时，多个错误位可能组合。音量左右声道逐个写入，第二次写入失败时可能只更新左声道，调用方可重试。接口不适合在中断内调用；多任务调用需要外部互斥，特别是静音/滤波使用读改写。

驱动负责 DAC 寄存器配置，PCM 数据发送继续使用 I2S2 / DMA 接口。
