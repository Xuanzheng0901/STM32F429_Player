#ifndef AUDIO_VOLUME_H
#define AUDIO_VOLUME_H

/* ES9018 衰减寄存器范围，值越小声音越大，每单位为 0.5 dB。 */
#define AUDIO_ATTENUATION_MIN 64
#define AUDIO_ATTENUATION_MAX 192
#define AUDIO_VOLUME_STEP 8

#endif
