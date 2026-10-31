#ifndef USB_AUDIO_H
#define USB_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

/* USB ISR 仅更新连接状态、写 PCM，不操作 I2S、I2C 或 UI。 */
void usb_audio_host_activity(void);
void usb_audio_disconnect(void);
void usb_audio_set_stream(bool enabled);
void usb_audio_set_mute(bool muted);
void usb_audio_receive(const uint8_t *data, uint32_t bytes);
bool usb_audio_connected(void);

/* 以下接口只由音频任务调用；固定 48 kHz、16 位、双声道。 */
bool usb_audio_start(void);
void usb_audio_stop(void);
void usb_audio_poll(uint32_t timeout_ticks);
bool usb_audio_running(void);
bool usb_audio_audible(void);

#endif
