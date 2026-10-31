#ifndef MOCK_PLATFORM_H
#define MOCK_PLATFORM_H
#include "sdio.h"
#include "FreeRTOS.h"
enum { MOCK_NORMAL, MOCK_START_FAIL, MOCK_DMA_ERROR, MOCK_DMA_ABORT, MOCK_TIMEOUT, MOCK_WRONG_EVENT };
extern int mock_mode, mock_present, mock_init_fail, mock_wide_fail, mock_busy;
void mock_detect_samples(unsigned samples);
extern void (*mock_wait_hook)(void);
extern void (*mock_dma_hook)(void);
extern unsigned mock_task_calls;
extern unsigned mock_busy_reads, mock_post_dma_busy;
extern unsigned mock_dma_calls, mock_max_blocks, mock_aborts, mock_init_calls, mock_wide_calls;
extern void *mock_last_buffer;
extern TickType_t mock_ticks;
extern uint8_t mock_card[MOCK_SECTORS * 512U];
void mock_clear_counters(void);
#endif
