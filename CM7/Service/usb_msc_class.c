#include "usb_msc_class.h"
#include "usb_msc_desc.h"
#include "usb_device.h"
#include "usbd_ioreq.h"
#include <stddef.h>
static bool s_in_open, s_out_open, s_configured;
static uint8_t s_reply[4] __attribute__((aligned(4)));
static uint8_t DeInit(USBD_HandleTypeDef *device, uint8_t config)
{
    bool failed = false;
    (void)device; (void)config;
    s_configured = false;
    if (s_out_open) {
        if (USB_Device_CloseEp(USB_MSC_OUT_EP) == SYS_OK) s_out_open = false;
        else failed = true;
    }
    if (s_in_open) {
        if (USB_Device_CloseEp(USB_MSC_IN_EP) == SYS_OK) s_in_open = false;
        else failed = true;
    }
    return failed ? USBD_FAIL : USBD_OK;
}
static uint8_t Init(USBD_HandleTypeDef *device, uint8_t config)
{
    if (device == NULL || config != 1U || s_in_open || s_out_open) return USBD_FAIL;
    if (USB_Device_OpenEp(USB_MSC_IN_EP, USBD_EP_TYPE_BULK, USB_MSC_PACKET_SIZE) != SYS_OK)
        return USBD_FAIL;
    s_in_open = true;
    if (USB_Device_OpenEp(USB_MSC_OUT_EP, USBD_EP_TYPE_BULK, USB_MSC_PACKET_SIZE) != SYS_OK) {
        (void)DeInit(device, config);
        return USBD_FAIL;
    }
    s_out_open = true;
    s_configured = true;
    return USBD_OK;
}
static uint8_t Setup(USBD_HandleTypeDef *device, USBD_SetupReqTypedef *req)
{
    if (device == NULL || req == NULL || !s_configured ||
        device->dev_state != USBD_STATE_CONFIGURED) return USBD_FAIL;
    /* Core already clears endpoint HALT and sends its status before this callback. */
    if (req->bmRequest == 0x02U && req->bRequest == USB_REQ_CLEAR_FEATURE &&
        req->wValue == 0U && req->wLength == 0U &&
        (req->wIndex == USB_MSC_IN_EP || req->wIndex == USB_MSC_OUT_EP)) return USBD_OK;
    if (req->wIndex != 0U) return USBD_FAIL;
    s_reply[0] = 0U; s_reply[1] = 0U;
    if (req->bmRequest == 0x81U && req->wValue == 0U) {
        if (req->bRequest == USB_REQ_GET_INTERFACE && req->wLength == 1U)
            return USBD_CtlSendData(device, s_reply, 1U);
        if (req->bRequest == USB_REQ_GET_STATUS && req->wLength == 2U)
            return USBD_CtlSendData(device, s_reply, 2U);
    }
    if (req->bmRequest == 0x01U && req->bRequest == USB_REQ_SET_INTERFACE &&
        req->wValue == 0U && req->wLength == 0U) return USBD_OK;
    /* Including GET_MAX_LUN and Bulk-Only Reset: BOT is deliberately not implemented. */
    return USBD_FAIL;
}
static uint8_t *NoDescriptor(uint16_t *length)
{
    if (length != NULL) *length = 0U;
    return NULL;
}
static USBD_ClassTypeDef s_class = {
    .Init = Init, .DeInit = DeInit, .Setup = Setup,
    .GetFSConfigDescriptor = USB_MSC_GetFSConfigDescriptor,
    .GetHSConfigDescriptor = NoDescriptor,
    .GetOtherSpeedConfigDescriptor = NoDescriptor,
    .GetDeviceQualifierDescriptor = NoDescriptor
};
SysResult_t USB_MSC_Register(void)
{
    return USB_Device_RegisterClass(&s_class, USB_MSC_GetDescriptors());
}
