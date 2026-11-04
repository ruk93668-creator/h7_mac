#include "usb_mode.h"
#include "usb_device.h"
#include "framework_init.h"
#include <stdbool.h>
#include "platform_time.h"

//登记基础TEST Class
SysResult_t Usb_TestRegister(void);

#define USB_MODE_CONFIGURED_TIMEOUT_MS    (5000U)

//USB模式运行上下文，由初始化流程准备、UsbTask串行推进
typedef struct
{

    //冷启动模式和同模式重连目标
    UsbMode_t mode, target_mode;

    //RUNNING表示主机已完成配置
    UsbModeState_t state;

    //模式管理器已初始化
    bool initialized;

	// 进入当前状态的时间，用于非阻塞超时判断
	uint32_t state_enter_ms;
	
    //最近一次启动结果，供调试观察
    SysResult_t last_result;
} UsbModeContext_t;

//静态模式管理对象，复位后状态为UNINITIALIZED
static UsbModeContext_t s_usb_mode;

//保存上层选择的冷启动模式
static UsbMode_t s_boot_mode;

//记录是否已经显式选择冷启动模式
static bool s_boot_mode_selected;

//保存上层传入的Class注册入口，不直接依赖具体UVC实现
static SysResult_t (*s_boot_register)(void);

//准备APPLICATION等级的USB模式状态
static SysResult_t UsbMode_AppStart(void);

//按冷启动模式登记Class
static SysResult_t UsbMode_RegisterClass(UsbMode_t mode);

//保存冷启动USB模式选择
SysResult_t USB_Mode_SelectBootMode(UsbMode_t mode, SysResult_t (*register_class)(void))
{
    uint16_t length = 0U;

    //模式只能在准备和Class注册之前选择一次
    if (s_usb_mode.initialized || s_boot_mode_selected ||
        USB_Device_GetConfigDescriptor(&length) != NULL)
        return SYS_ERR_INVALID_STATE;

    //未实现的输出模式明确拒绝，不把预留枚举当成功能支持
    if (mode != USB_MODE_TEST && mode != USB_MODE_UVC && mode != USB_MODE_NONE)
        return SYS_ERR_NOT_SUPPORTED;
    if (mode == USB_MODE_UVC && register_class == NULL)
        return SYS_ERR_INVALID_ARG;

    //只保存模式及注册入口，不在这里连接USB
    s_boot_register = register_class;
    s_boot_mode = mode;
    s_boot_mode_selected = true;
    return SYS_OK;
}

//准备模式管理状态和Class注册
SysResult_t USB_Mode_Init(void)
{

    //重复初始化不能清除正在运行的状态
    if (s_usb_mode.initialized)
        return SYS_OK;

    //保留上层已注册的Class，否则按编译期开关注册
    s_usb_mode.mode = USB_Mode_GetMode();
    s_usb_mode.target_mode = s_usb_mode.mode;
    if (s_usb_mode.mode == USB_MODE_NONE)
    {
        s_usb_mode.state = USB_MODE_STATE_IDLE;
        s_usb_mode.last_result = SYS_OK;
        s_usb_mode.initialized = true;
        return SYS_OK;
    }
    {
        uint16_t descriptor_length = 0U;
        if (USB_Device_GetConfigDescriptor(&descriptor_length) == NULL)
        {
            SysResult_t result = UsbMode_RegisterClass(s_usb_mode.mode);
            if (result != SYS_OK)
            {
                s_usb_mode.last_result = result;
                s_usb_mode.state = USB_MODE_STATE_ERROR;
                return result;
            }
        }
    }
    s_usb_mode.state = USB_MODE_STATE_IDLE;
    s_usb_mode.last_result = SYS_OK;
    s_usb_mode.initialized = true;
    return SYS_OK;
}

