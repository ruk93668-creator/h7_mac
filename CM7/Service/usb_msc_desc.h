#ifndef CM7_USB_MSC_DESC_H
#define CM7_USB_MSC_DESC_H

#include "usbd_def.h"

//MSC独立使用EP2，IN与OUT方向分别分配端点
#define USB_MSC_IN_EP 0x82U
#define USB_MSC_OUT_EP 0x02U

//Full-Speed Bulk端点最大包长为64字节
#define USB_MSC_PACKET_SIZE 64U

//返回静态配置描述符及协议长度，不包含PIO尾部填充
uint8_t *USB_MSC_GetFSConfigDescriptor(uint16_t *length);

//返回静态设备与字符串回调表，由USB Device注册过程使用
USBD_DescriptorsTypeDef *USB_MSC_GetDescriptors(void);

#endif
