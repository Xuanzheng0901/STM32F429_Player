#include "es9018.h"
#include "i2c.h"
#include "log.h"
#include "FreeRTOS.h"
#include "task.h"

static uint8_t s_volume;

/* ES9018K2M 手册 v3.7：控制接口第 10 页，寄存器第 12～19 页。
 * 逐个访问寄存器，不依赖地址自动递增。
 */
static HAL_StatusTypeDef ES9018_read_reg(uint8_t reg, uint8_t *value)
{
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(&ES9018_I2C_HANDLE, ES9018_I2C_ADDRESS << 1,
                            reg, I2C_MEMADD_SIZE_8BIT, value, 1U,
                            ES9018_I2C_TIMEOUT_MS);
    if(status != HAL_OK)
        LOGE("ES9018", "read reg=0x%02X failed, status=%d, I2C error=0x%08lX",
             reg, (int)status, (unsigned long)HAL_I2C_GetError(&ES9018_I2C_HANDLE));
    return status;
}

static HAL_StatusTypeDef ES9018_write_reg(uint8_t reg, uint8_t value)
{
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(&ES9018_I2C_HANDLE, ES9018_I2C_ADDRESS << 1,
                             reg, I2C_MEMADD_SIZE_8BIT, &value, 1U,
                             ES9018_I2C_TIMEOUT_MS);
    if(status != HAL_OK)
        LOGE("ES9018", "write reg=0x%02X value=0x%02X failed, status=%d, I2C error=0x%08lX",
             reg, value, (int)status, (unsigned long)HAL_I2C_GetError(&ES9018_I2C_HANDLE));
    return status;
}

static HAL_StatusTypeDef ES9018_update_register(uint8_t reg, uint8_t mask,
                                               uint8_t value)
{
    uint8_t current;
    HAL_StatusTypeDef status = ES9018_read_reg(reg, &current);
    if(status != HAL_OK)
        return status;

    return ES9018_write_reg(reg, (current & (uint8_t)~mask) | value); //将掩码位置零, 交给value修改
}

bool ES9018_is_locked(void)
{
    uint8_t status = 0;
    return ES9018_read_reg(ES9018_REG_CHIP_STATUS, &status) == HAL_OK &&
           (status & ES9018_STATUS_LOCK) != 0;
}

void ES9018_set_volume(uint8_t volume)
{
    s_volume = volume;
    ES9018_write_reg(ES9018_REG_VOLUME_LEFT, volume);
    ES9018_write_reg(ES9018_REG_VOLUME_RIGHT, volume);
}

uint8_t ES9018_get_volume(void) { return s_volume; }

void ES9018_set_mute(bool mute)
{
    ES9018_update_register(ES9018_REG_GENERAL, 0x03U, mute ? 0x03U : 0x00U);
}

void ES9018_set_filter(ES9018_Filter filter)
{
    if(filter != ES9018_FILTER_FAST && filter != ES9018_FILTER_SLOW &&
       filter != ES9018_FILTER_MINIMUM_PHASE)
        return;

    ES9018_update_register(ES9018_REG_GENERAL, 0x60U, (uint8_t)filter);
}

void ES9018_set_format(ES9018_SerialFormat format, ES9018_WordLength word_length)
{
    if((format != ES9018_FORMAT_I2S && format != ES9018_FORMAT_LEFT_JUSTIFIED) ||
       (word_length != ES9018_WORD_LENGTH_16 &&
        word_length != ES9018_WORD_LENGTH_24 &&
        word_length != ES9018_WORD_LENGTH_32))
        return;

    /* 关闭输入自动识别，固定使用 PCM 输入。 */
    ES9018_write_reg(ES9018_REG_INPUT, (uint8_t)format | (uint8_t)word_length);
}

void ES9018_init(void)
{
    /* 硬件复位后等待芯片稳定，再读取状态并配置寄存器。 */
    HAL_GPIO_WritePin(ES9018_RST_GPIO_Port, ES9018_RST_Pin, GPIO_PIN_RESET);
    vTaskDelay(100);
    HAL_GPIO_WritePin(ES9018_RST_GPIO_Port, ES9018_RST_Pin, GPIO_PIN_SET);
    vTaskDelay(100);
    uint8_t read_buf = 0;
    ES9018_read_reg(ES9018_REG_CHIP_STATUS, &read_buf);

    LOGI("ES9018", "Status: %d, revision: %d, chip_id: %d, automute: %d, lock: %d", read_buf, (read_buf >> 5) & 0x01, (read_buf >> 2) & 0x07, (read_buf >> 1) & 0x01, read_buf & 0x01);
    static const uint8_t settings[][2] = {
        {ES9018_REG_GENERAL,      0x83U}, /* 配置期间保持静音。 */
        {ES9018_REG_INPUT,        0x00U}, /* PCM、Philips I2S、16 位槽宽。 */
        {ES9018_REG_VOLUME_RATE,  0x4AU}, /* 旁路去加重，保留位 3 保持为 1。 */
        {ES9018_REG_MASTER_MODE,  0x00U}, /* 从机、启用 ASRC，兼容 Rev.W。 */
        {ES9018_REG_CHANNEL_MAP,  0x02U}, /* 通道 1 为左声道，通道 2 为右声道。 */
        {ES9018_REG_DPLL,         0x5AU}, /* 使用手册默认 DPLL 带宽。 */
        {ES9018_REG_SOFT_START,   0x8AU}, /* 正常输出，默认软启动时间。 */
        {ES9018_REG_VOLUME_LEFT,  0x00U},
        {ES9018_REG_VOLUME_RIGHT, 0x00U},
        {ES9018_REG_GENERAL,      0x80U}  /* 快滚降、正常 IIR，解除静音。 */
    };
    for(uint32_t i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i)
        ES9018_write_reg(settings[i][0], settings[i][1]);
    s_volume = 0;
}
