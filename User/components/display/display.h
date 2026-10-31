#ifndef DISPLAY_H
#define DISPLAY_H

#include "lv_port_disp.h"
#include "ssd1685.h"
#define DISP_HOR_RES    SSD1685_HEIGHT
#define DISP_VER_RES    SSD1685_WIDTH
#define DISP_BUF_SIZE   (DISP_HOR_RES * DISP_VER_RES / 8 + 8)
/* 完整横屏 I1，无调色板；stride 为每行字节数。 */
#define display_draw(pixels, stride) ssd1685_draw((pixels), (stride))

void display_init(void);

#endif
