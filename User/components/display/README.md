# Display 模块

当前工程使用 GDEY0266T90H / SSD1685 黑白墨水屏，横屏分辨率为 360×184。显示模块由 SPI 传输、SSD1685 驱动、LVGL 9.3.0 移植和编码器输入组成。

## 文件职责

| 路径 | 职责 |
| --- | --- |
| `display.h` | 显示分辨率、缓冲大小、初始化及绘制入口 |
| `display.c` / `display_internal.h` | SPI1 命令发送、DMA 数据发送、完成通知及 GPIO 映射 |
| `stm_ssd1685/` | [像素转换、刷新模式、BUSY 等待和休眠](stm_ssd1685/README.md) |
| `lvgl_port/lv_port_disp.c` | LVGL 初始化、单缓冲刷新、显示任务及互斥锁 |
| `lvgl_port/lv_port_encoder.c` | 旋转计数与按键边沿接入 LVGL 编码器设备 |
| `lvgl_port/lv_port_knob.c` | TIM3 硬件编码器采样和机械档位换算 |
| `lvgl_port/lv_port_button.c` | GPIO 按键扫描、消抖和点击事件 |
| `lvgl_port/lv_conf.h` | LVGL 功能、字体、内存及刷新周期配置 |
| `lvgl_port/lvgl_sources.cmake` | 按当前功能选择 LVGL 源文件 |

## 硬件配置

| 信号 | STM32F429VIT6 引脚 | 配置 |
| --- | --- | --- |
| SCK | PA5，封装脚 30 | SPI1_SCK，AF5，Very High，无上下拉 |
| MOSI / DIN | PA7，封装脚 32 | SPI1_MOSI，AF5，Very High，无上下拉 |
| DC | PC4，封装脚 33 | `EPD_DC`，推挽输出，初始低电平 |
| RST | PC5，封装脚 34 | `EPD_RST`，推挽输出，初始低电平 |
| BUSY | PB0，封装脚 35 | `EPD_BUSY`，输入上拉，高电平忙 |
| CS | GND | 常选中，DC 区分命令和数据 |

SPI1 使用主机模式、8 bit、MSB First、Mode 0、软件 NSS、八分频。APB2 为 84 MHz，SCK 为 10.5 MHz。

TX DMA 为 DMA2 Stream3 / Channel3，Normal、Byte、内存递增、Low，FIFO 关闭。SPI1 和 DMA2 Stream3 的中断优先级均为 5。命令及参数使用阻塞发送；图像数据使用 DMA，任务通过二值信号量等待完成后复用转换缓冲。BUSY 等待通过 `vTaskDelay()` 让出 CPU。

## LVGL 与内存

LVGL 使用 I1 颜色格式和 `LV_DISPLAY_RENDER_MODE_FULL`，默认刷新检查周期为 250 ms。一次 flush 传入完整画面，SSD1685 驱动决定物理刷新方式：首帧全刷，后续默认局刷，相同画面跳过。

| 内存 | 大小 | 位置及用途 |
| --- | ---: | --- |
| `disp_buf1` | 8,288 B | 普通 SRAM；8 B 调色板 + 8,280 B 横屏像素 |
| `previous_frame` | 8,280 B | 普通 SRAM；精确比较旧帧 |
| `tx_buffer` | 368 B | 普通 SRAM；16 个原生行的转换和 SPI DMA 数据 |
| LVGL 内置堆 | 24 KiB | CCMRAM `0x10000000`；LVGL 对象等动态数据 |
| FreeRTOS 堆 | 40 KiB | CCMRAM `0x10006000`；动态任务栈、队列及内核对象 |

三份显示像素缓冲共 16,936 B。flush 跳过 8 B 调色板，传入每行 45 B 的 I1 图像；等待发送、BUSY 和参考帧同步完成后调用 `lv_display_flush_ready()`。

`LV_ATTRIBUTE_LARGE_RAM_ARRAY` 将对象池放入 `.lvgl_heap (NOLOAD)`，不占 Flash；TLSF 初始化自身的管理数据，不依赖上电清零。像素帧缓冲、SPI DMA 中转区仍在普通 SRAM，DMA 不访问 CCMRAM。

LVGL 启用 FreeRTOS 时还创建 `swdraw` 软件绘制线程，即使只有一个绘制单元，也占用 16 KiB 动态任务栈。另有 `LVGL` 管理任务 4 KiB；曲库任务保留 16 KiB 栈，改为普通 SRAM 静态分配后，40 KiB RTOS 堆才能同时容纳这些线程。分配预算见 [heap_budget](../../../tests/heap_budget/README.md)。

默认字体为 `fusion_pixel_12_cjk`。使用 24 像素字体时，在应用源文件中声明 `LV_FONT_DECLARE(fusion_pixel_24);`，再通过 `lv_obj_set_style_text_font()` 设置。

当前 UI 的 12px 字库使用 `lv_font_conv 1.5.3` 扩充，沿用原文件头中的 Fusion Pixel
2025.03.14 简体等宽 TTF。保留 1bpp、不压缩、不使用 kerning，行高 11、基线 2 不变。
原有 5534 个字符的位图和字形尺寸保持一致；24px 字库不参与当前 UI，此次未扩充。

