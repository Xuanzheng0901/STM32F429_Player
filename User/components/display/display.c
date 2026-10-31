#include "display.h"
#include "display_internal.h"
#include "log.h"

static SemaphoreHandle_t spi_dma_sem;
static SemaphoreHandle_t spi_mutex;

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if(hspi == DISPLAY_SPI_HANDLE)
    {
        BaseType_t task_woken = pdFALSE;
        xSemaphoreGiveFromISR(spi_dma_sem, &task_woken);
        portYIELD_FROM_ISR(task_woken);
    }
}

void display_tx_cmd(uint8_t cmd, const uint8_t *param, size_t param_size)
{
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    HAL_GPIO_WritePin(DISPLAY_DC_GPIO_Port, DISPLAY_DC_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(DISPLAY_SPI_HANDLE, &cmd, 1, 100);
    HAL_GPIO_WritePin(DISPLAY_DC_GPIO_Port, DISPLAY_DC_Pin, GPIO_PIN_SET);
    if(param_size > 0)
        HAL_SPI_Transmit(DISPLAY_SPI_HANDLE, (uint8_t *)param, (uint16_t)param_size, 100);

    xSemaphoreGive(spi_mutex);
}

void display_tx_data(const uint8_t *data, size_t size)
{
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    HAL_GPIO_WritePin(DISPLAY_DC_GPIO_Port, DISPLAY_DC_Pin, GPIO_PIN_SET);
    HAL_SPI_Transmit_DMA(DISPLAY_SPI_HANDLE, (uint8_t *)data, (uint16_t)size);
    xSemaphoreTake(spi_dma_sem, portMAX_DELAY);
    xSemaphoreGive(spi_mutex);
}

void display_init(void)
{
    spi_dma_sem = xSemaphoreCreateBinary();
    spi_mutex = xSemaphoreCreateMutex();
    ssd1685_init();
    LOGI("EPD", "GDEY0266T90H / SSD1685 initialized (360x184)");
}
