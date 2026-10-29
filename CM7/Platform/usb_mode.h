#ifndef CM7_USB_MODE_H
#define CM7_USB_MODE_H

#include "sys_result.h"

//USB功能模式，当前只实现TEST启动
typedef enum
{
    USB_MODE_TEST = 0,
    USB_MODE_UVC,
    USB_MODE_MSC
} UsbMode_t;

//模式管理状态，切换阶段留给后续实现
typedef enum
{
    USB_MODE_STATE_UNINITIALIZED = 0,
    USB_MODE_STATE_IDLE,
    USB_MODE_STATE_STARTING,
	USB_MODE_STATE_WAIT_CONFIGURED,
    USB_MODE_STATE_RUNNING,
    USB_MODE_STATE_STOPPING,
    USB_MODE_STATE_WAIT_RECONNECT,
    USB_MODE_STATE_REGISTERING,
    USB_MODE_STATE_ERROR
} UsbModeState_t;

//初始化模式管理状态，不启动USB
SysResult_t USB_Mode_Init(void);

//启动已有TEST Class，不重复启动
SysResult_t USB_Mode_Start(void);

//请求断开并以目标模式重新枚举，只记录请求不执行硬件操作
SysResult_t USB_Mode_RequestReconnect(UsbMode_t target_mode);

//主循环处理入口，切换状态机由用户实现
void USB_Mode_Process(void);

#endif
