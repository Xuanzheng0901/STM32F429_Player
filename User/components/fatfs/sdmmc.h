#ifndef SDMMC_H
#define SDMMC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define SDMMC_SECTOR_SIZE 512U
typedef enum { SDMMC_READY, SDMMC_UNINITIALIZED, SDMMC_REMOVED } sdmmc_state_t;
typedef enum { SDMMC_OK, SDMMC_IO_ERROR, SDMMC_NOT_READY } sdmmc_result_t;

sdmmc_state_t sdmmc_initialize(void);
sdmmc_state_t sdmmc_status(void);
sdmmc_result_t sdmmc_transfer(void *buffer, uint32_t sector, unsigned count, bool write);
sdmmc_result_t sdmmc_sync(void);
uint32_t sdmmc_sector_count(void);
bool sdmmc_dma_capable(const void *buffer, size_t size);

bool sdmmc_poll_removal(void);
void sdmmc_invalidate(void);
void sdmmc_new_session(void);

/* PA15 低电平有卡；原始电平用于插卡消抖。 */
bool sdmmc_detected(void);
#endif
