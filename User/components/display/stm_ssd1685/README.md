# GDEY0266T90H / SSD1685 驱动

面板原生 RAM 为 184×360，应用使用横屏 360×184、黑白 I1 图像。驱动负责像素转换、RAM 写入、全刷、快刷、局刷和休眠，通过 `display_tx_cmd()` / `display_tx_data()` 访问 SPI1。

## 接口与接线

| 信号 | 引脚 |
| --- | --- |
| SCK | PA5 / SPI1_SCK |
| DIN | PA7 / SPI1_MOSI |
| DC | PC4 / `EPD_DC` |
| RST | PC5 / `EPD_RST`，低电平复位 |
| BUSY | PB0 / `EPD_BUSY`，上拉，高电平忙 |
| CS | GND，常选中 |

SPI1 为 8 bit、MSB First、Mode 0、10.5 MHz。图像 DMA 使用 DMA2 Stream3 / Channel3。详细 GPIO、LVGL 及任务配置见 [显示模块文档](../README.md)。

| API | 功能 |
| --- | --- |
| `ssd1685_init()` | 硬件和软件复位，配置分辨率、RAM 方向、边框及温度传感器 |
| `ssd1685_draw(pixels, stride)` | 比较并绘制完整横屏 I1 图像 |
| `ssd1685_draw_full(pixels, stride)` | 重新初始化并全刷同一格式的图像 |
| `ssd1685_set_refresh_mode(mode)` | 设置后续变化帧的刷新模式 |
| `ssd1685_deep_sleep()` | 关闭模拟电源和时钟，进入深度休眠 |

应用使用 `display_init()` 初始化传输层和驱动；使用 LVGL 时由 `lv_port_disp_init()` 完成。绘制、模式切换和休眠在同一任务中串行执行；与 LVGL 共用时持有 `lvgl_port_lock()`。

## 像素与分块传输

输入为完整的 360×184 横屏图像，行优先、MSB 在前，1 白、0 黑。`stride` 为源行跨度，单位为字节，最小为 45，支持行末填充。紧密排列的像素共 8280 B；调用 `ssd1685_draw()` 时传入调色板之后的像素地址。

横屏 `(x, y)` 映射到原生 RAM `(y, 359-x)`。原生 RAM 每行 23 B，X 地址递增、Y 地址从 359 递减到 0。转换函数每次生成最多 16 行，复用 368 B 的 `tx_buffer`；最后一块为 8 行、184 B。

每次 RAM 写入先发送一次写 RAM 命令，再连续发送各块。每块等待 DMA 完成后才复用缓冲；所有图像块发送完成后激活刷新。变化比较、新图写入和参考 RAM 同步分别重新转换源图，源像素在绘制函数返回前保持有效且内容不变。

驱动保留 8280 B 的 `previous_frame` 逐字节比较新旧帧；转换和参考 RAM 清零共用同一份 368 B 缓冲。SPI DMA 从普通 SRAM 的 `tx_buffer` 取数。

## 初始化与刷新

1. RST 拉低 10 ms，再拉高 10 ms，等待 BUSY 低；发送 `0x12` 软件复位并等待完成。
2. `0x01 = 67 01 00` 设置 360 个 Gate，`0x11 = 01` 设置 X 递增、Y 递减，`0x21 = 00 40` 选择 184 路 Source。
3. 配置 X 窗口 byte `0..22`、Y 窗口 `359..0`，`0x3C = 05` 设置边框，`0x18 = 80` 选择内部温度传感器。
4. 首帧向 `0x24` 写新图，向 `0x26` 写零，以 `0x22/F4`、`0x20` 全刷。BUSY 完成后将新图同步到 `0x26` 和软件旧帧。
5. 后续比较完整画面，相同画面直接返回；有变化时按选定模式刷新。局刷使用完整 RAM 窗口，向 `0x24` 分块写新图，以 `0x22/1C`、`0x20` 刷新，完成后同步 `0x26` 和软件旧帧。

## 刷新模式与休眠

默认模式由 `ssd1685.h` 中的 `SSD1685_REFRESH_MODE` 设置，当前为 `SSD1685_REFRESH_PARTIAL`。运行时通过 `ssd1685_set_refresh_mode()` 切换：

| 模式 | 值 | 后续变化帧 |
| --- | ---: | --- |
| `SSD1685_REFRESH_FULL` | 0 | 重新初始化，以 `F4` 全刷 |
| `SSD1685_REFRESH_FAST` | 1 | 重新初始化，`B1 → 1A/6E 00 → 91` 加载快刷配置，再以 `C7` 刷新 |
| `SSD1685_REFRESH_PARTIAL` | 2 | 使用真实旧帧，以 `1C` 局刷 |

所有模式的首帧均全刷。设置模式本身不触发刷新；快刷切回局刷后，下一次变化帧先全刷重建底图。连续局刷由应用通过 `ssd1685_draw_full()` 发起手动全刷。

```c
ssd1685_draw(pixels, 45);
ssd1685_set_refresh_mode(SSD1685_REFRESH_FAST);
ssd1685_draw(pixels_next, 45);
ssd1685_draw_full(pixels_next, 45);
ssd1685_deep_sleep();
```

休眠时，若模拟电源处于开启状态，先发送 `0x22/03`、`0x20` 关闭电源和时钟，再发送 `0x10/01` 进入深度休眠。休眠后底图失效，下次绘制重新初始化并全刷。

## 本地资料与测试

| 文件 | 内容 |
| --- | --- |
| [GDEY0266T90H.pdf](docs/GDEY0266T90H.pdf) | 面板引脚、电气特性、SPI 时序和操作流程 |
| [SSD1685.pdf](docs/SSD1685.pdf) | 控制器命令、RAM 配置和接口时序；第 44 页列出 SPI 写入上限 20 MHz |
| [GDEY0266T90H_Bitmap_Guide.pdf](docs/GDEY0266T90H_Bitmap_Guide.pdf) | 图片制作及取模说明 |

[主机测试](../../../../tests/display_port/README.md)覆盖三种刷新模式、完整 RAM 窗口、分块地址连续性、尾块长度、参考帧同步、重复画面跳过、带行填充的 I1 输入及休眠恢复。
