#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "fatfs.h"
#include "diskio.h"
#include "i2s.h"
#include "audio_player.h"
#include "stm32f4xx_hal_rcc_ex.h"
#include "es9018.h"

char SDPath[4] = "0:";
FATFS SDFatFS;
I2S_HandleTypeDef hi2s2 = {.State = HAL_I2S_STATE_READY};
static FILE *image, *expected;
static const char *fixture_dir, *mode;
static uint16_t *audio;
static unsigned half_words, next_half, starts, stops, completed, skips, init_calls;
static unsigned notifications, unaligned_reads, mount_errors, reported_errors;
static int active, injected, paused;
static const uint32_t rates[] = {44100, 48000, 22050, 32000, 44100};
static const unsigned clock_n[] = {271, 258, 429, 426, 271};
static const unsigned clock_r[] = {6, 3, 4, 4, 6};
static unsigned clock_configured;
static unsigned lock_reads, silent_halves;
static int dac_locked, stream_started;

HAL_StatusTypeDef ES9018_set_format(ES9018_SerialFormat format, ES9018_WordLength bits)
{
    assert(!active && format == ES9018_FORMAT_I2S && bits == ES9018_WORD_LENGTH_16);
    return HAL_OK;
}

HAL_StatusTypeDef ES9018_read_register(uint8_t reg, uint8_t *value)
{
    assert(active && reg == ES9018_REG_CHIP_STATUS && !stream_started);
    ++lock_reads;
    if(strcmp(mode, "dac_error") == 0 && !injected) { injected = 1; return HAL_ERROR; }
    if(strcmp(mode, "lock_timeout") == 0 && starts == 1) {
        injected = 1; *value = 0x10; return HAL_OK;
    }
    if(strcmp(mode, "lock_delay") == 0 && (uint64_t)lock_reads * (half_words / 2) * 1000 < (uint64_t)rates[starts - 1] * 400) { *value = 0x10; return HAL_OK; }
    dac_locked = 1; *value = 0x10 | ES9018_STATUS_LOCK; return HAL_OK;
}

HAL_I2S_StateTypeDef HAL_I2S_GetState(I2S_HandleTypeDef *handle) { return handle->State; }
void test_i2s_disable(I2S_HandleTypeDef *handle)
{ assert(handle == &hi2s2 && !active); clock_configured = 0; }
void HAL_RCCEx_GetPeriphCLKConfig(RCC_PeriphCLKInitTypeDef *config) { memset(config, 0, sizeof(*config)); }
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *config)
{
    assert(!active && starts < 5 && config->PeriphClockSelection == RCC_PERIPHCLK_I2S);
    assert(config->PLLI2S.PLLI2SN == clock_n[starts] && config->PLLI2S.PLLI2SR == clock_r[starts]);
    if(strcmp(mode, "clock_error") == 0 && !injected) { injected = 1; return HAL_ERROR; }
    clock_configured = 1; return HAL_OK;
}

static FILE *open_fixture(const char *name)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", fixture_dir, name);
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    return file;
}

void test_log(const char *tag, const char *format, ...)
{
    char text[512];
    va_list args; va_start(args, format); vsnprintf(text, sizeof(text), format, args); va_end(args);
    printf("%s: %s\n", tag, text);
    if(strstr(text, "playback complete")) ++completed;
    if(strstr(text, "skip:")) ++skips;
    if(strstr(text, "mount failed")) ++mount_errors;
    if(strstr(text, "read failed") || strstr(text, "DMA timeout") ||
       strstr(text, "missed deadline") || strstr(text, "DAC lock timeout")) ++reported_errors;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void *)1; }
TickType_t xTaskGetTickCount(void) { return 0; }
BaseType_t xTaskNotifyFromISR(TaskHandle_t task, uint32_t bits, eNotifyAction action, BaseType_t *wake)
{
    assert(task == (void *)1 && action == eSetBits); *wake = pdFALSE; notifications |= bits; return pdTRUE;
}

static void complete_half(void)
{
    const uint8_t *data = (const uint8_t *)(audio + next_half * half_words);
    bool silence = true;
    for(unsigned i = 0; i < half_words * 2; ++i) if(data[i]) { silence = false; break; }
    if(!stream_started && silence) {
        ++silent_halves;
        assert(ftell(expected) == 0); /* Preroll must not consume the WAV beginning. */
        assert(audio_player_elapsed() == 0);
    } else if(!stream_started) {
        assert(dac_locked && silent_halves >= 2);
        if(strcmp(mode, "lock_delay") == 0)
            assert((uint64_t)silent_halves * (half_words / 2) * 1000 >= (uint64_t)rates[starts - 1] * 400);
        stream_started = 1;
    }
    /* Model what DMA consumed; compare every sample, including mono expansion
       and the final zero padding. This checks stream order across refills. */
    if(stream_started && (starts > 1 || (strcmp(mode, "read_error") != 0 && strcmp(mode, "late_read") != 0)))
    {
        for(unsigned i = 0; i < half_words * 2; ++i)
        {
            int byte = fgetc(expected);
            assert(data[i] == (byte == EOF ? 0 : byte));
        }
    }
    if(next_half == 0) HAL_I2S_TxHalfCpltCallback(&hi2s2);
    else HAL_I2S_TxCpltCallback(&hi2s2);
    next_half ^= 1;
}

