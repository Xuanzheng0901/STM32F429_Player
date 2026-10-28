# F429VIT6 播放器硬件引脚分配

更新：2026-10-27。芯片：STM32F429VIT6，LQFP100。

## 1. 外置 Flash：SPI4

| STM32 引脚 | 封装脚号 | 功能 | 初始化方式 |
|---|---:|---|---|
| PE2 | 1 | SPI4_SCK | AF5，SPI 时钟 |
| PE4 | 3 | SPI4_CS | GPIO 推挽输出，初始高电平 |
| PE5 | 4 | SPI4_MISO | AF5，来自 Flash SO/DO |
| PE6 | 5 | SPI4_MOSI | AF5，发往 Flash SI/DI |

SPI4：主机、8 bit、Mode 0、软件 NSS，当前时钟 42 MHz，无 DMA。Flash 驱动需要用 PE4 将每条命令的命令/地址/数据包在同一次片选中，不能依靠 SPI 外设使能状态划分命令。实际速率和指令集待 Flash 型号确定后验证。CS 应有外部上拉，保证 MCU 复位期间 Flash 未选中。

## 2. ES9018K2M 和耳机输出

| STM32 引脚 | 封装脚号 | 功能 | 初始化方式 |
|---|---:|---|---|
| PB10 | 47 | I2C2_SCL | AF4 开漏，100 kHz，外部上拉 |
| PB11 | 48 | I2C2_SDA | AF4 开漏，外部上拉 |
| PB12 | 51 | I2S2_WS / LRCK | AF5，接 ES9018 DATA1 |
| PB13 | 52 | I2S2_CK / BCLK | AF5，接 ES9018 DATA_CLK |
| PB14 | 53 | EAR_DET | GPIO 输入，无内部上下拉 |
| PB15 | 54 | I2S2_SD / DATA | AF5，接 ES9018 DATA2 |
| PD8 | 55 | ES9018_RST | GPIO 输出，初始低电平，保持 RESETB 有效 |
| PD9 | 56 | MAX97220_MUTE | GPIO 输出，保留已有初始低电平 |

DAC 电源和独立主时钟稳定后，再由 DAC 驱动释放 RESETB。I2S 的 MCLK 输出继续关闭；PB14 用作耳机检测，不再预留为 I2S 扩展数据。

EAR_DET 的外部偏置和有效电平须按耳机插座电路确定。MAX97220_MUTE 保留此前的输出状态，本次不加入放音或静音时序，也不由网络名推断有效电平。输出级 MUTE、DAC RESETB 应有明确的硬件上电状态。

保留用户现有 PLLI2SN=258、PLLI2SR=3，I2S 时钟 86 MHz。当前 16-bit 立体声、MCLK 关闭时，实际采样率约 47,991.07 Hz；本次不调整音频时钟或 USB 同步策略。

## 3. 墨水屏：SPI1

| STM32 引脚 | 封装脚号 | 功能 | 初始化方式 |
|---|---:|---|---|
| PA5 | 30 | SPI1_SCK / EPD_CLK | AF5 |
| PA7 | 32 | SPI1_MOSI / EPD_DIN | AF5，截图网络名 SPI1_SDA |
| PC4 | 33 | EPD_DC | GPIO 输出，初始低 |
| PC5 | 34 | EPD_RST | GPIO 输出，初始低 |
| PB0 | 35 | EPD_BUSY | GPIO 输入，无内部上下拉 |

SPI1 保留现有 TX DMA 和 42 MHz 分频设置；实际工作速度须按屏幕规格设置。截图未提供 EPD_CS，工程没有添加虚构的 CS 引脚；画板时需要核实屏幕侧 CS 的实际连接。

## 4. ADC、编码器与按键

