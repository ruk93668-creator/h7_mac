#ifndef CM7_USB_MSC_CLASS_H
#define CM7_USB_MSC_CLASS_H

#include "sys_result.h"

//登记独立MSC枚举Class，本阶段不处理Bulk数据或访问存储
SysResult_t USB_MSC_Register(void);

//仅由UsbTask调用，不在USB ISR执行块设备操作
void USB_MSC_Process(uint32_t now_ms);

#endif
