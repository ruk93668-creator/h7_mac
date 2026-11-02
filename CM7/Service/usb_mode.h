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

//冷启动显式选择模式，初始化后拒绝更改；NONE不连接USB
SysResult_t USB_Mode_SelectBootMode(UsbMode_t mode, SysResult_t (*register_class)(void));

//启动主循环中调用：Core的DEVICE阶段后执行，不启动USB
//已初始化返回SYS_OK，不覆盖已有Class；SERVICE Initcall使用此入口
SysResult_t USB_Mode_Init(void);

//启动冷启动选定的Class，不重复启动
SysResult_t USB_Mode_Start(void);

//主循环请求同模式重连，只登记请求；跨模式返回NOT_SUPPORTED
//Process确认Stop成功后才等待500ms并重新启动
SysResult_t USB_Mode_RequestReconnect(UsbMode_t target_mode);

//主循环持续调用：非阻塞处理枚举和当前模式重连，不代替UVC_Process
void USB_Mode_Process(void);

//查询冷启动模式，初始化前返回编译期选择
UsbMode_t USB_Mode_GetMode(void);

#endif