//启动当前冷启动USB模式
SysResult_t USB_Mode_Start(void)
{
    SysResult_t result;

    //必须先完成模式管理初始化
    if (!s_usb_mode.initialized)
        return SYS_ERR_INVALID_STATE;

    if (s_usb_mode.mode != USB_MODE_TEST && s_usb_mode.mode != USB_MODE_UVC &&
        s_usb_mode.mode != USB_MODE_NONE)
        return SYS_ERR_NOT_SUPPORTED;

    if (s_usb_mode.mode == USB_MODE_UVC && s_boot_register == NULL)
        return SYS_ERR_NOT_SUPPORTED;

    //无输出时保持USB断开
    if (s_usb_mode.mode == USB_MODE_NONE)
        return SYS_OK;

    //已经启动时拒绝第二次启动
    if (s_usb_mode.state == USB_MODE_STATE_RUNNING)
        return SYS_ERR_BUSY;

    //公开Start只接受冷启动IDLE，同模式重连由Process推进
    if (s_usb_mode.state != USB_MODE_STATE_IDLE)
        return SYS_ERR_INVALID_STATE;

    //Start只确认软件启动成功，枚举完成由WAIT_CONFIGURED确认
    s_usb_mode.state = USB_MODE_STATE_STARTING;
    result = USB_Device_Start();
    s_usb_mode.last_result = result;

    //启动失败保留错误，不自动重试或恢复
    if (result != SYS_OK)
    {
        s_usb_mode.state = USB_MODE_STATE_ERROR;
        return result;
    }

	// 记录等待主机完成枚举的起始时间
    //这里只确认软件启动成功
	s_usb_mode.state_enter_ms = Platform_TimeGetMs();
    s_usb_mode.state = USB_MODE_STATE_WAIT_CONFIGURED;
    return SYS_OK;
}

//登记当前模式重连请求
SysResult_t USB_Mode_RequestReconnect(UsbMode_t target_mode)
{

    //未初始化时不能请求重连
    if (!s_usb_mode.initialized)
        return SYS_ERR_INVALID_STATE;

    //只允许当前模式重连，不实现跨模式热切换
    if (target_mode != s_usb_mode.mode)
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

    //RUNNING或ERROR允许进入受控Stop/重新枚举
    if (s_usb_mode.state != USB_MODE_STATE_RUNNING && s_usb_mode.state != USB_MODE_STATE_ERROR)
        return SYS_ERR_BUSY;

    s_usb_mode.target_mode = target_mode;
    s_usb_mode.state = USB_MODE_STATE_STOPPING;
	
    return SYS_OK;
}

//查询当前冷启动模式
UsbMode_t USB_Mode_GetMode(void)
{

    //初始化后返回运行上下文，之前返回显式选择或默认TEST
    if (s_usb_mode.initialized)
        return s_usb_mode.mode;

    return s_boot_mode_selected ? s_boot_mode : (USB_MODE_BOOT_UVC ? USB_MODE_UVC : USB_MODE_TEST);
}

