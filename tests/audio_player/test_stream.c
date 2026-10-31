#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "fatfs.h"
#include "diskio.h"
#include "audio_player.h"
#include "mp3_decoder.h"
#include "audio_i2s.h"
#include "stm32f4xx_hal_rcc_ex.h"
#include "es9018.h"

FATFS SDFatFS;
I2S_HandleTypeDef hi2s2 = {.State = HAL_I2S_STATE_READY};
static FILE *image, *expected;
static const char *directory, *mode;
static uint16_t *audio;
static unsigned half_words, next_half, notifications, halves, locks, silent;
static unsigned track, active_track, rate, starts, stops, source_bits, sample_words;
static unsigned preroll_halves;
static bool active, paused, started, injected;
static bool expect_error;
static TickType_t ticks;
static unsigned slow_logs, deadline_logs;
static music_type_t active_type;

static FILE *fixture(const char *name)
{
    char path[1024]; snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE *file = fopen(path, "rb"); assert(file); return file;
}
void test_log(const char *tag, const char *format, ...)
{
    char message[512];
    va_list args; va_start(args, format); vsnprintf(message, sizeof(message), format, args); va_end(args);
    printf("%s: %s\n", tag, message);
    if(strstr(message, "refill=40 ms, file read=40 ms, budget=32 ms")) ++slow_logs;
    if(strstr(message, "audio refill missed deadline: refill=40 ms, budget=32 ms")) ++deadline_logs;
}
HAL_I2S_StateTypeDef HAL_I2S_GetState(I2S_HandleTypeDef *h) { return h->State; }
void test_i2s_disable(I2S_HandleTypeDef *h) { assert(h == &hi2s2 && !active); }
void HAL_RCCEx_GetPeriphCLKConfig(RCC_PeriphCLKInitTypeDef *c) { memset(c, 0, sizeof(*c)); }
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *c)
{ assert(!active && c->PeriphClockSelection == RCC_PERIPHCLK_I2S); return HAL_OK; }
HAL_StatusTypeDef HAL_I2S_Init(I2S_HandleTypeDef *h)
{
    assert(!active && h->Init.AudioFreq == rate);
    assert(h->Init.DataFormat == (source_bits == 16 ? I2S_DATAFORMAT_16B :
                                 source_bits == 24 ? I2S_DATAFORMAT_24B : I2S_DATAFORMAT_32B));
    return HAL_OK;
}
HAL_StatusTypeDef ES9018_set_format(ES9018_SerialFormat format, ES9018_WordLength bits)
{
    assert(!active && format == ES9018_FORMAT_I2S);
    assert(bits == (source_bits == 16 ? ES9018_WORD_LENGTH_16 : ES9018_WORD_LENGTH_32));
    if(!strcmp(mode, "format_error") && !injected) { injected = true; return HAL_ERROR; }
    return HAL_OK;
}
HAL_StatusTypeDef ES9018_read_register(uint8_t reg, uint8_t *value)
{
    assert(reg == ES9018_REG_CHIP_STATUS && active && !started);
    ++locks;
    *value = strcmp(mode, "lock_delay") || (uint64_t)locks * (half_words / (2 * sample_words)) * 1000 >= rate * 400U ? ES9018_STATUS_LOCK : 0;
    if(*value & ES9018_STATUS_LOCK) preroll_halves = halves + 1;
    return HAL_OK;
}
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void *)1; }
TickType_t xTaskGetTickCount(void) { return ticks; }
BaseType_t xTaskNotifyFromISR(TaskHandle_t t, uint32_t bits, eNotifyAction a, BaseType_t *wake)
{ assert(t == (void *)1 && a == eSetBits); notifications |= bits; *wake = pdFALSE; return pdTRUE; }

