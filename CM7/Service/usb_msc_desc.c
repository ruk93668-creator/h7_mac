/* MSC-1: internal enumeration identity, not a production VID/PID allocation. */
#include "usb_msc_desc.h"
#include "usbd_ctlreq.h"
#include <stddef.h>
static uint8_t s_device[20] __attribute__((aligned(4))) = {
    18, 1, 0, 2, 0, 0, 0, 64, 0x09, 0x12, 0x01, 0, 0, 1, 1, 2, 0, 1
};
/* Protocol length excludes tail padding required by word-oriented PIO. */
static uint8_t s_config[36] __attribute__((aligned(4))) = {
    9, 2, 32, 0, 1, 1, 0, 0x80, 250,
    9, 4, 0, 0, 2, 0x08, 0x06, 0x50, 0,
    7, 5, USB_MSC_IN_EP, 2, USB_MSC_PACKET_SIZE, 0, 0,
    7, 5, USB_MSC_OUT_EP, 2, USB_MSC_PACKET_SIZE, 0, 0
};
static uint8_t s_language[4] = {4, 3, 0x09, 0x04};
static uint8_t s_string[64] __attribute__((aligned(4)));
static uint8_t *Device(USBD_SpeedTypeDef speed, uint16_t *length)
{
    if (length == NULL) return NULL;
    *length = speed == USBD_SPEED_FULL ? 18U : 0U;
    return *length != 0U ? s_device : NULL;
}
static uint8_t *Language(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    if (length == NULL) return NULL;
    *length = sizeof(s_language);
    return s_language;
}
static uint8_t *Manufacturer(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    if (length == NULL) return NULL;
    USBD_GetString((uint8_t *)"Internal test", s_string, length);
    return s_string;
}
static uint8_t *Product(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    if (length == NULL) return NULL;
    USBD_GetString((uint8_t *)"MSC-1 Enumeration", s_string, length);
    return s_string;
}
static USBD_DescriptorsTypeDef s_descriptors = {
    .GetDeviceDescriptor = Device,
    .GetLangIDStrDescriptor = Language,
    .GetManufacturerStrDescriptor = Manufacturer,
    .GetProductStrDescriptor = Product
};
uint8_t *USB_MSC_GetFSConfigDescriptor(uint16_t *length)
{
    if (length == NULL) return NULL;
    *length = 32U;
    return s_config;
}
USBD_DescriptorsTypeDef *USB_MSC_GetDescriptors(void) { return &s_descriptors; }
