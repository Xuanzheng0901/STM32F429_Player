# 外设配置

工程：F429_Player_remake；芯片：STM32F429VIT6，LQFP100。

配置日期：2026-10-28。STM32CubeMX：6.16.1。STM32Cube FW_F4：V1.28.3。

## 时钟

| 项目 | 配置 |
|---|---|
| HSE | 8 MHz 外部晶振，PH0/PH1 |
| 主 PLL | M=8，N=336，P=2，Q=7 |
| SYSCLK / HCLK | 168 MHz |
| APB1 / APB1 定时器 | 42 MHz / 84 MHz |
| APB2 / APB2 定时器 | 84 MHz / 168 MHz |
| USB / SDIO 时钟域 | 48 MHz |
| PLLI2S | N=258，R=3，输出 86 MHz |
| HAL 时基 | TIM14，1 ms |
| RTOS Tick | SysTick，1000 Hz |

## 内存与 FreeRTOS

| 区域 | 起始地址 | 容量 | 用途 |
|---|---|---|---|
| Flash | 0x08000000 | 2048 KiB | 程序与常量 |
| SRAM | 0x20000000 | 192 KiB | 全局数据、主栈与外设缓冲 |
| CCMRAM | 0x10000000 | 64 KiB | FreeRTOS 动态堆 |

FreeRTOS 使用 `heap_4.c`，堆大小为 65536 字节。动态创建的任务栈、任务控制块、队列和信号量从该堆分配。

堆迁移涉及三个文件：

- `Core/Inc/FreeRTOSConfig.h`：设置 `configAPPLICATION_ALLOCATED_HEAP=1`。
- `Core/Src/freertos.c`：定义 `ucHeap`，放入 `.freertos_heap` 段。
- `STM32F429XX_FLASH.ld`：将 `.freertos_heap (NOLOAD)` 映射至 CCMRAM，并按 8 字节对齐。

```c
uint8_t ucHeap[configTOTAL_HEAP_SIZE] __attribute__((section(".freertos_heap")));
```

```ld
.freertos_heap (NOLOAD):
{
  . = ALIGN(8);
  *(.freertos_heap)
  . = ALIGN(8);
} >CCMRAM
```

`ucHeap` 地址为 `0x10000000`，大小为 `0x10000`；`.freertos_heap` 为 NOBITS 段。

默认任务为 `defaultTask`，优先级 Normal，栈大小 4096 字节。默认任务初始化 USB Device，随后以 1 ms 间隔休眠。

## SPI1：墨水屏

主机发送，8 bit，Mode 0，MSB First，软件 NSS。APB2 四分频，SCK 为 21 MHz。TX 使用普通模式 DMA。

| 引脚 | 封装脚号 | 网络 | GPIO 配置 |
|---|---:|---|---|
| PA5 | 30 | SPI1_SCK / EPD_CLK | AF5，推挽，无上下拉，Very High |
| PA7 | 32 | SPI1_MOSI / EPD_DIN | AF5，推挽，无上下拉，Very High |
| PC4 | 33 | EPD_DC | 推挽输出，Low，初始低电平 |
| PC5 | 34 | EPD_RST | 推挽输出，Low，初始低电平 |
| PB0 | 35 | EPD_BUSY | 输入，上拉 |

## I2C2：ES9018K2M 控制

100 kHz，7 位地址，Duty Cycle 2。模拟滤波开启，数字滤波为 0。

| 引脚 | 封装脚号 | 网络 | GPIO 配置 |
|---|---:|---|---|
| PB10 | 47 | I2C2_SCL | AF4，开漏，上拉，Very High |
| PB11 | 48 | I2C2_SDA | AF4，开漏，上拉，Very High |

## I2S2：音频数据

主机发送，Philips 标准，16 bit，标称采样率 48 kHz，CPOL Low，MCLK 输出关闭，半双工。PLLI2S 输出为 86 MHz，当前分频计算采样率为 47,991.07 Hz。TX 使用半字对齐、循环模式 DMA，硬件优先级 Very High。

| 引脚 | 封装脚号 | 网络 | GPIO 配置 |
|---|---:|---|---|
| PB12 | 51 | I2S2_WS / LRCK | AF5，推挽，无上下拉，Low |
| PB13 | 52 | I2S2_CK / BCLK | AF5，推挽，无上下拉，Low |
| PB15 | 54 | I2S2_SD / DATA | AF5，推挽，无上下拉，Low |
| PD8 | 55 | ES9018_RST | 推挽输出，Low，初始低电平 |
| PD9 | 56 | MAX97220_MUTE | 推挽输出，Low，初始低电平 |
| PB14 | 53 | EAR_DET | 输入，上拉 |

## SDIO：SD 卡

上升沿采样，ClockDiv=0，传输时钟 24 MHz，关闭时钟旁路、时钟节能与硬件流控。初始化使用 1 bit 总线，BSP 初始化完成后切换为 4 bit。DMA 使用外设流控、字对齐、FIFO Full、INC4 突发。

