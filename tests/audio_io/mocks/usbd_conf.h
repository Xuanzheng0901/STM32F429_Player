#ifndef TEST_USBD_CONF_H
#define TEST_USBD_CONF_H
#include <stdint.h>
#define __PACKED __attribute__((packed))
#define __IO volatile
#define __STATIC_INLINE static inline
#define __ALIGN_BEGIN
#define __ALIGN_END __attribute__((aligned(4)))
#include <stdlib.h>
#include <string.h>
#include "usb_audio_format.h"
#define USBD_AUDIO_FREQ USB_AUDIO_SAMPLE_RATE
#define USBD_malloc malloc
#define USBD_free free
#define USBD_memset memset
#define USBD_memcpy memcpy
#define USBD_ErrLog(...)
#define USBD_DbgLog(...)
#define UNUSED(x) ((void)(x))
#endif