static void complete_half(void)
{
    int16_t *pcm = (int16_t *)(audio + next_half * half_words);
    bool zero = true;
    for(unsigned i = 0; i < half_words; ++i) if(pcm[i]) { zero = false; break; }
    if(!started && (!preroll_halves || halves < preroll_halves)) {
        assert(zero);
        ++silent; assert(ftell(expected) == 0 && audio_player_elapsed() == 0);
    } else {
        if(!started) {
            assert(locks && silent >= 2);
            if(!strcmp(mode, "lock_delay")) assert(silent >= locks + 1);
            started = true;
        }
        /* Independent FFmpeg reference: allow rounding differences up to 2 LSB. */
        for(unsigned i = 0; i < half_words; i += sample_words) {
            int32_t reference = 0;
            int32_t actual;
            if(sample_words == 1) {
                int16_t value = 0;
                if(fread(&value, sizeof(value), 1, expected) == 1) reference = value;
                actual = pcm[i];
            } else {
                if(fread(&reference, sizeof(reference), 1, expected) != 1) reference = 0;
                actual = (int32_t)(((uint32_t)(uint16_t)pcm[i] << 16) | (uint16_t)pcm[i + 1]);
            }
            int tolerance = active_type == MUSIC_MP3 ? 2 : 0;
            int64_t difference = (int64_t)actual - reference;
            if(!expect_error && (difference < -tolerance || difference > tolerance)) {
                fprintf(stderr, "PCM mismatch track=%u byte=%ld got=%ld want=%ld\n", track, ftell(expected), (long)actual, (long)reference);
                abort();
            }
        }
    }
    ++halves;
    if(next_half == 0) HAL_I2S_TxHalfCpltCallback(&hi2s2);
    else HAL_I2S_TxCpltCallback(&hi2s2);
    next_half ^= 1;
}
BaseType_t xTaskNotifyWait(uint32_t entry, uint32_t exit, uint32_t *events, uint32_t timeout)
{
    (void)entry; (void)exit;
    if(!notifications && timeout) { assert(active && !paused); complete_half(); }
    *events = notifications; notifications = 0; return *events ? pdTRUE : pdFALSE;
}
HAL_StatusTypeDef HAL_I2S_Transmit_DMA(I2S_HandleTypeDef *h, uint16_t *data, uint16_t words)
{
    assert(!active && h == &hi2s2 && !((uintptr_t)data & 3));
    sample_words = h->Init.DataFormat == I2S_DATAFORMAT_16B ? 1 : 2;
    /* 与 STM32 HAL 一致：宽格式 Size 为样本数，DMA 实际搬运两倍半字。 */
    assert(words * sample_words == 12288);
    audio = data; half_words = words * sample_words / 2; next_half = locks = silent = halves = preroll_halves = 0;
    started = paused = false; active = true; ++starts;
    active_track = track;
    char name[64]; snprintf(name, sizeof(name), "expected%u.pcm", track); expected = fixture(name);
    h->State = HAL_I2S_STATE_BUSY_TX; return HAL_OK;
}
HAL_StatusTypeDef HAL_I2S_DMAStop(I2S_HandleTypeDef *h)
{
    assert(active);
    if(!expect_error && ((strcmp(mode, "read_error") && strcmp(mode, "switch")) || active_track != 0) &&
       (strcmp(mode, "slow_read") || active_track != 3)) assert(fgetc(expected) == EOF);
    fclose(expected); expected = NULL; active = false; ++stops; h->State = HAL_I2S_STATE_READY; return HAL_OK;
}
HAL_StatusTypeDef HAL_I2S_DMAPause(I2S_HandleTypeDef *h) { (void)h; assert(active && !paused); paused = true; return HAL_OK; }
HAL_StatusTypeDef HAL_I2S_DMAResume(I2S_HandleTypeDef *h) { (void)h; assert(active && paused); paused = false; return HAL_OK; }
DSTATUS disk_initialize(BYTE d) { assert(d == 0); return 0; }
DSTATUS disk_status(BYTE d) { assert(d == 0); return 0; }
DRESULT disk_read(BYTE d, BYTE *buffer, DWORD sector, UINT count)
{
    assert(!d && count);
    if(active && !injected && !strcmp(mode, "read_error")) { injected = true; return RES_ERROR; }
    if(active && !injected && !strcmp(mode, "slow_read") && track == 3) {
        /* 24 位输出只有 32 ms，模拟读卡耗时 40 ms 且 DMA 已切到下一半。 */
        injected = true; ticks += 40; complete_half();
    }
    assert(!fseek(image, (long)sector * 512, SEEK_SET));
    return fread(buffer, 512, count, image) == count ? RES_OK : RES_ERROR;
}
DRESULT disk_write(BYTE d, const BYTE *p, DWORD sector, UINT n)
{ (void)d; (void)p; (void)sector; (void)n; assert(0); return RES_WRPRT; }
DRESULT disk_ioctl(BYTE d, BYTE command, void *p)
{ (void)d; (void)command; (void)p; return RES_OK; }
DWORD get_fattime(void) { return 0; }
void *ff_memalloc(UINT size) { return malloc(size); }
void ff_memfree(void *p) { free(p); }

