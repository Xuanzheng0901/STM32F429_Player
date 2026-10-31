#ifndef TEST_DISPLAY_INTERNAL_H
#define TEST_DISPLAY_INTERNAL_H
#include <stddef.h>
#include <stdint.h>
#define DISPLAY_RES_GPIO_Port ((void *)0)
#define DISPLAY_RES_Pin 0
#define EPD_BUSY_GPIO_Port ((void *)0)
#define EPD_BUSY_Pin 1
#define GPIO_PIN_RESET 0
#define GPIO_PIN_SET 1
#define pdMS_TO_TICKS(ms) (ms)
void HAL_GPIO_WritePin(void *port, int pin, int state);
int HAL_GPIO_ReadPin(void *port, int pin);
void vTaskDelay(uint32_t ticks);
void display_tx_cmd(uint8_t cmd, const uint8_t *data, size_t size);
void display_tx_data(const uint8_t *data, size_t size);
#endif
