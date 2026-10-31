#ifndef DISPLAY_INTERNAL_H
#define DISPLAY_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include "spi.h"

#define DISPLAY_SPI_HANDLE (&hspi1)
#define DISPLAY_DC_GPIO_Port EPD_DC_GPIO_Port
#define DISPLAY_DC_Pin       EPD_DC_Pin
#define DISPLAY_RES_GPIO_Port EPD_RST_GPIO_Port
#define DISPLAY_RES_Pin       EPD_RST_Pin

void display_tx_cmd(uint8_t cmd, const uint8_t *param, size_t param_size);
void display_tx_data(const uint8_t *data, size_t size);

#endif