static music_info_t info_for(const char *name)
{
    music_info_t info = {.type = strstr(name, ".WAV") ? MUSIC_WAV : strstr(name, ".FLAC") ? MUSIC_FLAC : MUSIC_MP3};
    for(unsigned i = 0; name[i]; ++i) info.path[i] = (TCHAR)name[i];
    return info;
}
int main(int argc, char **argv)
{
    assert(argc == 3); directory = argv[1]; mode = argv[2]; image = fixture("card.img");
    assert(f_mount(&SDFatFS, "0:", 1) == FR_OK);
    FILE *manifest = fixture("manifest.txt");
    char name[32], line[128];
    while(fgets(line, sizeof(line), manifest)) {
        unsigned duration = 0;
        source_bits = 16;
        assert(sscanf(line, "%31s %u %u %u", name, &rate, &duration, &source_bits) >= 2);
        music_info_t info = info_for(name);
        active_type = info.type;
        if(!strcmp(mode, "format_error") && !injected) {
            assert(!audio_player_start(&info) && injected && !active && audio_player_state() == AUDIO_ERROR);
        }
        assert(audio_player_start(&info));
        assert(info.sample_rate_hz == rate && info.bits_per_sample == source_bits);
        if(duration) assert(info.duration_seconds == duration);
        else if(track != 4) assert(info.duration_seconds >= 3 && info.duration_seconds <= 4);
        /* 无 Xing 的 VBR 仅按首帧码率估算，短文件可能不足一秒。 */
        bool toggled = false;
        while(audio_player_state() == AUDIO_PLAYING) {
            audio_player_poll(1000);
            if(!strcmp(mode, "pause_resume") && !toggled && started && audio_player_state() == AUDIO_PLAYING) {
                uint32_t elapsed = audio_player_elapsed();
                assert(audio_player_toggle() && audio_player_poll(0) == AUDIO_PAUSED);
                assert(audio_player_elapsed() == elapsed && audio_player_toggle()); toggled = true;
            }
            if(!strcmp(mode, "switch") && track == 0 && started) break;
            assert(halves < (uint64_t)rate * ((duration ? duration : 4) + 2) / (3072 / sample_words) + 40);
        }
        if((!strcmp(mode, "read_error") && track == 0) || (!strcmp(mode, "slow_read") && track == 3))
            assert(injected && audio_player_state() == AUDIO_ERROR);
        else if(strcmp(mode, "switch") || track != 0) {
            assert(audio_player_state() == AUDIO_FINISHED && audio_player_elapsed() >= 3);
        }
        ++track;
    }
    assert(track >= 12 && starts == track && stops == track && !active);
    if(!strcmp(mode, "slow_read")) assert(slow_logs == 1 && deadline_logs == 1);
    fclose(manifest);
#ifndef TEST_FLAC
    /* VBR Xing/Info duration includes all audio frames, independent of first-frame bitrate. */
    FIL file; music_info_t info = info_for("XING.MP3");
    assert(f_open(&file, info.path, FA_READ) == FR_OK);
    assert(mp3_decoder_open(&file, &info) && info.duration_seconds == 3);
    assert(f_close(&file) == FR_OK);
    const char *bad[] = {"BROKEN.MP3", "BADTAG.MP3", "GARBAGE.MP3"};
    for(unsigned i = 0; i < 3; ++i) { info = info_for(bad[i]); assert(!audio_player_start(&info) && audio_player_state() == AUDIO_ERROR); }
#else
    const char *bad[] = {"BROKEN.FLAC", "BIGBLOCK.FLAC", "MULTI.FLAC", "BADRATE.FLAC", "FLOAT.WAV", "BADGUID.WAV"};
    for(unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        music_info_t info = info_for(bad[i]);
        assert(!audio_player_start(&info) && audio_player_state() == AUDIO_ERROR && !active);
    }
    /* CRC 损坏和截断不能当作正常 EOF，随后仍可重新打开另一种格式。 */
    const char *damaged[] = {"CORRUPT.FLAC", "TRUNC.FLAC"};
    for(unsigned i = 0; i < 2; ++i) {
        track = 0; rate = 44100; source_bits = 16; active_type = MUSIC_FLAC; expect_error = true;
        music_info_t info = info_for(damaged[i]);
        assert(audio_player_start(&info));
        while(audio_player_state() == AUDIO_PLAYING) { audio_player_poll(1000); assert(halves < 200); }
        assert(audio_player_state() == AUDIO_ERROR && !active);
    }
    expect_error = false; track = 2; source_bits = 16; active_type = MUSIC_MP3;
    music_info_t info = info_for("TRACK2.MP3");
    assert(audio_player_start(&info));
    while(audio_player_state() == AUDIO_PLAYING) audio_player_poll(1000);
    assert(audio_player_state() == AUDIO_FINISHED && !active);
#endif
    audio_player_stop(); fclose(image);
#ifdef TEST_FLAC
    printf("PASS: %s, exact native-depth FLAC/WAV PCM, 16/24/32-bit sources, shared MP3 switching and error recovery\n", mode);
#else
    printf("PASS: %s, FFmpeg PCM comparison, nine MP3 sample rates, CBR/VBR, ID3, WAV switching and invalid files\n", mode);
#endif
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
