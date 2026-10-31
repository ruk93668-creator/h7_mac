#ifndef CM7_USB_UVC_DESC_H
#define CM7_USB_UVC_DESC_H

#include "usbd_def.h"

//配置描述符有效长度，不含PIO尾部补齐空间
#define USB_UVC_FS_CONFIG_DESC_SIZE 180U

//设备描述符有效长度
#define USB_UVC_DEVICE_DESC_SIZE 18U

//语言描述符有效长度
#define USB_UVC_LANGID_DESC_SIZE 4U

//功能名称描述符有效长度，不包含PIO补齐空间
#define USB_UVC_FUNCTION_STR_DESC_SIZE 22U

//返回FS配置静态指针和180字节长度，length为空时返回NULL
uint8_t *USB_UVC_GetFSConfigDescriptor(uint16_t *length);

//返回IAD设备描述符，仅支持FS；无效速度时长度为0
uint8_t *USB_UVC_GetDeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);

//返回English (United States)语言描述符
uint8_t *USB_UVC_GetLangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);

//返回UVC Camera功能名称字符串描述符
uint8_t *USB_UVC_GetInterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);

#endif
