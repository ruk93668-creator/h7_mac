#ifndef CM7_USB_MODE_H
#define CM7_USB_MODE_H

#include "sys_result.h"

//置1选择UVC静态彩条，默认保留TEST；需完整重编CM7
#ifndef USB_MODE_UVC_TEST_ENABLED
#define USB_MODE_UVC_TEST_ENABLED 0
#endif

#if USB_MODE_UVC_TEST_ENABLED != 0 && USB_MODE_UVC_TEST_ENABLED != 1
#error USB_MODE_UVC_TEST_ENABLED_must_be_0_or_1
#endif

//USB冷启动模式，MSC暂未接入
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

//启动冷启动选定的Class，不重复启动
SysResult_t USB_Mode_Start(void);

//请求断开并以目标模式重新枚举，只记录请求不执行硬件操作
SysResult_t USB_Mode_RequestReconnect(UsbMode_t target_mode);

//非阻塞处理枚举和当前模式的重连
void USB_Mode_Process(void);

//查询冷启动模式，初始化前返回编译期选择
UsbMode_t USB_Mode_GetMode(void);

#endif
