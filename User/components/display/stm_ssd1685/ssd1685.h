#ifndef SSD1685_H
#define SSD1685_H

#include <stdint.h>

#define SSD1685_WIDTH       184
#define SSD1685_HEIGHT      360
#define SSD1685_ROW_BYTES   (SSD1685_WIDTH / 8)
#define SSD1685_BUFFER_SIZE (SSD1685_ROW_BYTES * SSD1685_HEIGHT)

#define SSD1685_REFRESH_FULL    0
#define SSD1685_REFRESH_FAST    1
#define SSD1685_REFRESH_PARTIAL 2

//后续变化帧的默认刷新方式，首帧固定全刷建立底图
#define SSD1685_REFRESH_MODE SSD1685_REFRESH_PARTIAL

void ssd1685_init(void);
void ssd1685_deep_sleep(void);

void ssd1685_set_refresh_mode(uint8_t mode);

void ssd1685_draw(const uint8_t *pixels, uint16_t stride);
void ssd1685_draw_full(const uint8_t *pixels, uint16_t stride);

#endif
