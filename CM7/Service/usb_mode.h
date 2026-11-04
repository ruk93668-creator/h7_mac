#ifndef CM7_USB_MODE_H
#define CM7_USB_MODE_H

#include "sys_result.h"

//置1只选择UVC USB Class，默认保留TEST；不改变Application
#ifndef USB_MODE_BOOT_UVC
#define USB_MODE_BOOT_UVC 0
#endif

#if USB_MODE_BOOT_UVC != 0 && USB_MODE_BOOT_UVC != 1
#error USB_MODE_BOOT_UVC_must_be_0_or_1
#endif

//USB冷启动模式，MSC暂未接入
typedef enum
{
    USB_MODE_TEST = 0,
    USB_MODE_UVC,
    USB_MODE_MSC,
    USB_MODE_NONE
} UsbMode_t;

//模式管理状态，RUNNING仅表示主机已配置，不代表视频正在发送
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

//保存冷启动USB模式选择
SysResult_t USB_Mode_SelectBootMode(UsbMode_t mode, SysResult_t (*register_class)(void));

//准备模式管理状态和Class注册
SysResult_t USB_Mode_Init(void);

//启动当前冷启动USB模式
SysResult_t USB_Mode_Start(void);

//停止当前模式并保留冷启动Class选择
SysResult_t USB_Mode_Stop(void);

//登记当前模式重连请求
SysResult_t USB_Mode_RequestReconnect(UsbMode_t target_mode);

//非阻塞推进枚举和同模式重连
void USB_Mode_Process(void);

//查询当前冷启动模式
UsbMode_t USB_Mode_GetMode(void);

//查询模式管理最近结果
SysResult_t USB_Mode_GetLastResult(void);

#endif