| 引脚 | 封装脚号 | 网络 | GPIO 配置 |
|---|---:|---|---|
| PC8 | 65 | SDIO_D0 | AF12，推挽，上拉，Very High |
| PC9 | 66 | SDIO_D1 | AF12，推挽，上拉，Very High |
| PC10 | 78 | SDIO_D2 | AF12，推挽，上拉，Very High |
| PC11 | 79 | SDIO_D3 | AF12，推挽，上拉，Very High |
| PC12 | 80 | SDIO_CLK | AF12，推挽，上拉，Very High |
| PD2 | 83 | SDIO_CMD | AF12，推挽，上拉，Very High |
| PA15 | 77 | SDIO_CD | 输入，上拉，低电平表示插卡 |

FatFs 配置：代码页 936，长文件名模式 3，UTF-16 `TCHAR`，文件锁数量 8，启用互斥与 `chmod/utime`，FAT12/FAT16/FAT32。

## TIM3：旋钮编码器

编码器模式 TI12，四倍频；16 位向上计数，Prescaler=0，ARR=65535，ClockDivision=1。两个输入均为上升沿极性、Direct TI、输入分频 1、滤波 0。

| 引脚 | 封装脚号 | 网络 | GPIO 配置 |
|---|---:|---|---|
| PB4 | 90 | TIM3_CH1 | AF2，上拉，Low |
| PB5 | 91 | TIM3_CH2 | AF2，上拉，Low |
| PB3 | 89 | KNOB_BUTTON | 输入，上拉 |

## 按键

所有按键配置为普通 GPIO 输入、内部上拉。

| 引脚 | 封装脚号 | 网络 |
|---|---:|---|
| PD10 | 57 | BUTTON_SIDE1 |
| PD11 | 58 | BUTTON_SIDE2 |
| PD12 | 59 | BUTTON_SIDE3 |
| PD13 | 60 | BUTTON_UP1 |
| PD14 | 61 | BUTTON_UP2 |
| PD15 | 62 | BUTTON_UP3 |
| PC6 | 63 | BUTTON_UP4 |
| PC7 | 64 | BUTTON_UP5 |

## ADC1：模拟采样

12 bit，通道 9，软件触发单次转换，右对齐，采样时间 480 cycles。ADC 时钟为 APB2/4=21 MHz，EOC 为单次转换完成，使用轮询接口。

| 引脚 | 封装脚号 | 网络 | GPIO 配置 |
|---|---:|---|---|
| PB1 | 36 | ADC1_IN9 | 模拟输入，无上下拉 |

## USART1：调试串口

115200 baud，8N1，收发模式，16 倍过采样，无硬件流控。RX/TX 均配置普通模式 DMA，字节对齐。

| 引脚 | 封装脚号 | 网络 | GPIO 配置 |
|---|---:|---|---|
| PA9 | 68 | USART1_TX | AF7，推挽，无上下拉，Very High |
| PA10 | 69 | USART1_RX | AF7，推挽，无上下拉，Very High |

## USB OTG FS：USB Audio

Device 模式，内置 PHY，全速 12 Mbit/s。USB Audio 数据流为 48 kHz、双声道、16 bit，每毫秒 192 字节。VBUS sensing、SOF 中断和低功耗模式关闭。

| 引脚 | 封装脚号 | 网络 |
|---|---:|---|
| PA11 | 70 | USB_DM |
| PA12 | 71 | USB_DP |

## 调试与晶振

| 引脚 | 封装脚号 | 功能 |
|---|---:|---|
| PA13 | 72 | SWDIO |
| PA14 | 76 | SWCLK |
| PH0/OSC_IN | 12 | HSE OSC_IN |
| PH1/OSC_OUT | 13 | HSE OSC_OUT |

## DMA 与中断

NVIC 分组为 Priority Group 4，子优先级均为 0。

| 请求 | DMA Stream / Channel | 模式 | 数据宽度 | 硬件优先级 | IRQ 优先级 |
|---|---|---|---|---|---|
| I2S2_TX | DMA1 Stream4 / Channel0 | Circular | Halfword | Very High | 5 |
| USART1_RX | DMA2 Stream2 / Channel4 | Normal | Byte | Low | 5 |
| SPI1_TX | DMA2 Stream3 / Channel3 | Normal | Byte | Low | 5 |
| SDIO_RX/TX | DMA2 Stream6 / Channel4 | Peripheral Flow Control | Word | Low | 5 |
| USART1_TX | DMA2 Stream7 / Channel4 | Normal | Byte | Low | 5 |

| 中断 | 优先级 |
|---|---:|
| SPI1、SPI2、SDIO、USART1、OTG_FS | 5 |
| TIM8_TRG_COM_TIM14 | 15 |
| SysTick、PendSV | 15 |

## 本版外置存储

本版使用 SD 卡，外置 Flash 不装配，SPI4 关闭。PE2、PE4、PE5、PE6 为空闲引脚。

## 构建

```powershell
cmake --preset Debug
cmake --build --preset Debug -j 8
cmake --preset Release
cmake --build --preset Release -j 8
```

CCMRAM 堆迁移后的链接结果：

| 配置 | SRAM | CCMRAM | Flash |
|---|---:|---:|---:|
| Debug | 24,400 B | 65,536 B | 67,636 B |
| Release | 24,400 B | 65,536 B | 35,644 B |