| STM32 引脚 | 封装脚号 | 功能 | 初始化方式 |
|---|---:|---|---|
| PB1 | 36 | ADC1_IN9 | 模拟输入，无上下拉 |
| PC6 | 63 | BUTTON_UP4 | GPIO 输入，上拉 |
| PC7 | 64 | BUTTON_UP5 | GPIO 输入，上拉 |
| PB3 | 89 | KNOB_BUTTON | GPIO 输入，上拉 |
| PB4 | 90 | TIM3_CH1 | AF2，编码器 A 相 |
| PB5 | 91 | TIM3_CH2 | AF2，编码器 B 相 |
| PD10 | 57 | BUTTON_SIDE1 | GPIO 输入，上拉 |
| PD11 | 58 | BUTTON_SIDE2 | GPIO 输入，上拉 |
| PD12 | 59 | BUTTON_SIDE3 | GPIO 输入，上拉 |
| PD13 | 60 | BUTTON_UP1 | GPIO 输入，上拉 |
| PD14 | 61 | BUTTON_UP2 | GPIO 输入，上拉 |
| PD15 | 62 | BUTTON_UP3 | GPIO 输入，上拉 |

ADC1：12 bit、单通道 9、软件触发单次转换、480 cycles 采样时间，ADC 时钟为 APB2/4=21 MHz，不占 DMA。初始化不启动采样；应用按需调用 HAL_ADC_Start / HAL_ADC_PollForConversion / HAL_ADC_GetValue。

按键采用普通输入，由后续应用轮询和消抖，未开启 EXTI。PB14 的 EAR_DET 与 PD14 的 BUTTON_UP2 对应同一 EXTI14，不能同时映射为该 EXTI 中断源。TIM3 保留当前编码器模式和滤波设置，当前初始化并不启动计数。

## 5. SDIO、调试和 USB

| STM32 引脚 | 封装脚号 | 功能 |
|---|---:|---|
| PA15 | 77 | SDIO_CD，普通输入，保留现有低有效卡检测逻辑 |
| PC8 | 65 | SDIO_D0 |
| PC9 | 66 | SDIO_D1 |
| PC10 | 78 | SDIO_D2 |
| PC11 | 79 | SDIO_D3 |
| PC12 | 80 | SDIO_CLK |
| PD2 | 83 | SDIO_CMD |
| PA9 | 68 | USART1_TX，115200 8N1 调试输出 |
| PA10 | 69 | USART1_RX，115200 8N1 调试输入 |
| PA11 | 70 | USB_DM，截图网络 USB_DN |
| PA12 | 71 | USB_DP |
| PA13 | 72 | SWDIO |
| PA14 | 76 | SWCLK |
| PH0/OSC_IN | 12 | 8 MHz HSE 输入端 |
| PH1/OSC_OUT | 13 | 8 MHz HSE 输出端 |

USART1 保留 RX/TX DMA。PA9 已作调试 TX，USB 保持当前 VBUS sensing 关闭配置。PA15、PB3、PB4 分别被卡检测和编码器占用，调试使用 SWD，不启用完整 JTAG/SWO。

## 6. 释放与保留

- 关闭原 I2C1，释放图中 NC 的 PB6/PB7。
- 关闭原 UART7 及其两个 DMA/中断，调试使用图中的 USART1。
- SPI3 不启用；PB3/PB4/PB5 现在用于编码器。
- PC6 不作 I2S MCLK，PC6/PC7 按图用于按键。
- SAI、RTC、CAN、内部 DAC 等不额外开启。
- 现有五个 DMA 请求保留：I2S2_TX、SPI1_TX、SDIO、USART1_RX、USART1_TX，Stream 不重占。
- 系统时钟、TIM14 HAL 时基、FreeRTOS、FatFs、USB Audio 和 2 MiB Flash/192 KiB SRAM/64 KiB CCM 分区保留。

## 依据与验证范围

依据：用户提供的 MCU 原理图截图、本机 STM32CubeMX 6.16.1 器件数据库，以及 [ST STM32F429VI 数据手册的引脚/复用表](https://www.st.com/resource/en/datasheet/stm32f429vi.pdf)。

本次通过 CubeMX 命令行重新保存 `.ioc` 并生成 C/CMake 配置；校验引脚复用与 DMA，执行 Debug 构建。构建和静态校验不代替实板的 Flash 通信、ADC、按键、音频、SD 或 USB 验证。
