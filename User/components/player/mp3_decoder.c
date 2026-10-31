#include "mp3_decoder.h"
#include "log.h"
#include "decoder_memory.h"
static void *s_scratch;
#define MINIMP3_SCRATCH_MEMORY s_scratch
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "minimp3/minimp3.h"
#include <string.h>

#define MP3_INPUT_BYTES 8192U
static mp3dec_t *s_decoder;
static uint8_t *s_input;
static int16_t *s_pcm;
static FIL *s_file;
static uint32_t s_size, s_position, s_samples, s_pcm_position;
static uint32_t s_rate, s_channels, s_duration, s_audio_bytes;
static bool s_eof, s_first;

static uint32_t mp3_be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static bool mp3_next_frame(void)
{
    s_samples = s_pcm_position = 0;
    uint32_t searched = 0;
    while(1) {
        /* 保留未消费的压缩数据，多帧前瞻用于同步；不逐字节访问 SD。 */
        if(s_size - s_position < MP3_INPUT_BYTES / 2 && !s_eof) {
            s_size -= s_position;
            memmove(s_input, s_input + s_position, s_size);
            s_position = 0;
            UINT read = 0;
            UINT wanted = MP3_INPUT_BYTES - s_size;
            FRESULT result = f_read(s_file, s_input + s_size, wanted, &read);
            if(result != FR_OK) {
                LOGE("MP3", "read failed: FatFs=%d", (int)result);
                return false;
            }
            s_size += read;
            s_eof = read < wanted;
        }
        if(s_position == s_size) return true;
        mp3dec_frame_info_t frame = {0};
        int samples = mp3dec_decode_frame(s_decoder, s_input + s_position,
                                         (int)(s_size - s_position), s_pcm, &frame);
        if(!frame.frame_bytes) {
            if(s_eof) return true; /* 忽略末尾 ID3v1、填充或不完整帧。 */
            LOGW("MP3", "cannot synchronize within input buffer");
            return false;
        }
        const uint8_t *header = s_input + s_position + frame.frame_offset;
        s_position += (uint32_t)frame.frame_bytes;
        if(!samples) {
            searched += (uint32_t)frame.frame_bytes;
            if(searched > 65536U) {
                LOGW("MP3", "too much invalid data");
                return false;
            }
            continue;
        }
        if(frame.layer != 3 || (frame.channels != 1 && frame.channels != 2) ||
           frame.hz <= 0 || frame.bitrate_kbps <= 0 ||
           (s_rate && (s_rate != (uint32_t)frame.hz || s_channels != (uint32_t)frame.channels))) {
            LOGW("MP3", "unsupported frame or changing rate/channels");
            return false;
        }
        s_rate = (uint32_t)frame.hz;
        s_channels = (uint32_t)frame.channels;
        s_samples = (uint32_t)samples;
        if(s_first) {
            s_first = false;
            /* Xing/Info 和 VBRI 优先使用帧数；普通 CBR 按首帧码率估算时长。 */
            s_duration = (uint32_t)((uint64_t)s_audio_bytes * 8 / ((unsigned)frame.bitrate_kbps * 1000U));
            unsigned bytes = (unsigned)(frame.frame_bytes - frame.frame_offset);
            bool mpeg1 = (header[1] & 0x18) == 0x18;
            unsigned xing = 4 + ((header[1] & 1) ? 0 : 2) +
                            (mpeg1 ? (s_channels == 1 ? 17 : 32) : (s_channels == 1 ? 9 : 17));
            uint32_t frames = 0;
            bool metadata = bytes >= xing + 12 &&
                            (!memcmp(header + xing, "Xing", 4) || !memcmp(header + xing, "Info", 4));
            if(metadata && (mp3_be32(header + xing + 4) & 1)) frames = mp3_be32(header + xing + 8);
            else if(bytes >= 54 && !memcmp(header + 36, "VBRI", 4)) {
                metadata = true;
                frames = mp3_be32(header + 50);
            }
            if(frames) s_duration = (uint32_t)((uint64_t)frames * (mpeg1 ? 1152 : 576) / s_rate);
            if(metadata) {
                /* 首帧承载索引信息，不把它作为一帧静音插入音频。 */
                s_samples = 0;
                continue;
            }
        }
        return true;
    }
}

bool mp3_decoder_open(FIL *file, music_info_t *info)
{
    decoder_memory_reset();
    s_decoder = decoder_memory_alloc(sizeof(*s_decoder));
    s_input = decoder_memory_alloc(MP3_INPUT_BYTES);
    s_pcm = decoder_memory_alloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(*s_pcm));
    s_scratch = decoder_memory_alloc(sizeof(mp3dec_scratch_t));
    if(!s_decoder || !s_input || !s_pcm || !s_scratch) return false;
    s_file = file;
    s_size = s_position = s_samples = s_pcm_position = s_rate = s_channels = s_duration = 0;
    s_eof = false;
    s_first = true;
    mp3dec_init(s_decoder);
    uint8_t tag[10];
    UINT read;
    if(f_read(file, tag, sizeof(tag), &read) != FR_OK) return false;
    uint32_t offset = 0;
    if(read == sizeof(tag) && !memcmp(tag, "ID3", 3)) {
        /* ID3v2 长度采用 synchsafe 编码，直接跳过大标签及可选 footer。 */
        if(tag[3] < 2 || tag[3] > 4 || ((tag[6] | tag[7] | tag[8] | tag[9]) & 0x80)) return false;
        offset = 10 + ((uint32_t)tag[6] << 21 | (uint32_t)tag[7] << 14 | (uint32_t)tag[8] << 7 | tag[9]);
        if(tag[3] == 4 && (tag[5] & 0x10)) offset += 10;
    }
    s_audio_bytes = f_size(file) >= offset ? f_size(file) - offset : 0;
    if(!s_audio_bytes || f_lseek(file, offset) != FR_OK || !mp3_next_frame() || !s_samples) {
        LOGW("MP3", "skip: no playable Layer III frame");
        return false;
    }
    info->sample_rate_hz = s_rate;
    info->bits_per_sample = 16;
    info->duration_seconds = s_duration;
    LOGI("MP3", "%lu Hz, %lu channel(s), 16-bit PCM, duration=%lu s",
         (unsigned long)s_rate, (unsigned long)s_channels, (unsigned long)s_duration);
    return true;
}

bool mp3_decoder_read(int16_t *stereo, uint32_t frames, uint32_t *decoded)
{
    *decoded = 0;
    while(*decoded < frames) {
        if(s_pcm_position == s_samples) {
            if(!mp3_next_frame()) return false;
            if(!s_samples) break;
        }
        uint32_t count = s_samples - s_pcm_position;
        if(count > frames - *decoded) count = frames - *decoded;
        if(s_channels == 2) memcpy(stereo + *decoded * 2, s_pcm + s_pcm_position * 2, count * 4);
        else for(uint32_t i = 0; i < count; ++i) {
            int16_t sample = s_pcm[s_pcm_position + i];
            stereo[(*decoded + i) * 2] = stereo[(*decoded + i) * 2 + 1] = sample;
        }
        s_pcm_position += count;
        *decoded += count;
    }
    return true;
}
