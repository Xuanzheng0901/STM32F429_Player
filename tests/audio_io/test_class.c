#include <assert.h>
#include <stdio.h>
#include "usbd_audio_if.h"
#include "usbd_ctlreq.h"
#include "usbd_ioreq.h"
#include "usb_audio.h"

USBD_HandleTypeDef hUsbDeviceFS;
static bool stream, mute;
static uint8_t *receive_buffer, *control_buffer;
static uint32_t received_size;
static unsigned packets, errors;

void usb_audio_set_stream(bool enabled) { stream = enabled; }
void usb_audio_set_mute(bool enabled) { mute = enabled; }
void usb_audio_receive(const uint8_t *data, uint32_t bytes)
{
    assert(data == receive_buffer && bytes == received_size && bytes <= AUDIO_OUT_PACKET);
    ++packets;
}
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *dev, uint8_t ep, uint8_t type, uint16_t size)
{ (void)dev; assert(ep == AUDIO_OUT_EP && type == USBD_EP_TYPE_ISOC && size == 192); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *dev, uint8_t ep)
{ (void)dev; (void)ep; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *dev, uint8_t ep)
{ (void)dev; (void)ep; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *dev, uint8_t ep, uint8_t *data, uint32_t size)
{
    (void)dev; assert(ep == AUDIO_OUT_EP && size == 192);
    if(receive_buffer) assert(data == receive_buffer);
    receive_buffer = data; return USBD_OK;
}
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *dev, uint8_t ep)
{ (void)dev; assert(ep == AUDIO_OUT_EP); return received_size; }
USBD_StatusTypeDef USBD_CtlSendData(USBD_HandleTypeDef *dev, uint8_t *data, uint32_t size)
{ (void)dev; assert(size == 1); control_buffer = data; return USBD_OK; }
USBD_StatusTypeDef USBD_CtlPrepareRx(USBD_HandleTypeDef *dev, uint8_t *data, uint32_t size)
{ (void)dev; assert(size == 1); control_buffer = data; return USBD_OK; }
void USBD_CtlError(USBD_HandleTypeDef *dev, USBD_SetupReqTypedef *req)
{ (void)dev; (void)req; ++errors; }
USBD_DescHeaderTypeDef *USBD_GetNextDesc(uint8_t *desc, uint16_t *offset)
{ *offset += desc[0]; return (USBD_DescHeaderTypeDef *)(desc + desc[0]); }

static void interface(uint16_t index, uint16_t alt, bool accepted)
{
    USBD_SetupReqTypedef request = {.bmRequest = USB_REQ_TYPE_STANDARD,
        .bRequest = USB_REQ_SET_INTERFACE, .wIndex = index, .wValue = alt};
    assert((USBD_AUDIO.Setup(&hUsbDeviceFS, &request) == USBD_OK) == accepted);
}
int main(void)
{
    hUsbDeviceFS.dev_state = USBD_STATE_CONFIGURED;
    hUsbDeviceFS.dev_speed = USBD_SPEED_FULL;
    assert(sizeof(USBD_AUDIO_HandleTypeDef) < 1024);
    assert(USBD_AUDIO_RegisterInterface(&hUsbDeviceFS, &USBD_AUDIO_fops_FS) == USBD_OK);
    assert(USBD_AUDIO.Init(&hUsbDeviceFS, 0) == USBD_OK && !stream && !mute);
    interface(1, 1, true); assert(stream);
    /* 控制接口的 alt=0 不应关闭流接口。 */
    interface(0, 0, true); assert(stream);
    interface(0, 1, false); interface(1, 2, false); assert(errors == 2);
    USBD_SetupReqTypedef request = {.bmRequest = USB_REQ_TYPE_STANDARD,
        .bRequest = USB_REQ_GET_INTERFACE, .wIndex = 0};
    assert(USBD_AUDIO.Setup(&hUsbDeviceFS, &request) == USBD_OK && *control_buffer == 0);
    request.wIndex = 1;
    assert(USBD_AUDIO.Setup(&hUsbDeviceFS, &request) == USBD_OK && *control_buffer == 1);
    for(unsigned i = 0; i < 1000; ++i) {
        received_size = i % 3 == 0 ? 188 : 192;
        assert(USBD_AUDIO.DataOut(&hUsbDeviceFS, AUDIO_OUT_EP) == USBD_OK);
    }
    assert(packets == 1000 && stream);
    request = (USBD_SetupReqTypedef){.bmRequest = USB_REQ_TYPE_CLASS,
        .bRequest = AUDIO_REQ_SET_CUR, .wIndex = AUDIO_OUT_STREAMING_CTRL << 8,
        .wValue = 0x100, .wLength = 1};
    assert(USBD_AUDIO.Setup(&hUsbDeviceFS, &request) == USBD_OK);
    *control_buffer = 1;
    assert(USBD_AUDIO.EP0_RxReady(&hUsbDeviceFS) == USBD_OK && mute);
    request.bRequest = AUDIO_REQ_GET_CUR;
    assert(USBD_AUDIO.Setup(&hUsbDeviceFS, &request) == USBD_OK && *control_buffer == 1);
    interface(1, 0, true); assert(!stream);
    interface(1, 1, true); assert(stream);
    assert(USBD_AUDIO.DeInit(&hUsbDeviceFS, 0) == USBD_OK && !stream);
    puts("PASS: USB interface alt settings, packet boundaries, mute requests and deinit");
    return 0;
}
