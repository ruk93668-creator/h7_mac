#include "usb_mode.h"
#include "usb_device.h"
#include "framework_init.h"
#include <stdbool.h>

//USB模式运行上下文，仅由主循环和初始化流程管理
typedef struct
{

    //当前模式，暂时只支持TEST
    UsbMode_t mode, target_mode;

    //管理状态，RUNNING表示已启动，不代表主机枚举完成
    UsbModeState_t state;

    //模式管理器已初始化
    bool initialized;

    //最近一次启动结果，供调试观察
    SysResult_t last_result;
} UsbModeContext_t;

//静态模式管理对象，复位后状态为UNINITIALIZED
static UsbModeContext_t s_usb_mode;

//在APPLICATION等级启动当前USB模式
static SysResult_t UsbMode_AppStart(void);

//SERVICE等级准备管理状态，不重复初始化Core
SysResult_t USB_Mode_Init(void)
{

    //重复初始化不能清除正在运行的状态
    if (s_usb_mode.initialized)
        return SYS_OK;

    //DEVICE等级已经注册现有TEST Class
    s_usb_mode.mode = USB_MODE_TEST;
    s_usb_mode.state = USB_MODE_STATE_IDLE;
    s_usb_mode.last_result = SYS_OK;
    s_usb_mode.initialized = true;

    return SYS_OK;
}

//启动现有TEST设备，保留原有描述符和端点配置
SysResult_t USB_Mode_Start(void)
{
    SysResult_t result;

    //必须先完成模式管理初始化
    if (!s_usb_mode.initialized)
        return SYS_ERR_INVALID_STATE;

    //UVC和MSC仅预留枚举，尚未接入Class
    if (s_usb_mode.mode != USB_MODE_TEST)
        return SYS_ERR_NOT_SUPPORTED;

    //已经启动时拒绝第二次启动
    if (s_usb_mode.state == USB_MODE_STATE_RUNNING)
        return SYS_ERR_BUSY;

    //切换和故障恢复流程尚未实现，只允许空闲时启动
    if (s_usb_mode.state != USB_MODE_STATE_IDLE)
        return SYS_ERR_INVALID_STATE;

    //记录启动过程，调用既有Core启动接口
    s_usb_mode.state = USB_MODE_STATE_STARTING;
    result = USB_Device_Start();
    s_usb_mode.last_result = result;

    //启动失败保留错误，不自动重试或恢复
    if (result != SYS_OK)
    {
        s_usb_mode.state = USB_MODE_STATE_ERROR;
        return result;
    }

    //这里只确认软件启动成功，枚举状态仍由Core维护
    s_usb_mode.state = USB_MODE_STATE_RUNNING;
    return SYS_OK;
}

//预留重连请求，不接受尚未实现的切换操作
SysResult_t USB_Mode_RequestReconnect(UsbMode_t target_mode)
{
    //未初始化时不能请求重连
    if (!s_usb_mode.initialized)
        return SYS_ERR_INVALID_STATE;
    //UVC和MSC尚未接入Class，不能作为重连目标
    if (target_mode != USB_MODE_TEST)
        return SYS_ERR_NOT_SUPPORTED;

    //启动流程未完成时不能打断
    if (s_usb_mode.state == USB_MODE_STATE_STARTING)
        return SYS_ERR_BUSY;

    //重连流程已经进行中，拒绝第二次请求
    if (s_usb_mode.state == USB_MODE_STATE_STOPPING ||
        s_usb_mode.state == USB_MODE_STATE_WAIT_RECONNECT ||
        s_usb_mode.state == USB_MODE_STATE_REGISTERING)
        return SYS_ERR_BUSY;

    //空闲状态没有已建立的连接，重连没有意义
    if (s_usb_mode.state == USB_MODE_STATE_IDLE)
        return SYS_ERR_INVALID_STATE;

    //RUNNING、WAIT_CONFIGURED和ERROR允许受理：
    //ERROR受理是停止或启动失败后的唯一恢复入口
    s_usb_mode.target_mode = target_mode;
    s_usb_mode.state = USB_MODE_STATE_STOPPING;
	
    return SYS_OK;
}

//非阻塞主循环入口，暂不执行任何模式切换
void USB_Mode_Process(void)
{

    //初始化未完成时不处理
    if (!s_usb_mode.initialized)
        return;

    //TODO：由用户建立重连请求和目标模式的处理逻辑

    //TODO：由用户实现停止、断开等待、Class注册和重新启动状态机

    //TODO：由用户处理停止失败、注册失败和启动失败的恢复策略
}

//Core在DEVICE阶段就绪，管理器在SERVICE阶段初始化
FRAMEWORK_INIT_REGISTER(USB_Mode_Init, INIT_LEVEL_SERVICE);

//保留唯一的APPLICATION等级USB启动入口
FRAMEWORK_INIT_REGISTER(UsbMode_AppStart, INIT_LEVEL_APPLICATION);

//内部功能函数实现

//APPLICATION等级只转交模式管理器，不直接启动Core
static SysResult_t UsbMode_AppStart(void)
{
    return USB_Mode_Start();
}
