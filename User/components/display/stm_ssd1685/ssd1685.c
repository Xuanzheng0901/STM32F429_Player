#include "ssd1685.h"
#include "ssd1685_internal.h"
#include "display_internal.h"
#include "log.h"
#include <stdbool.h>
#include <string.h>

/* 流程参考佳显 S-GDEY0266T90H-2FP-20230915：
 * https://www.good-display.cn/companyfile/1155.html
 * 0x21/00 40 选择分辨率，见 SSD1685 Rev 0.11 第 26 页。
 */
static bool sleeping = true;
static bool base_valid;
static bool analog_on;
static uint8_t refresh_mode = SSD1685_REFRESH_MODE;
static uint8_t previous_frame[SSD1685_BUFFER_SIZE];
/* 检测变化、像素转换和参考 RAM 清零共用小缓冲，以重复计算换掉整屏转换缓存。 */
#define SSD1685_TX_CHUNK_ROWS 16U
#define SSD1685_TX_CHUNK_SIZE (SSD1685_ROW_BYTES * SSD1685_TX_CHUNK_ROWS)
static _Alignas(4) uint8_t tx_buffer[SSD1685_TX_CHUNK_SIZE];

static void ssd1685_wait_busy(void)
{
    vTaskDelay(pdMS_TO_TICKS(10));
    while(HAL_GPIO_ReadPin(EPD_BUSY_GPIO_Port, EPD_BUSY_Pin) == GPIO_PIN_SET)
        vTaskDelay(pdMS_TO_TICKS(1));
}

static void ssd1685_set_window(uint16_t first_byte, uint16_t last_byte,
                               uint16_t first_row, uint16_t last_row)
{
    uint16_t y_start = SSD1685_HEIGHT - 1 - first_row;
    uint16_t y_end = SSD1685_HEIGHT - 1 - last_row;
    display_tx_cmd(SSD1685_CMD_RAM_X_WINDOW, (uint8_t[]){first_byte, last_byte}, 2);
    display_tx_cmd(SSD1685_CMD_RAM_Y_WINDOW,
                   (uint8_t[]){y_start & 0xFF, y_start >> 8, y_end & 0xFF, y_end >> 8}, 4);
    display_tx_cmd(SSD1685_CMD_RAM_X_COUNTER, (uint8_t[]){first_byte}, 1);
    display_tx_cmd(SSD1685_CMD_RAM_Y_COUNTER, (uint8_t[]){y_start & 0xFF, y_start >> 8}, 2);
}

static void ssd1685_activate(uint8_t sequence)
{
    display_tx_cmd(SSD1685_CMD_UPDATE_CONTROL_2, &sequence, 1);
    display_tx_cmd(SSD1685_CMD_ACTIVATE, NULL, 0);
    ssd1685_wait_busy();
}

static void ssd1685_convert_rows(const uint8_t *pixels, uint16_t stride,
                                 uint16_t first_row, uint16_t row_count)
{
    /* 横屏 (x,y) 转为 RAM (y,359-x)，仅计算当前块的原生 RAM 行。 */
    for(uint16_t row = 0; row < row_count; row++)
    {
        uint16_t x = first_row + row;
        for(uint16_t byte = 0; byte < SSD1685_ROW_BYTES; byte++)
        {
            uint8_t value = 0;
            for(uint8_t bit = 0; bit < 8; bit++)
            {
                uint16_t y = byte * 8 + bit;
                if(pixels[y * stride + x / 8] & (0x80U >> (x % 8)))
                    value |= 0x80U >> bit;
            }
            tx_buffer[row * SSD1685_ROW_BYTES + byte] = value;
        }
    }
}

static void ssd1685_write_ram(uint8_t command, const uint8_t *pixels, uint16_t stride,
                              uint16_t first_row, uint16_t last_row)
{
    display_tx_cmd(command, NULL, 0);
    for(uint16_t row = first_row; row <= last_row;)
    {
        uint16_t row_count = last_row - row + 1;
        if(row_count > SSD1685_TX_CHUNK_ROWS)
            row_count = SSD1685_TX_CHUNK_ROWS;
        size_t size = (size_t)row_count * SSD1685_ROW_BYTES;
        if(pixels != NULL)
            ssd1685_convert_rows(pixels, stride, row, row_count);
        else
            memset(tx_buffer, 0, size);

        /* DMA 完成后才复用缓冲；RAM 地址计数器跨块自动推进。 */
        display_tx_data(tx_buffer, size);
        if(command == SSD1685_CMD_WRITE_REFERENCE && pixels != NULL)
            memcpy(previous_frame + (size_t)row * SSD1685_ROW_BYTES, tx_buffer, size);
        row += row_count;
    }
}

void ssd1685_init(void)
{
    HAL_GPIO_WritePin(DISPLAY_RES_GPIO_Port, DISPLAY_RES_Pin, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(10));
    HAL_GPIO_WritePin(DISPLAY_RES_GPIO_Port, DISPLAY_RES_Pin, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(10));
    ssd1685_wait_busy();
    display_tx_cmd(SSD1685_CMD_SW_RESET, NULL, 0);
    ssd1685_wait_busy();

    display_tx_cmd(SSD1685_CMD_DRIVER_OUTPUT,
                   (uint8_t[]){(SSD1685_HEIGHT - 1) & 0xFF, (SSD1685_HEIGHT - 1) >> 8, 0x00}, 3);
    display_tx_cmd(SSD1685_CMD_DATA_ENTRY, (uint8_t[]){0x01}, 1);
    /* 正常读写黑白/参考 RAM，选择 184 路源极输出。 */
    display_tx_cmd(SSD1685_CMD_UPDATE_CONTROL_1, (uint8_t[]){0x00, 0x40}, 2);
    ssd1685_set_window(0, SSD1685_ROW_BYTES - 1, 0, SSD1685_HEIGHT - 1);
    display_tx_cmd(SSD1685_CMD_BORDER_WAVEFORM, (uint8_t[]){0x05}, 1);
    display_tx_cmd(SSD1685_CMD_TEMPERATURE_SENSOR, (uint8_t[]){0x80}, 1);
    ssd1685_wait_busy();
    sleeping = false;
    base_valid = false;
    analog_on = false;
}

