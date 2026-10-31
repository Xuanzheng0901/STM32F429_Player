#include "ff.h"
#include "diskio.h"
#include "sdmmc.h"

static DSTATUS disk_state(sdmmc_state_t state)
{
    if(state == SDMMC_READY) return 0;
    return state == SDMMC_REMOVED ? STA_NOINIT | STA_NODISK : STA_NOINIT;
}

static DRESULT disk_result(sdmmc_result_t result)
{
    if(result == SDMMC_OK) return RES_OK;
    return result == SDMMC_NOT_READY ? RES_NOTRDY : RES_ERROR;
}

DSTATUS disk_status(BYTE drive)
{
    (void)drive;
    return disk_state(sdmmc_status());
}

DSTATUS disk_initialize(BYTE drive)
{
    (void)drive;
    return disk_state(sdmmc_initialize());
}

DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count)
{
    (void)drive;
    return disk_result(sdmmc_transfer(buffer, sector, count, false));
}

DRESULT disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count)
{
    (void)drive;
    return disk_result(sdmmc_transfer((void *)buffer, sector, count, true));
}

DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer)
{
    (void)drive;
    if(sdmmc_status() != SDMMC_READY) return RES_NOTRDY;
    switch(command)
    {
    case CTRL_SYNC: return disk_result(sdmmc_sync());
    case GET_SECTOR_COUNT: *(LBA_t *)buffer = sdmmc_sector_count(); break;
    case GET_SECTOR_SIZE: *(WORD *)buffer = SDMMC_SECTOR_SIZE; break;
    case GET_BLOCK_SIZE: *(DWORD *)buffer = 1; break;
    default: return RES_PARERR;
    }
    return RES_OK;
}
