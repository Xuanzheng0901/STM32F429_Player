#ifndef TEST_ES9018_H
#define TEST_ES9018_H
#include "i2s.h"
#include <stdbool.h>
bool ES9018_is_locked(void);
#define ES9018_REG_CHIP_STATUS 0x40U
#define ES9018_STATUS_LOCK 0x01U
typedef enum { ES9018_WORD_LENGTH_16 = 0, ES9018_WORD_LENGTH_24 = 0x40, ES9018_WORD_LENGTH_32 = 0x80 } ES9018_WordLength;
typedef enum { ES9018_FORMAT_I2S = 0 } ES9018_SerialFormat;
HAL_StatusTypeDef ES9018_set_format(ES9018_SerialFormat format, ES9018_WordLength bits);
HAL_StatusTypeDef ES9018_read_register(uint8_t reg, uint8_t *value);
#endif