static void ssd1685_write_full(const uint8_t *pixels, uint16_t stride, bool fast)
{
    /* 全刷和快刷前按厂家要求重新初始化。 */
    if(sleeping || base_valid)
        ssd1685_init();
    if(fast)
    {
        /* 厂家 1.5 秒快刷配置，兼容旧版屏幕。 */
        ssd1685_activate(0xB1);
        display_tx_cmd(SSD1685_CMD_TEMPERATURE_WRITE, (uint8_t[]){0x6E, 0x00}, 2);
        ssd1685_activate(0x91);
    }
    ssd1685_set_window(0, SSD1685_ROW_BYTES - 1, 0, SSD1685_HEIGHT - 1);
    ssd1685_write_ram(SSD1685_CMD_WRITE_BW_RAM, pixels, stride, 0, SSD1685_HEIGHT - 1);
    ssd1685_set_window(0, SSD1685_ROW_BYTES - 1, 0, SSD1685_HEIGHT - 1);
    ssd1685_write_ram(SSD1685_CMD_WRITE_REFERENCE, NULL, 0, 0, SSD1685_HEIGHT - 1);

    // LOGD("EPD", "%s refresh start", fast ? "Fast" : "Full");
    /* F4 保持模拟电源和时钟，供后续 1C 局刷使用。 */
    ssd1685_activate(fast ? 0xC7 : 0xF4);
    analog_on = !fast;
    // LOGD("EPD", "Refresh done");

    ssd1685_set_window(0, SSD1685_ROW_BYTES - 1, 0, SSD1685_HEIGHT - 1);
    ssd1685_write_ram(SSD1685_CMD_WRITE_REFERENCE, pixels, stride, 0, SSD1685_HEIGHT - 1);
    base_valid = true;
}

void ssd1685_draw_full(const uint8_t *pixels, uint16_t stride)
{
    /* 初始化使底图失效，draw() 随后转换并全刷。 */
    ssd1685_init();
    ssd1685_draw(pixels, stride);
}

void ssd1685_set_refresh_mode(uint8_t mode)
{
    refresh_mode = mode;
}

void ssd1685_draw(const uint8_t *pixels, uint16_t stride)
{
    if(!base_valid)
    {
        ssd1685_write_full(pixels, stride, false);
        return;
    }

    uint16_t first_row = SSD1685_HEIGHT, last_row = 0;
    for(uint16_t row = 0; row < SSD1685_HEIGHT;)
    {
        uint16_t row_count = SSD1685_HEIGHT - row;
        if(row_count > SSD1685_TX_CHUNK_ROWS)
            row_count = SSD1685_TX_CHUNK_ROWS;
        ssd1685_convert_rows(pixels, stride, row, row_count);
        for(uint16_t i = 0; i < row_count; i++)
        {
            uint16_t changed_row = row + i;
            if(memcmp(tx_buffer + i * SSD1685_ROW_BYTES,
                      previous_frame + (size_t)changed_row * SSD1685_ROW_BYTES,
                      SSD1685_ROW_BYTES) == 0)
                continue;
            if(changed_row < first_row) first_row = changed_row;
            if(changed_row > last_row) last_row = changed_row;
        }
        row += row_count;
    }
    if(first_row == SSD1685_HEIGHT)
        return;

    if(refresh_mode != SSD1685_REFRESH_PARTIAL || !analog_on)
    {
        ssd1685_write_full(pixels, stride, refresh_mode == SSD1685_REFRESH_FAST);
        return;
    }

    /* 按厂家 EPD_Dis_PartAll 使用完整 RAM 窗口做局刷。
     * 滚动交替改变文字与高亮背景，不复用大小不同的窄窗口。
     * 仍使用 1C 波形；新图和参考图都完整同步，不额外分配整屏缓冲。 */
    first_row = 0;
    last_row = SSD1685_HEIGHT - 1;
    uint16_t first_byte = 0, last_byte = SSD1685_ROW_BYTES - 1;
    // LOGI("EPD", "Partial window: bytes %u..%u, rows %u..%u, %u bytes",
    //      (unsigned)first_byte, (unsigned)last_byte, (unsigned)first_row, (unsigned)last_row,
    //      (unsigned)(last_row - first_row + 1) * SSD1685_ROW_BYTES);
    ssd1685_set_window(first_byte, last_byte, first_row, last_row);
    ssd1685_write_ram(SSD1685_CMD_WRITE_BW_RAM, pixels, stride, first_row, last_row);
    // LOGI("EPD", "Partial refresh start");
    ssd1685_activate(0x1C);
    // LOGI("EPD", "Refresh done");

    /* 刷新后同步控制器参考 RAM 和软件旧帧。 */
    ssd1685_set_window(first_byte, last_byte, first_row, last_row);
    ssd1685_write_ram(SSD1685_CMD_WRITE_REFERENCE, pixels, stride, first_row, last_row);
}

void ssd1685_deep_sleep(void)
{
    if(analog_on)
        ssd1685_activate(0x03);
    display_tx_cmd(SSD1685_CMD_DEEP_SLEEP, (uint8_t[]){0x01}, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    analog_on = false;
    sleeping = true;
    base_valid = false;
}
