#ifndef TEST_SDIO_H
#define TEST_SDIO_H
#include <stdint.h>
#define MOCK_SECTORS 32768U
#define MOCK_SRAM_SIZE (600U * 512U)
extern uint8_t mock_sram[MOCK_SRAM_SIZE];
#define SD_DMA_RAM_START ((uintptr_t)mock_sram)
#define SD_DMA_RAM_END ((uintptr_t)mock_sram + sizeof(mock_sram))
typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
typedef struct { unsigned unused; } SD_HandleTypeDef;
typedef struct { uint32_t LogBlockNbr; } HAL_SD_CardInfoTypeDef;
extern SD_HandleTypeDef hsd;
#define GPIO_PIN_RESET 0
#define SDIO_CD_GPIO_Port ((void *)1)
#define SDIO_CD_Pin 1
#define SDIO_BUS_WIDE_4B 4
#define HAL_SD_CARD_TRANSFER 4
int HAL_GPIO_ReadPin(void *port, unsigned pin);
HAL_StatusTypeDef HAL_SD_Init(SD_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_SD_ConfigWideBusOperation(SD_HandleTypeDef *handle, unsigned width);
int HAL_SD_GetCardState(SD_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_SD_GetCardInfo(SD_HandleTypeDef *handle, HAL_SD_CardInfoTypeDef *info);
HAL_StatusTypeDef HAL_SD_ReadBlocks_DMA(SD_HandleTypeDef *handle, uint8_t *buffer, uint32_t sector, uint32_t count);
HAL_StatusTypeDef HAL_SD_WriteBlocks_DMA(SD_HandleTypeDef *handle, uint8_t *buffer, uint32_t sector, uint32_t count);
HAL_StatusTypeDef HAL_SD_Abort(SD_HandleTypeDef *handle);
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *handle);
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *handle);
void HAL_SD_ErrorCallback(SD_HandleTypeDef *handle);
void HAL_SD_AbortCallback(SD_HandleTypeDef *handle);
#endif
