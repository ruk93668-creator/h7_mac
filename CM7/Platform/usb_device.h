#ifndef CM7_USB_DEVICE_H
#define CM7_USB_DEVICE_H
#include "sys_result.h"
#include "usbd_def.h"
#include <stdbool.h>
//初始化官方USB Device Core及基础枚举Class，不连接主机
SysResult_t USB_Device_Init(void);

//在初始化后、启动前注册后续Class及静态描述符表
SysResult_t USB_Device_RegisterClass(USBD_ClassTypeDef *driver, USBD_DescriptorsTypeDef *descriptors);

//启动枚举并连接主机
SysResult_t USB_Device_Start(void);

//停止设备，可再次启动或更换Class
SysResult_t USB_Device_Stop(void);

//查询主机是否已经完成配置，软件状态不代表实板验收
bool USB_Device_IsConfigured(void);

//读取最近一次底层适配错误
SysResult_t USB_Device_GetLastResult(void);
#endif
