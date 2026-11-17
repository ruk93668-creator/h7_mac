#ifndef CM7_USB_MSC_DESC_H
#define CM7_USB_MSC_DESC_H
#include "usbd_def.h"
#define USB_MSC_IN_EP 0x82U
#define USB_MSC_OUT_EP 0x02U
#define USB_MSC_PACKET_SIZE 64U
uint8_t *USB_MSC_GetFSConfigDescriptor(uint16_t *length);
USBD_DescriptorsTypeDef *USB_MSC_GetDescriptors(void);
#endif
