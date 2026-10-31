#include "mock_platform.h"
#include "ff.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "diskio.h"
#include "sdmmc.h"

static void remove_while_waiting(void)
{
    mock_present = 0;
    mock_wait_hook = NULL;
}

int main(void)
{
    uint8_t unaligned[9 * 512 + 1];
    mock_present = 0;
    assert(disk_initialize(0) == (STA_NOINIT | STA_NODISK));
    mock_present = 1;
    sdmmc_new_session();
    mock_init_fail = 1;
    assert(disk_initialize(0) & STA_NOINIT);
    mock_init_fail = 0;
    mock_wide_fail = 1;
    assert(disk_initialize(0) & STA_NOINIT);
    mock_wide_fail = 0;
    assert(disk_initialize(0) == 0);
    assert(mock_init_calls && mock_wide_calls);
    assert(!sdmmc_dma_capable((void *)0x10000000U, 512));
    assert(sdmmc_dma_capable(mock_sram, sizeof(mock_sram)));
    assert(!sdmmc_dma_capable(mock_sram + 1, 512));
    assert(!sdmmc_dma_capable(mock_sram + sizeof(mock_sram) - 4, 8));
    assert(!sdmmc_dma_capable(mock_sram + sizeof(mock_sram), 4));

    for(unsigned i = 0; i < 9 * 512; i++) mock_card[100 * 512 + i] = (uint8_t)(i * 7);
    mock_clear_counters();
    assert(disk_read(0, unaligned + 1, 100, 9) == RES_OK);
    assert(memcmp(unaligned + 1, mock_card + 100 * 512, 9 * 512) == 0);
    assert(mock_dma_calls == 2 && mock_max_blocks == 8 && !((uintptr_t)mock_last_buffer & 3) && mock_last_buffer != unaligned + 1);
    void *bounce_buffer = mock_last_buffer;
    memset(unaligned + 1, 0x5a, 9 * 512);
    mock_clear_counters();
    assert(disk_write(0, unaligned + 1, 200, 9) == RES_OK);
    assert(memcmp(unaligned + 1, mock_card + 200 * 512, 9 * 512) == 0);
    assert(mock_dma_calls == 2 && mock_max_blocks == 8 && mock_last_buffer == bounce_buffer);
    memset(mock_sram, 0x3c, sizeof(mock_sram));
    mock_clear_counters();
    assert(disk_write(0, mock_sram, 1000, 600) == RES_OK);
    assert(mock_dma_calls == 2 && mock_max_blocks == 511);
    memset(mock_sram, 0, sizeof(mock_sram));
    assert(disk_read(0, mock_sram, 1000, 600) == RES_OK);
    for(unsigned i = 0; i < sizeof(mock_sram); i++) assert(mock_sram[i] == 0x3c);
    assert(mock_last_buffer == mock_sram + 511 * 512);

    mock_post_dma_busy = 3;
    TickType_t before = mock_ticks;
    assert(disk_write(0, mock_sram, 0, 1) == RES_OK);
    assert((TickType_t)(mock_ticks - before) == 3);
    mock_post_dma_busy = 0;
    mock_ticks = UINT32_MAX - 1;
    mock_busy_reads = 3;
    assert(disk_ioctl(0, CTRL_SYNC, NULL) == RES_OK && mock_ticks == 1);

    LBA_t sectors;
    WORD size;
    DWORD block;
    assert(disk_ioctl(0, GET_SECTOR_COUNT, &sectors) == RES_OK && sectors == MOCK_SECTORS);
    assert(disk_ioctl(0, GET_SECTOR_SIZE, &size) == RES_OK && size == 512);
    assert(disk_ioctl(0, GET_BLOCK_SIZE, &block) == RES_OK && block == 1);
    assert(disk_ioctl(0, CTRL_SYNC, NULL) == RES_OK);
    assert(disk_ioctl(0, 255, &block) == RES_PARERR);
    for(int mode = MOCK_START_FAIL; mode <= MOCK_WRONG_EVENT; mode++)
    {
        mock_clear_counters();
        mock_mode = mode;
        assert(disk_read(0, mock_sram, 0, 1) == RES_ERROR);
        assert(mock_aborts == 1 && (disk_status(0) & STA_NOINIT));
        assert(disk_read(0, mock_sram, 0, 1) == RES_NOTRDY);
        mock_mode = MOCK_NORMAL;
        assert(disk_initialize(0) == 0);
    }
    mock_clear_counters();
    mock_mode = MOCK_TIMEOUT;
    mock_wait_hook = remove_while_waiting;
    before = mock_ticks;
    assert(disk_read(0, mock_sram, 0, 1) == RES_NOTRDY);
    assert(mock_ticks - before == 10 && mock_aborts == 1);
    mock_present = 1;
    assert(disk_initialize(0) & STA_NOINIT);
    mock_mode = MOCK_NORMAL;
    sdmmc_new_session();
    assert(disk_initialize(0) == 0);
    sdmmc_invalidate();
    assert(disk_read(0, mock_sram, 0, 1) == RES_NOTRDY);
    assert(disk_write(0, mock_sram, 0, 1) == RES_NOTRDY);
    assert(disk_ioctl(0, CTRL_SYNC, NULL) == RES_NOTRDY);
    sdmmc_new_session();
    assert(disk_initialize(0) == 0);
    mock_busy = 1;
    assert(disk_ioctl(0, CTRL_SYNC, NULL) == RES_ERROR);
    mock_busy = 0;
    mock_present = 0;
    assert(disk_status(0) == (STA_NOINIT | STA_NODISK));
    assert(disk_read(0, mock_sram, 0, 1) == RES_NOTRDY);
    puts("SDIO init, DMA direct/bounce, block limits, callbacks and failure recovery passed");
}