| 范围 | 字符数 |
| --- | ---: |
| 基本区汉字 U+4E00～U+9FFF | 17637（原 5200） |
| 汉字扩展 A | 56 |
| 增补平面汉字 | 25 |
| ASCII、部首、标点/假名、全角字符 | 852 |
| 合计 | 18570（原 5534） |

使用原 TTF 实际提供的字形，所选范围内缺少的字不会凭空补齐；例如当前覆盖 GB2312
汉字 6446/6763 个，并非完整 GB2312 或全部 Unicode 汉字。字模、描述符和字符映射
均为 Flash 中的只读常量，无需扩大 LVGL/RTOS 内存池或启用大字库/压缩配置。

在仓库根目录使用以下命令重新生成，字体 C 文件头也保留了生成参数：

```powershell
lv_font_conv --font 'F:\素材\字体\fusion-pixel-font-12px-monospaced-ttf-v2025.03.14\fusion-pixel-12px-monospaced-zh_hans.ttf' --size 12 --bpp 1 --no-compress --no-kerning --range '0x20-0x7E,0x2E80-0x30FF,0x3400-0x4DBF,0x4E00-0x9FFF,0xFF00-0xFFEF,0x20000-0x323AF' --format lvgl --lv-font-name fusion_pixel_12_cjk -o User/components/display/lvgl_port/fusion_pixel_12_cjk.c
```

源 TTF 的 OFL 及内含字体许可保存在 `lvgl_port/font_licenses/fusion-pixel/`。
工具和字体来源：[LVGL 字体转换器](https://github.com/lvgl/lv_font_conv)、
[Fusion Pixel](https://github.com/TakWolf/fusion-pixel-font)。

## 初始化与调用

GPIO、DMA、SPI1、USART1 和 TIM3 由工程初始化。当前 `StartDefaultTask()` 初始化 USB 后进入延时循环，display/log 由应用任务调用。初始化顺序为日志服务、显示模块、界面及输入设备：

```c
#include "display.h"
#include "log.h"

/* 在 FreeRTOS 任务中执行一次。 */
log_init(LOG_INFO);
lv_port_disp_init();

if(lvgl_port_lock(portMAX_DELAY))
{
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Hello World");
    lv_obj_center(label);
    lvgl_port_unlock();
}
```

`lv_port_disp_init()` 包含 `display_init()` 和 `lv_init()`，创建优先级 6、栈深 1024 words（4096 B）的显示任务。应用任务通过 `lvgl_port_lock(timeout_ms)` / `lvgl_port_unlock()` 串行访问 LVGL 对象及屏幕驱动。

创建完整首屏时，可在同一锁内先调用 `disp_disable_update()`，构建对象后调用 `disp_enable_update()`。后者会使当前界面失效并触发下一次刷新。

日志使用 USART1，PA9 / PA10，115200、8N1；TX DMA 为 DMA2 Stream7 / Channel4。`log_init()` 创建优先级 12、栈深 256 words（1024 B）的发送任务和两个指针队列。16 个静态日志块共 4096 B，存放于普通 SRAM；每条最多发送 253 B，包含颜色复位和换行。生产任务以零等待方式入队，内存池用完时跳过本条日志。

## 编码器输入与音量按键

实体编码器使用 TIM3 PB4/PB5 旋转、PB3 按压确认，侧键模拟旋转/确认的代码和开关
已删除。旋钮 5 ms 采样、每档 4 计数；确认键沿用按键驱动，短按边沿在 UI 忙时入队
保留。上方五键和侧边上/下音量键共用一个 10 ms 扫描定时器，三次采样消抖，
每按一次触发一次，长按不连发；中侧键暂不扫描、不绑定动作。

| 按键 | 引脚 | 功能 |
| --- | --- | --- |
| SIDE1，下 | PD10 | 减小音量：衰减寄存器 +16，实际 -8 dB |
| SIDE2，中 | PD11 | 预留，无动作 |
| SIDE3，上 | PD12 | 增大音量：衰减寄存器 -16，实际 +8 dB |
| UP1，最右 | PD13 | 列表 |
| UP2 | PD14 | 下一首 |
| UP3，中 | PD15 | 播放/暂停 |
| UP4 | PC6 | 上一首 |
| UP5，最左 | PC7 | 模式切换 |

所有按键低电平有效。上方五键按屏幕位置对应，按下时投递 UI 左到右的控件编号
4/3/2/1/0。输入字节队列仍为 8 项，编码器确认边沿、上方键和音量键共用队列，旋转
方向单独累加。扫描回调不操作 UI 或 I2C；LVGL 读取时调用 `control_click` 或
`volume_step`，前者触发原控件事件，后者经 player → playback 交给音频任务处理。

旋钮使用有符号 16 位差值处理硬件计数回绕。输入设备通过 lv_indev_set_group 绑定
界面 group，删除使用 lvgl_port_remove_encoder；外部创建的 PB3 按键仍由调用者释放。
RTOS 扫描定时器现在为三个：旋钮、确认键、其余控制键；没有新增任务或队列。

## 构建与测试

```sh
cmake --preset Debug
cmake --build --preset Debug -j 8
cmake --preset Release
cmake --build --preset Release -j 8
```

主机测试及执行命令见 [tests/display_port](../../../tests/display_port/README.md)。