//非阻塞推进枚举和同模式重连
void USB_Mode_Process(void)
{
    uint32_t now_ms;
    SysResult_t usb_result;
	
    //初始化未完成时不处理
    if (!s_usb_mode.initialized)
        return;

    switch ((uint8_t)s_usb_mode.state)
    {
        case USB_MODE_STATE_WAIT_CONFIGURED:
        {

            // 主机已经完成配置，正式进入运行状态
            if (USB_Device_IsConfigured())
            {
                s_usb_mode.state = USB_MODE_STATE_RUNNING;
                s_usb_mode.last_result = SYS_OK;
                break;
            }

            // 底层发生错误时，不必继续等待超时
            usb_result = USB_Device_GetLastResult();

            if (usb_result != SYS_OK)
            {
                s_usb_mode.last_result = usb_result;
                s_usb_mode.state = USB_MODE_STATE_ERROR;
                break;
            }

            // 非阻塞检查枚举超时
            now_ms = Platform_TimeGetMs();

            if ((uint32_t)(now_ms - s_usb_mode.state_enter_ms)
                >= USB_MODE_CONFIGURED_TIMEOUT_MS)
            {
                s_usb_mode.last_result = SYS_ERR_TIMEOUT;
                s_usb_mode.state = USB_MODE_STATE_ERROR;
            }

            break;
        }
        case USB_MODE_STATE_RUNNING:
        {

            //运行期间只响应设备级致命错误，端点BUSY和ISO丢包不会触发恢复
            usb_result = USB_Device_GetLastResult();
            if (usb_result != SYS_OK)
            {
                s_usb_mode.last_result = usb_result;
                s_usb_mode.state = USB_MODE_STATE_ERROR;
            }
            break;
        }
        case USB_MODE_STATE_STOPPING:
		{

			//停止USB驱动
			usb_result = USB_Device_Stop();
			
			if ( usb_result != SYS_OK)
			{
				s_usb_mode.last_result = usb_result;
				s_usb_mode.state = USB_MODE_STATE_ERROR;
				return;
			}
			
			//记录停止时间
			s_usb_mode.state_enter_ms = Platform_TimeGetMs();
			
			s_usb_mode.state = USB_MODE_STATE_WAIT_RECONNECT;
			break;
		}
		case USB_MODE_STATE_WAIT_RECONNECT:
		{

			//断开等待500MS
			if ( Platform_TimeGetMs() - 
				 s_usb_mode.state_enter_ms >= 500)
				s_usb_mode.state = USB_MODE_STATE_STARTING;
			break;
		}
		case USB_MODE_STATE_STARTING:
		{

			//开始配置USB
			usb_result = USB_Device_Start();
			
			if ( usb_result != SYS_OK)
			{
				s_usb_mode.last_result = usb_result;
				s_usb_mode.state = USB_MODE_STATE_ERROR;
				return;
			}
			
			//更新时间
			s_usb_mode.state_enter_ms = Platform_TimeGetMs();
			s_usb_mode.state = USB_MODE_STATE_WAIT_CONFIGURED;
			break;
		}
    }
}

//Core在DEVICE阶段就绪，管理器在SERVICE阶段初始化
FRAMEWORK_INIT_REGISTER(USB_Mode_Init, INIT_LEVEL_SERVICE);

//此历史命名表项仅准备，禁止在调度器前连接USB
FRAMEWORK_INIT_REGISTER(UsbMode_AppStart, INIT_LEVEL_APPLICATION);

//内部功能函数实现

//按冷启动模式登记Class
static SysResult_t UsbMode_RegisterClass(UsbMode_t mode)
{

    //TEST使用既有注册入口，UVC只调用上层传入的注册函数
    switch (mode)
    {
        case USB_MODE_TEST:
            return Usb_TestRegister();
        case USB_MODE_UVC:
            return s_boot_register != NULL ? s_boot_register() : SYS_ERR_NOT_SUPPORTED;
        case USB_MODE_MSC:
        default:
            return SYS_ERR_NOT_SUPPORTED;
    }
}

//准备APPLICATION等级的USB模式状态
static SysResult_t UsbMode_AppStart(void)
{

    //保留历史Initcall表项，但这里只做幂等准备，连接由UsbTask启动
    return USB_Mode_Init();
}

//查询模式管理最近结果
SysResult_t USB_Mode_GetLastResult(void)
{

    //未初始化时返回状态错误；结果不构成任何图像缓冲区释放授权
    return s_usb_mode.initialized ? s_usb_mode.last_result : SYS_ERR_INVALID_STATE;
}

//停止当前模式并保留冷启动Class选择
SysResult_t USB_Mode_Stop(void)
{
    SysResult_t result;

    if (!s_usb_mode.initialized)
        return SYS_ERR_INVALID_STATE;

    //Core确认硬件和Class均已释放后，才允许回到可启动状态
    result = USB_Device_Stop();
    s_usb_mode.last_result = result;
    s_usb_mode.state = result == SYS_OK ? USB_MODE_STATE_IDLE : USB_MODE_STATE_ERROR;
    return result;
}
