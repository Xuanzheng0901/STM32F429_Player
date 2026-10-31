#ifndef ES9018_H
#define ES9018_H

#include <stdbool.h>
#include <stdint.h>

#define ES9018_I2C_ADDRESS       0x48U
#define ES9018_I2C_HANDLE        hi2c2
#define ES9018_I2C_TIMEOUT_MS    100U

#define ES9018_REG_SYSTEM        0x00U
#define ES9018_REG_INPUT         0x01U
#define ES9018_REG_VOLUME_RATE   0x06U
#define ES9018_REG_GENERAL       0x07U
#define ES9018_REG_MASTER_MODE   0x0AU
#define ES9018_REG_CHANNEL_MAP   0x0BU
#define ES9018_REG_DPLL          0x0CU
#define ES9018_REG_SOFT_START    0x0EU
#define ES9018_REG_VOLUME_LEFT   0x0FU
#define ES9018_REG_VOLUME_RIGHT  0x10U
#define ES9018_REG_CHIP_STATUS   0x40U

#define ES9018_STATUS_LOCK       0x01U
#define ES9018_STATUS_AUTOMUTE   0x02U
#define ES9018_STATUS_ID_MASK    0x1CU
#define ES9018_STATUS_ID         0x10U

typedef enum
{
    ES9018_WORD_LENGTH_16 = 0x00,
    ES9018_WORD_LENGTH_24 = 0x40,
    ES9018_WORD_LENGTH_32 = 0x80
} ES9018_WordLength;

typedef enum
{
    ES9018_FORMAT_I2S = 0x00,
    ES9018_FORMAT_LEFT_JUSTIFIED = 0x10
} ES9018_SerialFormat;

typedef enum
{
    ES9018_FILTER_FAST = 0x00,
    ES9018_FILTER_SLOW = 0x20,
    ES9018_FILTER_MINIMUM_PHASE = 0x40
} ES9018_Filter;

// PCM I2S 从机、16 位槽宽、立体声、0 dB 并解除静音
void ES9018_init(void);

// 原始衰减值：-(volume / 2) dB
void ES9018_set_volume(uint8_t volume);
void ES9018_set_mute(bool mute);
void ES9018_set_filter(ES9018_Filter filter);
void ES9018_set_format(ES9018_SerialFormat format, ES9018_WordLength word_length);

#endif