BaseType_t xTaskNotifyWait(uint32_t clear_entry, uint32_t clear_exit, uint32_t *events, uint32_t ticks)
{
    (void)clear_entry; (void)clear_exit;
    if(!notifications && ticks)
    {
        assert(active && !paused);
        if(!injected && strcmp(mode, "dma_error") == 0)
        {
            injected = 1; HAL_I2S_ErrorCallback(&hi2s2);
        }
        else if(strcmp(mode, "timeout") == 0 && !injected) injected = 1;
        else complete_half();
    }
    *events = notifications; notifications = 0;
    return *events ? pdTRUE : pdFALSE;
}

HAL_StatusTypeDef HAL_I2S_Init(I2S_HandleTypeDef *handle)
{
    assert(handle == &hi2s2 && starts < 5);
    assert(handle->Init.AudioFreq == rates[starts]);
    assert(clock_configured && !active && handle->Init.DataFormat == I2S_DATAFORMAT_16B);
    if(strcmp(mode, "start_error") == 0 && !injected) {
        injected = 1;
        return HAL_ERROR;
    }
    return HAL_OK;
}
HAL_StatusTypeDef HAL_I2S_Transmit_DMA(I2S_HandleTypeDef *handle, uint16_t *data, uint16_t words)
{
    assert(handle == &hi2s2 && !active && words % 4 == 0 && ((uintptr_t)data & 3) == 0);
    assert(words == 24576); /* 48 KiB DMA，16 位双声道共 12288 帧。 */
    audio = data; half_words = words / 2; next_half = 0; active = 1;
    lock_reads = silent_halves = 0;
    dac_locked = stream_started = 0;
    handle->State = HAL_I2S_STATE_BUSY_TX;
    char name[64]; snprintf(name, sizeof(name), "expected%u.pcm", starts);
    expected = open_fixture(name); ++starts;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_I2S_DMAStop(I2S_HandleTypeDef *handle)
{
    assert(handle == &hi2s2 && active);
    if(strcmp(mode, "normal") == 0 || strcmp(mode, "pause_resume") == 0 ||
       strcmp(mode, "lock_delay") == 0 || starts > 1) {
        assert(stream_started && fgetc(expected) == EOF);
    }
    fclose(expected); expected = NULL; active = 0; ++stops;
    handle->State = HAL_I2S_STATE_READY;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_I2S_DMAPause(I2S_HandleTypeDef *handle)
{ assert(handle == &hi2s2 && active && !paused); paused = 1; return HAL_OK; }
HAL_StatusTypeDef HAL_I2S_DMAResume(I2S_HandleTypeDef *handle)
{ assert(handle == &hi2s2 && active && paused); paused = 0; return HAL_OK; }

DSTATUS disk_initialize(BYTE drive)
{
    assert(drive == 0); ++init_calls;
    return strcmp(mode, "absent") == 0 ? STA_NOINIT : 0;
}
DSTATUS disk_status(BYTE drive) { assert(drive == 0); return strcmp(mode,"absent") == 0 ? STA_NOINIT : 0; }
DRESULT disk_read(BYTE drive, BYTE *buffer, DWORD sector, UINT count)
{
    assert(drive == 0 && count != 0);
    if((uintptr_t)buffer & 3) ++unaligned_reads;
    if(active && !injected && strcmp(mode, "read_error") == 0)
    {
        injected = 1; return RES_ERROR;
    }
    if(active && !injected && strcmp(mode, "late_read") == 0)
    {
        injected = 1; complete_half();
    }
    assert(fseek(image, (long)sector * 512, SEEK_SET) == 0);
    return fread(buffer, 512, count, image) == count ? RES_OK : RES_ERROR;
}
DRESULT disk_write(BYTE drive, const BYTE *buffer, DWORD sector, UINT count)
{
    (void)drive; (void)buffer; (void)sector; (void)count; assert(0); return RES_WRPRT;
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer)
{
    assert(drive == 0);
    if(command == GET_SECTOR_COUNT) *(DWORD *)buffer = 131072;
    else if(command == GET_BLOCK_SIZE) *(DWORD *)buffer = 1;
    else if(command != CTRL_SYNC) return RES_PARERR;
    return RES_OK;
}
DWORD get_fattime(void) { return 0; }
void *ff_memalloc(UINT size) { return malloc(size); }
void ff_memfree(void *memory) { free(memory); }

int main(int argc, char **argv)
{
    assert(argc == 3); fixture_dir = argv[1]; mode = argv[2]; image = open_fixture("card.img");
    FRESULT result = f_mount(&SDFatFS, "0:", 1);
    if(strcmp(mode, "absent") == 0) assert(result != FR_OK);
    else {
        assert(result == FR_OK);
        DIR directory;
        FILINFO entry;
        assert(f_opendir(&directory, "0:/") == FR_OK);
        while(1) {
            assert(f_readdir(&directory, &entry) == FR_OK);
            if(!entry.fname[0]) break;
            unsigned len = 0;
            while(entry.fname[len]) ++len;
            if(len < 4 || (entry.fname[len - 3] != 'W' && entry.fname[len - 3] != 'w')) continue;
            music_info_t info = {.type = MUSIC_WAV};
            memcpy(info.path, entry.fname, (len + 1) * sizeof(TCHAR));
            if(!audio_player_start(&info)) {
                assert(audio_player_state() == AUDIO_ERROR);
                if((strcmp(mode, "start_error") == 0 || strcmp(mode, "clock_error") == 0) && starts == 0) {
                    assert(injected && starts == 0);
                    assert(audio_player_start(&info));
                } else continue;
            }
            assert(info.bits_per_sample == 16 && info.sample_rate_hz == rates[starts - 1]);
            if(strcmp(mode, "pause_resume") == 0) {
                uint32_t before = audio_player_elapsed();
                assert(audio_player_toggle() && audio_player_state() == AUDIO_PAUSED);
                assert(audio_player_poll(0) == AUDIO_PAUSED && audio_player_elapsed() == before);
                assert(audio_player_toggle() && audio_player_state() == AUDIO_PLAYING);
                /* A DMA half can complete immediately before a pause request. */
                if(starts == 1) {
                    complete_half();
                    assert(audio_player_toggle());
                    assert(audio_player_poll(0) == AUDIO_PAUSED);
                    assert(audio_player_toggle());
                }
            }
            if((strcmp(mode, "switch") == 0 || strcmp(mode, "switch_preroll") == 0) && starts == 1) {
                assert(audio_player_poll(1000) == AUDIO_PLAYING);
                if(strcmp(mode, "switch") == 0) {
                    while(!stream_started) assert(audio_player_poll(1000) == AUDIO_PLAYING);
                }
                continue; /* Next start must stop and close the old file. */
            }
            bool paused_during_song = false;
            while(audio_player_state() == AUDIO_PLAYING) {
                audio_player_poll(1000);
                if(strcmp(mode, "pause_resume") == 0 && starts == 1 && stream_started && !paused_during_song) {
                    uint32_t before = audio_player_elapsed();
                    assert(audio_player_toggle() && audio_player_state() == AUDIO_PAUSED);
                    assert(audio_player_poll(0) == AUDIO_PAUSED && audio_player_elapsed() == before);
                    assert(audio_player_toggle() && audio_player_state() == AUDIO_PLAYING);
                    paused_during_song = true;
                }
            }
            assert(audio_player_state() == AUDIO_FINISHED || audio_player_state() == AUDIO_ERROR);
        }
        assert(f_closedir(&directory) == FR_OK);
    }
    assert(!active && expected == NULL && init_calls == 1);
    if(strcmp(mode, "absent") == 0)
        assert(starts == 0 && mount_errors == 0);
    else
    {
        assert(starts == 5 && stops == 5 && skips == 2 && mount_errors == 0);
        if(strcmp(mode, "normal") == 0 || strcmp(mode, "pause_resume") == 0 ||
           strcmp(mode, "start_error") == 0 || strcmp(mode, "clock_error") == 0 ||
           strcmp(mode, "lock_delay") == 0 || strcmp(mode, "dac_error") == 0)
            assert(completed == 5 && reported_errors == 0 && unaligned_reads > 0);
        else if(strcmp(mode, "switch") == 0 || strcmp(mode, "switch_preroll") == 0)
            assert(completed == 4 && reported_errors == 0);
        else assert(injected && completed == 4 && reported_errors == 1);
    }
    fclose(image);
    printf("PASS: %s (real FatFs/FAT32, Unicode paths, exact PCM and EOF checks)\n", mode);
    return 0;
}

bool ES9018_is_locked(void)
{
    uint8_t status;
    return ES9018_read_register(ES9018_REG_CHIP_STATUS, &status) == HAL_OK &&
           (status & ES9018_STATUS_LOCK) != 0;
}
int ff_mutex_create(int volume) { (void)volume; return 1; }
void ff_mutex_delete(int volume) { (void)volume; }
int ff_mutex_take(int volume) { (void)volume; return 1; }
void ff_mutex_give(int volume) { (void)volume; }
