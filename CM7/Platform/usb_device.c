//通用USB管理：官方Core负责协议，BSP负责硬件
#include "usb_device.h"
#include "bsp_usb.h"
#include "framework_init.h"
#include "usbd_core.h"
#include "usbd_ctlreq.h"
#include "stm32h7xx_hal.h"
#include <stddef.h>
#include <string.h>

#if defined(APP_USB_ISO_TIMING_TEST_ENABLED) && APP_USB_ISO_TIMING_TEST_ENABLED

    //仅采样构建接入时间记录，不改变通用USB通知接口
#include "usb_iso_timing.h"
#endif


//对应官方Core的16个端点编号，不预设Class用途
#define USB_DEVICE_EP_COUNT 16U

//保存Application绑定的普通C通知函数
static UsbDeviceEventCallback_t s_event_callback;

//保存通知接收端的上下文，Platform不解释其内容
static void *s_event_context;

//通知上层USB状态已经变化
static void Usb_Notify(uint32_t events);

//保存官方Core设备句柄、EP0请求及Class绑定
static USBD_HandleTypeDef s_device;

//记录Core与BSP是否已经准备成功
static volatile bool s_initialized;

//记录设备连接是否已经启动
static volatile bool s_started;

//记录是否已经登记有效Class和描述符
static volatile bool s_registered;

//锁存尚未恢复的设备故障，恢复前禁止重新启动
static volatile bool s_stop_required;

//生命周期回调执行期间禁止重入Init、Register、Start和Stop
static volatile bool s_control_busy;

//保存设备级故障结果，不与普通操作BUSY混用
static volatile SysResult_t s_last_result;

//保存最近一次LL操作的工程返回码
static volatile SysResult_t s_operation_result;

//复制外部操作表，配置数据本身由调用者保持静态有效
typedef enum
{
    USB_CLASS_INACTIVE = 0,
    USB_CLASS_INITIALIZING,
    USB_CLASS_ACTIVE,
    USB_CLASS_DEINITIALIZING,
    USB_CLASS_RELEASE_FAILED,
    USB_CLASS_RELEASING
} UsbClassState_t;

//记录Class初始化与释放所处阶段
static volatile UsbClassState_t s_class_state;

//保存外部原始Class操作表
static USBD_ClassTypeDef s_class_callbacks;

//保存交给官方Core的包装操作表
static USBD_ClassTypeDef s_class_driver;

//官方DeInit会清空pUserData，失败时保留Class当前的外部绑定
static void *s_class_user_data;

//记录硬件停止失败，禁止提前归还Class资源
static volatile bool s_hardware_stop_failed;

//记录DeInit过程中的释放意向，成功后再提交回收
static volatile bool s_class_free_pending;

//保存外部描述符查询回调表
static USBD_DescriptorsTypeDef s_descriptor_driver;

//保存当前配置描述符的静态地址
static uint8_t *s_configuration;

//保存配置描述符的有效协议长度
static uint16_t s_configuration_length;

//注册描述符声明的精确端点集合，包含不同Alternate Setting
static BspUsbEndpointConfig_t s_endpoint_requirements[BSP_USB_MAX_ENDPOINT_CONFIGS];

//记录已提取的端点声明数量
static uint32_t s_endpoint_requirement_count;

//保留固定Class内存块，不使用malloc
static uint8_t s_class_memory[1024] __attribute__((aligned(32)));

//记录固定Class内存块是否仍被占用
static volatile bool s_class_memory_used;

//两个方向独立管理资源，不规定具体端点类型或地址
static volatile UsbDeviceEndpointState_t s_endpoints[2][USB_DEVICE_EP_COUNT];

//准备通用Core和BSP
static SysResult_t Usb_InitDevice(void);

//校验并登记外部Class和描述符
static SysResult_t Usb_RegisterClass(USBD_ClassTypeDef *driver, USBD_DescriptorsTypeDef *descriptors);

//启动USB设备连接
static SysResult_t Usb_StartDevice(void);

//停止硬件并释放USB资源
static SysResult_t Usb_StopDevice(void);

//执行受保护的Class初始化
static uint8_t Usb_ManagedClassInit(USBD_HandleTypeDef *device, uint8_t config);

//执行受保护的Class反初始化
static uint8_t Usb_ManagedClassDeInit(USBD_HandleTypeDef *device, uint8_t config);

//提取描述符端点需求并规划FIFO
static SysResult_t Usb_PlanEndpoints(uint8_t *config, uint16_t length);

//校验配置描述符结构及字符串回调
static SysResult_t Usb_ValidateConfig(uint8_t *config, uint16_t length,
                                             USBD_DescriptorsTypeDef *descriptors);

//按方向和编号查找端点状态
static volatile UsbDeviceEndpointState_t *Usb_Endpoint(uint8_t ep);

//判断是否为有效的Class数据端点
static bool Usb_ClassEndpoint(uint8_t ep);

//保存并屏蔽USB中断
static uint32_t Usb_Lock(void);

//恢复进入前的USB中断状态
static void Usb_Unlock(uint32_t enabled);

//关闭Class遗留的数据端点
static SysResult_t Usb_ReleaseEndpoints(void);

//取得LL路径保存的工程返回码
static SysResult_t Usb_PublicResult(USBD_StatusTypeDef status);

//转换并记录BSP操作结果
static USBD_StatusTypeDef Usb_LlResult(SysResult_t result);

//取消旧控制事务并分发新SETUP
static void Usb_Setup(uint8_t *data);

//交付OUT完成并通知上层
static void Usb_DataOut(uint8_t ep, uint8_t *data);

//交付IN完成并通知上层
static void Usb_DataIn(uint8_t ep, uint8_t *data);

//记录IN传输失败结果
static void Usb_DataInError(uint8_t ep, SysResult_t result);

//处理总线复位并恢复EP0
static void Usb_Reset(void);

//转交总线挂起事件
static void Usb_Suspend(void);

//转交总线恢复事件
static void Usb_Resume(void);

//转交设备连接事件
static void Usb_Connect(void);

//处理断线并回收Class资源
static void Usb_Disconnect(void);

//转交IN等时传输未完成事件
static void Usb_IsoInIncomplete(uint8_t ep);

//转交OUT等时传输未完成事件
static void Usb_IsoOutIncomplete(uint8_t ep);

//保存并屏蔽USB中断
uint32_t USB_Device_Lock(void)
{

    //仅屏蔽USB IRQ并返回原状态，调用者必须成对恢复
    return Usb_Lock();
}

//恢复USB中断使能状态
void USB_Device_Unlock(uint32_t saved)
{

    //按进入前的状态恢复，不无条件打开USB IRQ
    Usb_Unlock(saved);
}

//记录各IN数据端点的提交序号，用于匹配异步完成
static volatile uint32_t s_in_sequence[USB_DEVICE_EP_COUNT];

//记录各IN数据端点最近确认的真实完成序号
static volatile uint32_t s_in_completed[USB_DEVICE_EP_COUNT];

//查询指定IN端点的提交序号
uint32_t USB_Device_GetInSequence(uint8_t ep)
{

    //只接受有效IN端点地址，返回提交序号而非完成次数
    return (ep & 0x80U) && (ep & 0x7FU) < USB_DEVICE_EP_COUNT ? s_in_sequence[ep & 0x7FU] : 0U;
}

//查询指定IN端点的完成序号
uint32_t USB_Device_GetInCompleteSequence(uint8_t ep)
{

    //返回最后一次真实完成对应的提交序号，用于排除旧回调
    return (ep & 0x80U) && (ep & 0x7FU) < USB_DEVICE_EP_COUNT ? s_in_completed[ep & 0x7FU] : 0U;
}

//记录去重后的SOF计数，不保存无限发送额度
static volatile uint32_t s_sof_sequence;

//保存最近一次硬件SOF帧号，用于过滤重复通知
static uint16_t s_sof_frame;

//记录是否已有可用于SOF去重的历史帧号
static bool s_sof_valid;

//去重SOF并转交官方Core
static void Usb_Sof(uint16_t frame);

//查询去重后的SOF序号
uint32_t USB_Device_GetSofSequence(void)
{

    //读取SOF进度，调用者自行限制每个SOF的提交机会
    return s_sof_sequence;
}

//将BSP事件绑定到通用Core适配函数
static const BspUsbCallbacks_t s_callbacks =
{
    Usb_Setup,
    Usb_DataOut,
    Usb_DataIn,
    Usb_Reset,
    Usb_Suspend,
    Usb_Resume,
    Usb_Connect,
    Usb_Disconnect,
    Usb_IsoInIncomplete,
    Usb_IsoOutIncomplete,
    Usb_DataInError,
    Usb_Sof
};

//绑定USB状态通知接收端
SysResult_t USB_Device_SetEventCallback(UsbDeviceEventCallback_t callback, void *context)
{

    //拒绝空通知函数，避免后续事件调用无效地址
    if (callback == NULL)
        return SYS_ERR_INVALID_ARG;

    //只允许设备尚未启动时从非中断上下文绑定
    if (__get_IPSR() != 0U || s_started)
        return SYS_ERR_INVALID_STATE;

    //只绑定一次，不在运行期间替换接收端
    if (s_event_callback != NULL)
        return SYS_ERR_ALREADY_EXISTS;

    //先保存上下文再提交回调，接收端必须持续有效
    s_event_context = context;
    s_event_callback = callback;
    return SYS_OK;
}

//初始化通用USB Device环境
SysResult_t USB_Device_Init(void)
{
    SysResult_t result;

    //生命周期操作不能重入，回调内再次调用时直接返回BUSY
    if (s_control_busy)
        return SYS_ERR_BUSY;

    //只允许普通执行上下文调用，不能在ISR中执行生命周期操作
    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;

    //保护内部操作执行过程，返回前恢复入口可用状态
    s_control_busy = true;
    result = Usb_InitDevice();
    s_control_busy = false;
    return result;
}

//登记外部Class和描述符
SysResult_t USB_Device_RegisterClass(USBD_ClassTypeDef *driver, USBD_DescriptorsTypeDef *descriptors)
{
    SysResult_t result;

    //生命周期操作不能重入，回调内再次调用时直接返回BUSY
    if (s_control_busy)
        return SYS_ERR_BUSY;

    //只允许普通执行上下文调用，不能在ISR中执行生命周期操作
    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;

    //保护内部操作执行过程，返回前恢复入口可用状态
    s_control_busy = true;
    result = Usb_RegisterClass(driver, descriptors);
    s_control_busy = false;
    return result;
}

//返回当前已登记的配置描述符
uint8_t *USB_Device_GetConfigDescriptor(uint16_t *length)
{

    //检查长度输出指针，再返回静态描述符地址和有效长度
    if (length == NULL)
        return NULL;
    *length = s_configuration_length;
    return s_configuration;
}

//启动已登记的USB设备
SysResult_t USB_Device_Start(void)
{
    SysResult_t result;

    //生命周期操作不能重入，回调内再次调用时直接返回BUSY
    if (s_control_busy)
        return SYS_ERR_BUSY;

    //只允许普通执行上下文调用，不能在ISR中执行生命周期操作
    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;

    //保护内部操作执行过程，返回前恢复入口可用状态
    s_control_busy = true;
    result = Usb_StartDevice();
    s_control_busy = false;
    return result;
}

//停止USB并清理Class资源
SysResult_t USB_Device_Stop(void)
{
    SysResult_t result;

    //生命周期操作不能重入，回调内再次调用时直接返回BUSY
    if (s_control_busy)
        return SYS_ERR_BUSY;

    //只允许普通执行上下文调用，不能在ISR中执行生命周期操作
    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;

    //保护内部操作执行过程，返回前恢复入口可用状态
    s_control_busy = true;
    result = Usb_StopDevice();
    s_control_busy = false;
    return result;
}

//查询USB设备是否已完成配置
bool USB_Device_IsConfigured(void)
{

    //设备已启动、无待恢复故障且主机配置完成时才返回true
    return s_started && !s_stop_required && s_device.dev_state == USBD_STATE_CONFIGURED;
}

//查询设备级运行结果
SysResult_t USB_Device_GetLastResult(void)
{

    //这里只读取设备级结果，不把端点临时BUSY当作设备故障
    return s_last_result;
}

//查询最近一次接口操作结果
SysResult_t USB_Device_GetLastOperationResult(void)
{

    //读取最近一次LL调用结果，不改变设备级故障状态
    return s_operation_result;
}

//打开Class数据端点
SysResult_t USB_Device_OpenEp(uint8_t ep, uint8_t type, uint16_t size)
{

    //公开Class接口拒绝EP0，控制端点生命周期由Core独立管理
    if (!Usb_ClassEndpoint(ep))
        return SYS_ERR_INVALID_ARG;

    //交给通用LL路径校验状态并调用BSP，返回原工程错误语义
    return Usb_PublicResult(USBD_LL_OpenEP(&s_device, ep, type, size));
}

//取消Class数据端点的当前传输
SysResult_t USB_Device_AbortEp(uint8_t ep)
{
    volatile UsbDeviceEndpointState_t *entry = Usb_Endpoint(ep);
    SysResult_t result;
    uint32_t enabled;
    if (!Usb_ClassEndpoint(ep) || entry == NULL)
        return SYS_ERR_INVALID_ARG;

    //屏蔽USB IRQ，避免取消过程与传输完成同时修改占用状态
    enabled = Usb_Lock();
    if (!s_initialized || !entry->opened)
        result = SYS_ERR_INVALID_STATE;
    else
    {

        //先确认硬件已停止访问缓冲区，成功后才清除busy
        result = BSP_USB_AbortEp(ep);
        if (result == SYS_OK)
        {
            entry->busy = false;
            entry->last_result = SYS_OK;
        }
        else entry->last_result = result;
    }

    //保留本次取消结果，失败时不伪造资源已归还
    s_operation_result = result;
    if (result == SYS_ERR_IO || result == SYS_ERR_TIMEOUT) s_last_result = result;
    Usb_Unlock(enabled);
    return result;
}

//关闭Class数据端点
SysResult_t USB_Device_CloseEp(uint8_t ep)
{

    //公开Class接口拒绝EP0，控制端点生命周期由Core独立管理
    if (!Usb_ClassEndpoint(ep))
        return SYS_ERR_INVALID_ARG;

    //交给通用LL路径校验状态并调用BSP，返回原工程错误语义
    return Usb_PublicResult(USBD_LL_CloseEP(&s_device, ep));
}

//清空空闲Class数据端点的FIFO
SysResult_t USB_Device_FlushEp(uint8_t ep)
{

    //公开Class接口拒绝EP0，控制端点生命周期由Core独立管理
    if (!Usb_ClassEndpoint(ep))
        return SYS_ERR_INVALID_ARG;

    //交给通用LL路径校验状态并调用BSP，返回原工程错误语义
    return Usb_PublicResult(USBD_LL_FlushEP(&s_device, ep));
}

//向Class IN端点提交发送
SysResult_t USB_Device_Transmit(uint8_t ep, uint8_t *data, uint32_t size)
{

    //公开Class接口拒绝EP0，控制端点生命周期由Core独立管理
    if (!Usb_ClassEndpoint(ep))
        return SYS_ERR_INVALID_ARG;

    //交给通用LL路径校验状态并调用BSP，返回原工程错误语义
    return Usb_PublicResult(USBD_LL_Transmit(&s_device, ep, data, size));
}

//向Class OUT端点提交接收
SysResult_t USB_Device_Receive(uint8_t ep, uint8_t *data, uint32_t size)
{

    //公开Class接口拒绝EP0，控制端点生命周期由Core独立管理
    if (!Usb_ClassEndpoint(ep))
        return SYS_ERR_INVALID_ARG;

    //交给通用LL路径校验状态并调用BSP，返回原工程错误语义
    return Usb_PublicResult(USBD_LL_PrepareReceive(&s_device, ep, data, size));
}

//设置Class数据端点STALL
SysResult_t USB_Device_StallEp(uint8_t ep)
{

    //公开Class接口拒绝EP0，控制端点生命周期由Core独立管理
    if (!Usb_ClassEndpoint(ep))
        return SYS_ERR_INVALID_ARG;

    //交给通用LL路径校验状态并调用BSP，返回原工程错误语义
    return Usb_PublicResult(USBD_LL_StallEP(&s_device, ep));
}

//清除Class数据端点STALL
SysResult_t USB_Device_ClearStallEp(uint8_t ep)
{

    //公开Class接口拒绝EP0，控制端点生命周期由Core独立管理
    if (!Usb_ClassEndpoint(ep))
        return SYS_ERR_INVALID_ARG;

    //交给通用LL路径校验状态并调用BSP，返回原工程错误语义
    return Usb_PublicResult(USBD_LL_ClearStallEP(&s_device, ep));
}

//取得端点状态快照
SysResult_t USB_Device_GetEpState(uint8_t ep, UsbDeviceEndpointState_t *state)
{
    volatile UsbDeviceEndpointState_t *entry = Usb_Endpoint(ep);
    uint32_t enabled;
    if (entry == NULL || state == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!s_initialized)
        return SYS_ERR_INVALID_STATE;

    //屏蔽USB IRQ后复制快照，避免读到回调更新一半的状态
    enabled = Usb_Lock();
    *state = *entry;
    Usb_Unlock(enabled);
    return SYS_OK;
}

//查询Class OUT端点接收长度
uint32_t USB_Device_GetRxSize(uint8_t ep)
{

    //公开接口只查询Class OUT端点，EP0仍由Core查询
    if (!Usb_ClassEndpoint(ep) || (ep & 0x80U) != 0U)
        return 0U;
    return USBD_LL_GetRxDataSize(&s_device, ep);
}

//为官方Core准备BSP和事件回调
USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *device)
{
    SysResult_t result;

    //只接受本模块持有的官方Core句柄
    if (device != &s_device)
        return USBD_FAIL;

    //准备硬件并接入静态事件表，此时不连接主机
    result = BSP_USB_Init();
    if (result == SYS_OK) result = BSP_USB_SetCallbacks(&s_callbacks);
    return Usb_LlResult(result);
}

//为官方Core释放USB硬件环境
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *device)
{
    SysResult_t result = SYS_ERR_IO;

    //官方Core传入的设备必须属于当前通用管理实例
    if (device != &s_device)
        return USBD_FAIL;

    //官方DeInit忽略外部释放错误，不能因此强制丢弃旧资源
    if (s_hardware_stop_failed || s_class_state != USB_CLASS_INACTIVE || s_class_memory_used)
        goto deinit_failed;
    result = BSP_USB_DeInit();
    if (result != SYS_OK)
        goto deinit_failed;
    s_initialized = false;
    s_started = false;
    s_registered = false;
    s_stop_required = false;
    s_configuration = NULL;
    s_configuration_length = 0U;
    s_class_free_pending = false;
    s_class_user_data = NULL;
    memset((void *)s_endpoints, 0, sizeof(s_endpoints));
    return Usb_LlResult(SYS_OK);

deinit_failed:

    //PCD反初始化也可能失败，所有失败出口都恢复仍需使用的注册绑定
    s_device.pClass[0] = s_registered ? &s_class_driver : NULL;
    s_device.pDesc = s_registered ? &s_descriptor_driver : NULL;
    s_device.pConfDesc = s_configuration;
    s_device.pUserData[0] = s_class_user_data;
    s_stop_required = true;
    return Usb_LlResult(result);
}

//转交官方Core的硬件启动请求
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *device)
{

    //官方Core传入的设备必须属于当前通用管理实例
    if (device != &s_device)
        return USBD_FAIL;
    return Usb_LlResult(BSP_USB_Start());
}

//转交官方Core的硬件停止请求
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *device)
{
    SysResult_t result;

    //官方Core传入的设备必须属于当前通用管理实例
    if (device != &s_device)
        return USBD_FAIL;
    s_class_user_data = s_device.pUserData[0];
    result = BSP_USB_Stop();
    s_hardware_stop_failed = result != SYS_OK;
    if (result == SYS_OK)
        s_started = false;
    else
        s_stop_required = true;
    return Usb_LlResult(result);
}

//校验并打开Core指定端点
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *device, uint8_t ep, uint8_t type, uint16_t size)
{
    volatile UsbDeviceEndpointState_t *entry = Usb_Endpoint(ep);
    SysResult_t result;
    uint32_t enabled;

    //先检查设备句柄和端点地址，禁止访问无效状态项
    if (device != &s_device || entry == NULL || size == 0U || type > USBD_EP_TYPE_INTR)
        return Usb_LlResult(SYS_ERR_INVALID_ARG);

    //在USB IRQ保护下检查并提交端点状态，退出时恢复原使能状态
    enabled = Usb_Lock();
    if (!s_initialized || ((ep & 0x0FU) != 0U && (!s_started || s_stop_required)))
    {
        Usb_Unlock(enabled);
        return Usb_LlResult(SYS_ERR_INVALID_STATE);
    }
    if ((ep & 0x0FU) != 0U && entry->opened)
    {
        Usb_Unlock(enabled);
        return Usb_LlResult(SYS_ERR_ALREADY_EXISTS);
    }

    //EP0由Core管理；只有Class数据端点需要匹配注册声明
    if ((ep & 0x0FU) != 0U)
    {
        uint32_t i;
        bool declared = false;
        for (i = 0U; i < s_endpoint_requirement_count; ++i)
        {
            const BspUsbEndpointConfig_t *requirement = &s_endpoint_requirements[i];
            if (requirement->address == ep && requirement->type == type && requirement->max_packet == size)
            { declared = true; break; }
        }
        if (!declared) { Usb_Unlock(enabled); return Usb_LlResult(SYS_ERR_NOT_SUPPORTED); }
    }

    //通过声明匹配后再申请硬件，成功才提交opened和最大包长
    result = BSP_USB_OpenEp(ep, type, size);
    entry->last_result = result;
    if (result == SYS_OK)
    {
        entry->opened = true;
        entry->busy = false;
        entry->stalled = false;
        entry->type = type;
        entry->max_packet = size;
        if ((ep & 0x80U) != 0U)
        {
            s_device.ep_in[ep & 0x0FU].is_used = 1U;
            s_device.ep_in[ep & 0x0FU].maxpacket = size;
        }
        else
        {
            s_device.ep_out[ep & 0x0FU].is_used = 1U;
            s_device.ep_out[ep & 0x0FU].maxpacket = size;
        }
    }
    Usb_Unlock(enabled);
    return Usb_LlResult(result);
}

//取消传输并关闭Core指定端点
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    volatile UsbDeviceEndpointState_t *entry = Usb_Endpoint(ep);
    SysResult_t result;
    uint32_t enabled;

    //先检查设备句柄和端点地址，禁止访问无效状态项
    if (device != &s_device || entry == NULL)
        return Usb_LlResult(SYS_ERR_INVALID_ARG);
    if (!s_initialized)
        return Usb_LlResult(SYS_ERR_INVALID_STATE);

    //在USB IRQ保护下检查并提交端点状态，退出时恢复原使能状态
    enabled = Usb_Lock();
    if (entry->opened && entry->busy && (ep & 0x0FU) != 0U)
    {

        //Abort成功后硬件不再持有缓冲区，即使后续Close失败也归还传输所有权
        result = BSP_USB_AbortEp(ep);
        if (result == SYS_OK)
            entry->busy = false;
    }
    else result = SYS_OK;

    //前一步取消成功后才继续关闭，失败时保留尚未释放的资源
    if (result == SYS_OK && entry->opened)
        result = BSP_USB_CloseEp(ep);
    entry->last_result = result;

    //确认关闭成功后同步清理Core与本模块的端点占用
    if (result == SYS_OK)
    {
        entry->opened = false;
        entry->busy = false;
        entry->stalled = false;
        if ((ep & 0x80U) != 0U)
            memset(&s_device.ep_in[ep & 0x0FU], 0, sizeof(s_device.ep_in[0]));
        else
            memset(&s_device.ep_out[ep & 0x0FU], 0, sizeof(s_device.ep_out[0]));
    }
    Usb_Unlock(enabled);
    return Usb_LlResult(result);
}

//清空Core指定的空闲端点FIFO
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    volatile UsbDeviceEndpointState_t *entry = Usb_Endpoint(ep);
    SysResult_t result;
    uint32_t enabled;
    uint32_t index;

    //先检查设备句柄和端点地址，禁止访问无效状态项
    if (device != &s_device || entry == NULL)
        return Usb_LlResult(SYS_ERR_INVALID_ARG);

    //在USB IRQ保护下检查并提交端点状态，退出时恢复原使能状态
    enabled = Usb_Lock();
    if (!s_initialized || !entry->opened)
    {
        Usb_Unlock(enabled);
        return Usb_LlResult(SYS_ERR_INVALID_STATE);
    }

    //Flush不等于取消传输，busy期间不能释放缓冲区所有权
    result = entry->busy ? SYS_ERR_BUSY : SYS_OK;

    //所有OUT共享RX FIFO，不能丢弃另一OUT尚未交付的数据
    if ((ep & 0x80U) == 0U)
        for (index = 0U; index < USB_DEVICE_EP_COUNT; ++index)
            if (s_endpoints[0][index].busy)
                result = SYS_ERR_BUSY;
    if (result == SYS_OK)
        result = BSP_USB_FlushEp(ep);
    entry->last_result = result;
    Usb_Unlock(enabled);
    return Usb_LlResult(result);
}

//设置Core指定端点STALL
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    volatile UsbDeviceEndpointState_t *entry = Usb_Endpoint(ep);
    SysResult_t result;
    uint32_t enabled;

    //先检查设备句柄和端点地址，禁止访问无效状态项
    if (device != &s_device || entry == NULL)
        return Usb_LlResult(SYS_ERR_INVALID_ARG);

    //在USB IRQ保护下检查并提交端点状态，退出时恢复原使能状态
    enabled = Usb_Lock();
    if (!s_initialized || !entry->opened)
    {
        Usb_Unlock(enabled);
        return Usb_LlResult(SYS_ERR_INVALID_STATE);
    }
    result = BSP_USB_StallEp(ep);
    entry->last_result = result;
    if (result == SYS_OK)
        entry->stalled = true;
    Usb_Unlock(enabled);
    return Usb_LlResult(result);
}

//清除Core指定端点STALL
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    volatile UsbDeviceEndpointState_t *entry = Usb_Endpoint(ep);
    SysResult_t result;
    uint32_t enabled;

    //先检查设备句柄和端点地址，禁止访问无效状态项
    if (device != &s_device || entry == NULL)
        return Usb_LlResult(SYS_ERR_INVALID_ARG);

    //在USB IRQ保护下检查并提交端点状态，退出时恢复原使能状态
    enabled = Usb_Lock();
    if (!s_initialized || !entry->opened)
    {
        Usb_Unlock(enabled);
        return Usb_LlResult(SYS_ERR_INVALID_STATE);
    }
    result = BSP_USB_ClearStallEp(ep);
    entry->last_result = result;
    if (result == SYS_OK)
        entry->stalled = false;
    Usb_Unlock(enabled);
    return Usb_LlResult(result);
}

//查询底层端点STALL状态
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *device, uint8_t ep)
{

    //核对设备归属后查询BSP实际STALL状态
    return device == &s_device && BSP_USB_IsStallEp(ep) ? 1U : 0U;
}

//向BSP提交设备地址
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *device, uint8_t address)
{

    //官方Core传入的设备必须属于当前通用管理实例
    if (device != &s_device)
        return USBD_FAIL;
    return Usb_LlResult(BSP_USB_SetAddress(address));
}

//向BSP提交IN传输并管理占用状态
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *device, uint8_t ep, uint8_t *buffer, uint32_t size)
{
    volatile UsbDeviceEndpointState_t *entry;
    SysResult_t result;
    uint32_t enabled;

    //官方Core用编号0提交控制IN，统一转成完整IN端点地址
    if (ep == 0U) ep = 0x80U;
    entry = Usb_Endpoint(ep);
    if (device != &s_device || entry == NULL || (ep & 0x80U) == 0U ||
        (buffer == NULL && size != 0U))
        return Usb_LlResult(SYS_ERR_INVALID_ARG);

    //串行检查设备和端点状态，防止IRQ提前完成当前提交
    enabled = Usb_Lock();
    if (!s_initialized || !s_started || s_stop_required || !entry->opened)
    {
        Usb_Unlock(enabled);
        return Usb_LlResult(SYS_ERR_INVALID_STATE);
    }

    //所有IN端点均禁止STALL后继续发送，防止Core补交失败请求的状态包
    if (entry->busy || entry->stalled)
    {

        //官方Core可能先写STATUS_IN，EP0被STALL时恢复真实状态
        if ((ep & 0x0FU) == 0U && entry->stalled)
            device->ep0_state = USBD_EP0_STALL;
        result = entry->busy ? SYS_ERR_BUSY : SYS_ERR_INVALID_STATE;
        Usb_Unlock(enabled);
        return Usb_LlResult(result);
    }
    if ((ep & 0x7FU) != 0U)
    {
        if (s_in_sequence[ep & 0x7FU] == UINT32_MAX)
        { Usb_Unlock(enabled); return Usb_LlResult(SYS_ERR_NO_RESOURCE); }
        s_in_sequence[ep & 0x7FU]++;
    }

    //先标记在途再提交硬件；提交失败才撤销本次占用
    entry->busy = true;
    result = BSP_USB_Transmit(ep, buffer, size);
    entry->last_result = result;
    if (result != SYS_OK)
        entry->busy = false;
    Usb_Unlock(enabled);
    return Usb_LlResult(result);
}

//向BSP提交OUT接收并管理占用状态
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *device, uint8_t ep, uint8_t *buffer, uint32_t size)
{
    volatile UsbDeviceEndpointState_t *entry;
    SysResult_t result;
    uint32_t enabled;

    //按OUT地址查找状态，方向错误或空缓冲配置直接拒绝
    entry = Usb_Endpoint(ep);
    if (device != &s_device || entry == NULL || (ep & 0x80U) != 0U ||
        (buffer == NULL && size != 0U))
        return Usb_LlResult(SYS_ERR_INVALID_ARG);

    //屏蔽USB IRQ，保护接收状态检查与提交
    enabled = Usb_Lock();
    if (!s_initialized || !s_started || s_stop_required || !entry->opened)
    {
        Usb_Unlock(enabled);
        return Usb_LlResult(SYS_ERR_INVALID_STATE);
    }

    //EP0多阶段由官方Core推进，数据端点禁止覆盖未完成事务
    if ((ep & 0x0FU) != 0U && (entry->busy || entry->stalled))
    {
        result = entry->busy ? SYS_ERR_BUSY : SYS_ERR_INVALID_STATE;
        Usb_Unlock(enabled);
        return Usb_LlResult(result);
    }

    //硬件接收期间保留占用，启动失败时才撤销本次busy
    entry->busy = true;
    result = BSP_USB_Receive(ep, buffer, size);
    entry->last_result = result;
    if (result != SYS_OK)
        entry->busy = false;
    Usb_Unlock(enabled);
    return Usb_LlResult(result);
}

//查询Core指定OUT端点接收长度
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *device, uint8_t ep)
{
    volatile UsbDeviceEndpointState_t *entry = Usb_Endpoint(ep);

    //只有本设备已打开的OUT端点才能查询实际接收长度
    if (device != &s_device || entry == NULL || (ep & 0x80U) != 0U || !entry->opened)
        return 0U;
    return BSP_USB_GetRxSize(ep);
}

//提供官方Core需要的HAL延时
void USBD_LL_Delay(uint32_t delay)
{

    //使用独立HAL时基等待，调用者不能在无法推进HAL Tick的上下文阻塞
    HAL_Delay(delay);
}

//从固定内存块借出Class上下文
void *USB_Device_Alloc(uint32_t size)
{
    void *memory = NULL;
    uint32_t enabled = Usb_Lock();

    //只借出唯一固定块，容量不足或Class正在释放时拒绝申请
    if (size != 0U && size <= sizeof(s_class_memory) && !s_class_memory_used &&
        s_class_state != USB_CLASS_DEINITIALIZING && s_class_state != USB_CLASS_RELEASE_FAILED && s_class_state != USB_CLASS_RELEASING)
    {
        s_class_memory_used = true;
        memory = s_class_memory;
    }
    Usb_Unlock(enabled);
    return memory;
}

//归还Class固定内存块
void USB_Device_Free(void *memory)
{
    uint32_t enabled = Usb_Lock();

    //只接受本模块借出的固定块，不能释放任意地址
    if (memory == s_class_memory && s_class_memory_used)
    {

        //DeInit成功前只登记释放意向，失败时保留分配标志
        if (s_class_state == USB_CLASS_DEINITIALIZING)
            s_class_free_pending = true;
        else if (s_class_state != USB_CLASS_RELEASE_FAILED && s_class_state != USB_CLASS_RELEASING)
            s_class_memory_used = false;
    }
    Usb_Unlock(enabled);
}

//DEVICE阶段只准备通用Core，不指定设备用途
FRAMEWORK_INIT_REGISTER(USB_Device_Init, INIT_LEVEL_DEVICE);

//内部功能函数实现

//转换并记录BSP操作结果
static USBD_StatusTypeDef Usb_LlResult(SysResult_t result)
{

    //保留完整工程错误码，转换成官方状态时不丢失原始原因
    s_operation_result = result;
    if (result == SYS_OK) return USBD_OK;

    //只有真正的底层I/O故障升级为设备级故障
    if (result == SYS_ERR_IO || result == SYS_ERR_TIMEOUT) s_last_result = result;
    return result == SYS_ERR_BUSY ? USBD_BUSY : USBD_FAIL;
}

//取得LL路径保存的工程返回码
static SysResult_t Usb_PublicResult(USBD_StatusTypeDef status)
{
    (void)status;

    //LL转换已保存原始工程错误码，不从有限的官方状态反推原因
    return s_operation_result;
}

//按方向和编号查找端点状态
static volatile UsbDeviceEndpointState_t *Usb_Endpoint(uint8_t ep)
{

    //拒绝保留地址位和越界编号，IN与OUT使用独立状态项
    if ((ep & 0x70U) != 0U || (ep & 0x0FU) >= USB_DEVICE_EP_COUNT)
        return NULL;
    return &s_endpoints[(ep & 0x80U) != 0U][ep & 0x0FU];
}

//判断是否为有效的Class数据端点
static bool Usb_ClassEndpoint(uint8_t ep)
{

    //端点地址必须有效且编号非0，禁止Class管理EP0生命周期
    return Usb_Endpoint(ep) != NULL && (ep & 0x0FU) != 0U;
}

//保存并屏蔽USB中断
static uint32_t Usb_Lock(void)
{

    //只保存并屏蔽USB IRQ，其他中断和HAL时间基准继续运行
    uint32_t enabled = NVIC_GetEnableIRQ(OTG_FS_IRQn);
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    return enabled;
}

//恢复进入前的USB中断状态
static void Usb_Unlock(uint32_t enabled)
{

    //仅恢复原先已开启的USB IRQ，不能擅自改变调用者的屏蔽决定
    if (enabled != 0U)
        HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}

//关闭Class遗留的数据端点
static SysResult_t Usb_ReleaseEndpoints(void)
{
    uint8_t direction;
    uint8_t index;
    SysResult_t result = SYS_OK;

    //分别清理IN和OUT数据端点，EP0由Core单独管理
    for (direction = 0U; direction < 2U; ++direction)
    {
        for (index = 1U; index < USB_DEVICE_EP_COUNT; ++index)
        {

            //继续尝试关闭其他端点，但只保留第一个失败结果
            if (s_endpoints[direction][index].opened &&
                USBD_LL_CloseEP(&s_device, index | (direction ? 0x80U : 0U)) != USBD_OK &&
                result == SYS_OK)
                result = s_operation_result;
        }
    }
    return result;
}

//校验配置描述符结构及字符串回调
static SysResult_t Usb_ValidateConfig(uint8_t *config, uint16_t length,
                                             USBD_DescriptorsTypeDef *descriptors)
{
    uint32_t offset = 0U, previous;
    uint16_t interfaces = 0U, alt_zero = 0U;
    uint32_t endpoint_mask = 0U, endpoint_bit;
    uint8_t endpoint_count = 0U, declared_endpoints = 0U;
    uint8_t interface_number = 0U, alternate = 0U, type;
    uint16_t packet;
    bool have_interface = false;
    bool have_string_index;

    //先核对配置头、总长度、接口数量及配置字符串回调
    if (config == NULL || descriptors == NULL || length < 9U ||
        config[0] != 9U || config[1] != USB_DESC_TYPE_CONFIGURATION ||
        ((uint16_t)config[2] | ((uint16_t)config[3] << 8)) != length ||
        config[4] == 0U || config[4] > 16U || config[5] == 0U ||
        (config[7] & 0x80U) == 0U || (config[7] & 0x1FU) != 0U ||
        (config[6] != 0U && descriptors->GetConfigurationStrDescriptor == NULL))
        return SYS_ERR_INVALID_ARG;
    if ((uint32_t)config[4] - 1U > USBD_MAX_NUM_INTERFACES) return SYS_ERR_NOT_SUPPORTED;
    have_string_index = config[6] != 0U;

    //逐项检查描述符长度，防止跳出数组或停在零长度项
    while (offset < length)
    {
        uint8_t *desc = config + offset;
        if (length - offset < 2U || desc[0] < 2U || desc[0] > length - offset)
            return SYS_ERR_INVALID_ARG;

        //接口编号与Alt组合必须唯一，并核对上一接口的端点数量
        if (desc[1] == USB_DESC_TYPE_INTERFACE)
        {
            if (desc[0] != 9U || desc[2] >= config[4] ||
                (have_interface && endpoint_count != declared_endpoints) ||
                (desc[8] != 0U && descriptors->GetInterfaceStrDescriptor == NULL))
                return SYS_ERR_INVALID_ARG;
            for (previous = 9U; previous < offset; )
            {
                uint8_t *old = config + previous;
                if (old[1] == USB_DESC_TYPE_INTERFACE && old[2] == desc[2] && old[3] == desc[3])
                    return SYS_ERR_INVALID_ARG;
                previous += old[0];
            }
            if (desc[8] != 0U) have_string_index = true;
            interface_number = desc[2]; alternate = desc[3];
            interfaces |= (uint16_t)(1UL << interface_number);
            if (alternate == 0U) alt_zero |= (uint16_t)(1UL << interface_number);
            declared_endpoints = desc[4]; endpoint_count = 0U; endpoint_mask = 0U; have_interface = true;
        }

        //校验数据端点地址、传输类型和全速最大包长，不允许声明EP0
        else if (desc[1] == USB_DESC_TYPE_ENDPOINT)
        {
            if (desc[0] < 7U || !have_interface || (desc[2] & 0x70U) != 0U ||
                !Usb_ClassEndpoint(desc[2])) return SYS_ERR_INVALID_ARG;
            type = desc[3] & 3U;
            packet = (uint16_t)desc[4] | ((uint16_t)desc[5] << 8);
            if ((desc[3] & 0xC0U) != 0U || (packet & 0xF800U) != 0U)
                return SYS_ERR_INVALID_ARG;
            if (type != EP_TYPE_ISOC && (desc[3] & 0xFCU) != 0U)
                return SYS_ERR_INVALID_ARG;
            if (type == EP_TYPE_ISOC &&
                (((desc[3] >> 2) & 3U) == 3U || ((desc[3] >> 4) & 3U) == 3U))
                return SYS_ERR_INVALID_ARG;
            packet &= 0x07FFU;
            if (type == EP_TYPE_CTRL) return SYS_ERR_NOT_SUPPORTED;
            if (type == EP_TYPE_BULK && !(packet == 8U || packet == 16U || packet == 32U || packet == 64U))
                return SYS_ERR_NOT_SUPPORTED;
            if (type == EP_TYPE_INTR && (packet == 0U || packet > 64U))
                return SYS_ERR_NOT_SUPPORTED;
            if (type == EP_TYPE_ISOC && (packet == 0U || packet > 1023U))
                return SYS_ERR_NOT_SUPPORTED;
            if ((type == EP_TYPE_INTR && desc[6] == 0U) ||
                (type == EP_TYPE_ISOC && (desc[6] == 0U || desc[6] > 16U)))
                return SYS_ERR_INVALID_ARG;

            //用方向和编号组成独立位，拒绝同一备用设置内重复声明端点
            endpoint_bit = 1UL << ((desc[2] & 0x0FU) +
                            ((desc[2] & 0x80U) != 0U ? 16U : 0U));
            if ((endpoint_mask & endpoint_bit) != 0U)
                return SYS_ERR_INVALID_ARG;
            endpoint_mask |= endpoint_bit;
            ++endpoint_count;
        }

        //检查IAD接口范围及功能字符串回调，不解释具体Class拓扑
        else if (desc[1] == 0x0BU)
        {
            if (desc[0] != 8U || (uint16_t)desc[2] + desc[3] > config[4] ||
                desc[3] == 0U || (desc[7] != 0U && descriptors->GetInterfaceStrDescriptor == NULL))
                return SYS_ERR_INVALID_ARG;
            if (desc[7] != 0U) have_string_index = true;
        }
        offset += desc[0];
    }

    //出现字符串索引时必须有语言描述符，所有接口也必须具备Alt0
    if (have_string_index && descriptors->GetLangIDStrDescriptor == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!have_interface || endpoint_count != declared_endpoints ||
        interfaces != (uint16_t)((1UL << config[4]) - 1UL) ||
        alt_zero != (uint16_t)((1UL << config[4]) - 1UL))
        return SYS_ERR_INVALID_ARG;
    return SYS_OK;
}

//取消旧控制事务并分发新SETUP
static void Usb_Setup(uint8_t *data)
{
    USBD_StatusTypeDef status;
    uint8_t recipient;
    uint8_t ep;

    //没有Class或没有SETUP数据时不进行分发
    if (!s_registered || data == NULL)
        return;
    recipient = data[0] & 0x1FU;

    //新SETUP可能中止旧控制事务，先确认硬件停止再回收PIO上下文
    {
        uint8_t direction;
        for (direction = 0U; direction < 2U; ++direction)
        {
            uint8_t address = direction != 0U ? 0x80U : 0x00U;
            if (!s_endpoints[direction][0].busy)
                continue;
            if (USBD_LL_CloseEP(&s_device, address) != USBD_OK ||
                USBD_LL_OpenEP(&s_device, address, USBD_EP_TYPE_CTRL, USB_MAX_EP0_SIZE) != USBD_OK)
            {

                //无法确认旧控制事务已清理，不提交新事务或伪造完成
                s_stop_required = true;
                if (s_last_result == SYS_OK)
                    s_last_result = SYS_ERR_IO;
                return;
            }
        }
    }

    //USB新SETUP解除上一轮控制STALL，状态快照同步复位
    s_endpoints[0][0].stalled = false;
    s_endpoints[1][0].stalled = false;
    if ((data[0] & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_MASK)
    {
        USBD_ParseSetupRequest(&s_device.request, data);
        USBD_CtlError(&s_device, &s_device.request);
        return;
    }
    if (recipient == USB_REQ_RECIPIENT_ENDPOINT)
    {
        volatile UsbDeviceEndpointState_t *entry;
        ep = data[4];
        entry = Usb_Endpoint(ep);

        //USB规范不允许对等时端点设置或清除HALT
        if (entry != NULL && entry->type == USBD_EP_TYPE_ISOC &&
            (data[0] & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_STANDARD &&
            (data[1] == USB_REQ_SET_FEATURE || data[1] == USB_REQ_CLEAR_FEATURE) &&
            data[2] == USB_FEATURE_EP_HALT && data[3] == 0U)
        {
            USBD_ParseSetupRequest(&s_device.request, data);
            USBD_CtlError(&s_device, &s_device.request);
            return;
        }
        if (data[5] != 0U || entry == NULL || !entry->opened)
        {
            USBD_ParseSetupRequest(&s_device.request, data);
            USBD_CtlError(&s_device, &s_device.request);
            return;
        }
    }

    //标准请求保留官方实现，Class和Vendor由已注册Setup通用分发
    status = USBD_LL_SetupStage(&s_device, data);
    if (status != USBD_OK)
        USBD_CtlError(&s_device, &s_device.request);

    //请求处理完成后只通知状态变化，不把协议处理搬到任务
    Usb_Notify(USB_DEVICE_EVENT_STATE);
}

//交付OUT完成并通知上层
static void Usb_DataOut(uint8_t ep, uint8_t *data)
{
    if (!s_registered || ep >= USB_DEVICE_EP_COUNT || !s_endpoints[0][ep].opened ||
        (ep != 0U && s_class_state != USB_CLASS_ACTIVE))
        return;

    //BSP已经交付真实完成，先归还端点占用，再进入官方Core回调
    s_endpoints[0][ep].busy = false;
    if (USBD_LL_DataOutStage(&s_device, ep, data) != USBD_OK)
    {
        s_endpoints[0][ep].last_result = SYS_ERR_IO;
        s_last_result = SYS_ERR_IO;
    }

    //完成协议回调后唤醒上层任务，通知本身不代表新一帧可释放
    Usb_Notify(USB_DEVICE_EVENT_IO);
}

//交付IN完成并通知上层
static void Usb_DataIn(uint8_t ep, uint8_t *data)
{
    if (!s_registered || ep >= USB_DEVICE_EP_COUNT || !s_endpoints[1][ep].opened ||
        (ep != 0U && s_class_state != USB_CLASS_ACTIVE))
        return;
    if (ep != 0U)
    {
        if (!s_endpoints[1][ep].busy) return;
        s_in_completed[ep] = s_in_sequence[ep];
    }

    //BSP已经交付真实完成，先归还端点占用，再进入官方Core回调
    s_endpoints[1][ep].busy = false;
    if (USBD_LL_DataInStage(&s_device, ep, data) != USBD_OK)
    {
        s_endpoints[1][ep].last_result = SYS_ERR_IO;
        s_last_result = SYS_ERR_IO;
    }

    //完成协议回调后唤醒上层任务，通知本身不代表新一帧可释放
#if defined(APP_USB_ISO_TIMING_TEST_ENABLED) && APP_USB_ISO_TIMING_TEST_ENABLED

    //标记EP1完成通知开始，序号沿用Core已确认的事务
    if (ep == 1U)
        UsbIsoTiming_Record(USB_ISO_TIME_NOTIFY_BEGIN, s_in_completed[ep], 0U);
#endif
    Usb_Notify(USB_DEVICE_EVENT_IO);
#if defined(APP_USB_ISO_TIMING_TEST_ENABLED) && APP_USB_ISO_TIMING_TEST_ENABLED

    //标记通知返回，此时仍在原IRQ内，不代表任务已经运行
    if (ep == 1U)
        UsbIsoTiming_Record(USB_ISO_TIME_NOTIFY_END, s_in_completed[ep], 0U);
#endif
}

//记录IN传输失败结果
static void Usb_DataInError(uint8_t ep, SysResult_t result)
{
    if (ep >= USB_DEVICE_EP_COUNT || !s_endpoints[1][ep].opened) return;

    //BSP已交付失败结束，清除本次占用并保留原失败码，不上报成功完成
    s_endpoints[1][ep].busy = false;
    s_endpoints[1][ep].last_result = result;

    //先完成Core状态更新，再唤醒所属USB任务处理恢复
    Usb_Notify(USB_DEVICE_EVENT_IO);
}

//处理总线复位并恢复EP0
static void Usb_Reset(void)
{
    USBD_StatusTypeDef reset_result;
    SysResult_t release_result;
    if (!s_registered)
        return;

    //总线已复位，旧EP0打开状态不能代表本轮重建成功
    memset((void *)&s_endpoints[0][0], 0, sizeof(s_endpoints[0][0]));
    memset((void *)&s_endpoints[1][0], 0, sizeof(s_endpoints[1][0]));
    (void)USBD_LL_SetSpeed(&s_device, USBD_SPEED_FULL);

    //由官方Core处理Class复位并重新建立控制端点
    reset_result = USBD_LL_Reset(&s_device);

    //即使Class释放失败，也继续清理剩余数据端点
    release_result = Usb_ReleaseEndpoints();

    //官方Reset忽略OpenEP返回值，必须核对EP0实际资源状态
    if (reset_result != USBD_OK || release_result != SYS_OK ||
        !s_endpoints[0][0].opened || !s_endpoints[1][0].opened)
    {
        if (s_last_result == SYS_OK)
            s_last_result = release_result != SYS_OK ? release_result : SYS_ERR_IO;
        s_stop_required = true;
    }
    Usb_Notify(USB_DEVICE_EVENT_STATE);
}

//转交总线挂起事件
static void Usb_Suspend(void)
{

    //先更新官方Core挂起状态，再通知Application重新检查状态
    (void)USBD_LL_Suspend(&s_device);
    Usb_Notify(USB_DEVICE_EVENT_STATE);
}

//转交总线恢复事件
static void Usb_Resume(void)
{

    //先恢复官方Core状态，再发送任务唤醒提示
    (void)USBD_LL_Resume(&s_device);
    Usb_Notify(USB_DEVICE_EVENT_STATE);
}

//转交设备连接事件
static void Usb_Connect(void)
{

    //将连接事件交给Core，连接本身不代表枚举完成
    (void)USBD_LL_DevConnected(&s_device);
    Usb_Notify(USB_DEVICE_EVENT_STATE);
}

//处理断线并回收Class资源
static void Usb_Disconnect(void)
{
    if (!s_registered)
        return;

    //让官方Core处理Class断线释放，失败时锁存恢复要求
    if (USBD_LL_DevDisconnected(&s_device) != USBD_OK)
    {
        s_last_result = SYS_ERR_IO;
        s_stop_required = true;
    }

    //继续清理遗留数据端点，未全部释放时禁止直接重新启动
    if (Usb_ReleaseEndpoints() != SYS_OK)
        s_stop_required = true;
    Usb_Notify(USB_DEVICE_EVENT_STATE);
}

//转交IN等时传输未完成事件
static void Usb_IsoInIncomplete(uint8_t ep)
{
    if (!s_registered || ep == 0U || ep >= USB_DEVICE_EP_COUNT || !s_endpoints[1][ep].opened ||
        (ep != 0U && s_class_state != USB_CLASS_ACTIVE) || s_endpoints[1][ep].type != USBD_EP_TYPE_ISOC)
        return;

    //Incomplete并不证明Abort成功，busy和缓冲区继续保留
    s_endpoints[1][ep].last_result = SYS_ERR_TIMEOUT;
    if (USBD_LL_IsoINIncomplete(&s_device, ep) != USBD_OK)
    {

        //官方Core无法分发事件时，记录真正的Core错误
        s_endpoints[1][ep].last_result = SYS_ERR_IO;
        s_last_result = SYS_ERR_IO;
    }

    //先完成Core状态更新，再唤醒所属USB任务处理恢复
    Usb_Notify(USB_DEVICE_EVENT_IO);
}

//转交OUT等时传输未完成事件
static void Usb_IsoOutIncomplete(uint8_t ep)
{
    if (!s_registered || ep == 0U || ep >= USB_DEVICE_EP_COUNT || !s_endpoints[0][ep].opened ||
        (ep != 0U && s_class_state != USB_CLASS_ACTIVE) || s_endpoints[0][ep].type != USBD_EP_TYPE_ISOC)
        return;

    //按现有OUT完成路径更新占用和超时状态，再转交Core回调
    s_endpoints[0][ep].busy = false;
    s_endpoints[0][ep].last_result = SYS_ERR_TIMEOUT;
    if (USBD_LL_IsoOUTIncomplete(&s_device, ep) != USBD_OK)
    {
        s_endpoints[0][ep].last_result = SYS_ERR_IO;
        s_last_result = SYS_ERR_IO;
    }

    //先完成Core状态更新，再唤醒所属USB任务处理恢复
    Usb_Notify(USB_DEVICE_EVENT_IO);
}

//准备通用Core和BSP
SysResult_t Usb_InitDevice(void)
{
    USBD_StatusTypeDef status;

    //已经准备成功时保持幂等，不重复初始化硬件
    if (s_initialized)
        return SYS_OK;

    //只准备Core和BSP，未注册Class时禁止Start
    s_last_result = SYS_OK;
    s_operation_result = SYS_OK;

    //官方Core通过USBD_LL_Init回到本文件，再由BSP配置PCD
    status = USBD_Init(&s_device, NULL, 0U);
    if (status != USBD_OK)
        return Usb_PublicResult(status);
    s_initialized = true;
    return SYS_OK;
}

//校验并登记外部Class和描述符
SysResult_t Usb_RegisterClass(USBD_ClassTypeDef *driver, USBD_DescriptorsTypeDef *descriptors)
{
    uint8_t *config;
    uint8_t *device_desc;
    uint16_t length = 0U;
    uint16_t device_length = 0U;
    SysResult_t result;

    //必需回调先判空，字符串回调可按实际描述符选择提供
    if (driver == NULL || descriptors == NULL ||
        driver->Init == NULL || driver->DeInit == NULL ||
        driver->Setup == NULL || driver->GetFSConfigDescriptor == NULL ||
        descriptors->GetDeviceDescriptor == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!s_initialized || s_started || s_stop_required || __get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;

    //读取外部静态配置并校验完整边界，不在Core拼接具体Class内容
    config = driver->GetFSConfigDescriptor(&length);
    result = Usb_ValidateConfig(config, length, descriptors);
    if (result != SYS_OK)
        return result;
    device_desc = descriptors->GetDeviceDescriptor(USBD_SPEED_FULL, &device_length);
    if (device_desc == NULL || device_length != USB_LEN_DEV_DESC ||
        device_desc[0] != USB_LEN_DEV_DESC || device_desc[1] != USB_DESC_TYPE_DEVICE ||
        device_desc[7] != USB_MAX_EP0_SIZE || device_desc[17] != 1U)
        return SYS_ERR_INVALID_ARG;

    //声明了字符串索引，就必须提供相应查询回调
    if ((device_desc[14] != 0U && descriptors->GetManufacturerStrDescriptor == NULL) ||
        (device_desc[15] != 0U && descriptors->GetProductStrDescriptor == NULL) ||
        (device_desc[16] != 0U && descriptors->GetSerialStrDescriptor == NULL) ||
        ((device_desc[14] | device_desc[15] | device_desc[16]) != 0U &&
         descriptors->GetLangIDStrDescriptor == NULL))
        return SYS_ERR_INVALID_ARG;

    //在修改已登记对象前完成校验，不调用外部Init或启动硬件
    if (s_class_state != USB_CLASS_INACTIVE || s_class_memory_used)
        return SYS_ERR_BUSY;
    result = Usb_PlanEndpoints(config, length);
    if (result != SYS_OK)
    {
        if (result == SYS_ERR_IO || result == SYS_ERR_TIMEOUT)
        {
            s_last_result = result;
            s_stop_required = true;
        }
        return result;
    }

    //校验和FIFO规划成功后再复制操作表，用包装入口管理生命周期
    s_class_callbacks = *driver;
    s_class_driver = *driver;
    s_class_driver.Init = Usb_ManagedClassInit;
    s_class_driver.DeInit = Usb_ManagedClassDeInit;
    s_descriptor_driver = *descriptors;
    s_configuration = config;
    s_configuration_length = length;

    //官方Core通过通用入口读取已验证的配置，不绑定具体数组
    s_class_driver.GetFSConfigDescriptor = USB_Device_GetConfigDescriptor;
    s_device.classId = 0U;
    s_device.NumClasses = 0U;
    if (USBD_RegisterClass(&s_device, &s_class_driver) != USBD_OK)
        return SYS_ERR_IO;
    s_device.pDesc = &s_descriptor_driver;
    s_registered = true;
    return SYS_OK;
}

//启动USB设备连接
SysResult_t Usb_StartDevice(void)
{
    USBD_StatusTypeDef status;

    //必须已准备并登记Class，且没有未恢复故障
    if (!s_initialized || !s_registered || s_stop_required || __get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;

    //重复连接请求直接返回BUSY，不再次启动PCD
    if (s_started)
        return SYS_ERR_BUSY;
    s_last_result = SYS_OK;
    s_operation_result = SYS_OK;
    s_device.dev_state = USBD_STATE_DEFAULT;
    s_device.dev_config = 0U;

    //先提交允许分发状态，再由BSP开放硬件IRQ
    s_started = true;
    status = USBD_Start(&s_device);
    if (status != USBD_OK)
    {
        s_started = false;
        return Usb_PublicResult(status);
    }
    if (s_last_result != SYS_OK)
        s_stop_required = true;
    return s_last_result;
}

//停止硬件并释放USB资源
SysResult_t Usb_StopDevice(void)
{
    SysResult_t result;
    SysResult_t release_result;

    //停止只允许从普通上下文进入，不能在USB ISR中释放生命周期资源
    if (!s_initialized || __get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;
    if (!s_started && !s_stop_required)
        return SYS_OK;

    //官方Stop忽略LL结果，先确认硬件停止再释放Class
    s_last_result = SYS_OK;
    s_class_user_data = s_device.pUserData[0];
    result = BSP_USB_Stop();
    s_hardware_stop_failed = result != SYS_OK;
    if (result != SYS_OK)
    {
        s_stop_required = true;
        s_last_result = result;
        return result;
    }
    s_started = false;

    //硬件已确认停止后才调用Class释放，并继续回收数据端点
    result = SYS_OK;
    if (s_registered && s_class_driver.DeInit(&s_device, (uint8_t)s_device.dev_config) != USBD_OK)
        result = SYS_ERR_IO;
    release_result = Usb_ReleaseEndpoints();
    if (result == SYS_OK)
        result = release_result;
    if (result != SYS_OK)
    {
        s_stop_required = true;
        s_last_result = result;
        return result;
    }

    //硬件已停、Class已释放，恢复本Class完整FIFO布局后才允许重启
    result = Usb_PlanEndpoints(s_configuration, s_configuration_length);
    if (result != SYS_OK)
    {
        s_stop_required = true;
        s_last_result = result;
        return result;
    }
    memset((void *)s_endpoints, 0, sizeof(s_endpoints));
    memset(s_device.ep_in, 0, sizeof(s_device.ep_in));
    memset(s_device.ep_out, 0, sizeof(s_device.ep_out));
    s_device.dev_state = USBD_STATE_DEFAULT;
    s_device.dev_config = 0U;
    s_device.ep0_state = USBD_EP0_IDLE;
    s_stop_required = false;
    return SYS_OK;
}

//提取描述符端点需求并规划FIFO
static SysResult_t Usb_PlanEndpoints(uint8_t *config, uint16_t length)
{
    BspUsbEndpointConfig_t entries[BSP_USB_MAX_ENDPOINT_CONFIGS];
    uint32_t count = 0U, offset = 0U;
    SysResult_t result;
    if (config == NULL && length == 0U)
    {
        result = BSP_USB_ConfigureEndpoints(NULL, 0U);
        if (result == SYS_OK) s_endpoint_requirement_count = 0U;
        return result;
    }

    //按已验证的描述符边界遍历，提取所有备用设置中的端点需求
    while (offset < length)
    {
        uint8_t *desc = config + offset;
        if (desc[1] == USB_DESC_TYPE_ENDPOINT)
        {
            if (count == BSP_USB_MAX_ENDPOINT_CONFIGS) return SYS_ERR_NO_RESOURCE;
            entries[count].address = desc[2];
            entries[count].type = desc[3] & 3U;
            entries[count].max_packet = ((uint16_t)desc[4] | ((uint16_t)desc[5] << 8)) & 0x07FFU;
            ++count;
        }
        offset += desc[0];
    }

    //让BSP根据通用需求规划FIFO，成功后才保存端点声明集合
    result = BSP_USB_ConfigureEndpoints(entries, count);
    if (result == SYS_OK)
    {
        memcpy(s_endpoint_requirements, entries, count * sizeof(entries[0]));
        s_endpoint_requirement_count = count;
    }
    return result;
}

//执行受保护的Class初始化
static uint8_t Usb_ManagedClassInit(USBD_HandleTypeDef *device, uint8_t config)
{
    uint8_t status;
    if (device != &s_device || s_stop_required || s_class_state != USB_CLASS_INACTIVE || s_class_memory_used)
        return USBD_FAIL;

    //先提交初始化状态，外部回调执行期间禁止生命周期重入
    s_class_state = USB_CLASS_INITIALIZING;
    {
        bool control_busy = s_control_busy;
        s_control_busy = true;
        status = s_class_callbacks.Init(device, config);
        s_control_busy = control_busy;
    }
    if (status == USBD_OK)
        s_class_state = USB_CLASS_ACTIVE;
    else
    {

        //失败Init也可能持有部分资源，留给Stop清理
        s_class_state = USB_CLASS_RELEASE_FAILED;
        s_stop_required = true;
        s_last_result = SYS_ERR_IO;
    }
    return status;
}

//执行受保护的Class反初始化
static uint8_t Usb_ManagedClassDeInit(USBD_HandleTypeDef *device, uint8_t config)
{
    uint8_t status;
    bool control_busy;

    //硬件未安全停止或生命周期正在重入时禁止释放上下文
    if (device != &s_device || s_hardware_stop_failed ||
        s_class_state == USB_CLASS_INITIALIZING || s_class_state == USB_CLASS_DEINITIALIZING)
        return USBD_FAIL;
    if (s_class_state == USB_CLASS_INACTIVE)
    {
        if (!s_class_memory_used)
            return Usb_ReleaseEndpoints() == SYS_OK ? USBD_OK : USBD_FAIL;

        //外部DeInit已成功但未Free，报告泄漏，不重复调用回调
        s_last_result = SYS_ERR_IO;
        s_stop_required = true;
        return USBD_FAIL;
    }
    if (s_class_state != USB_CLASS_RELEASING)
    {
        s_class_state = USB_CLASS_DEINITIALIZING;
        s_class_free_pending = false;
        control_busy = s_control_busy;
        s_control_busy = true;

        //先保存外部绑定，DeInit失败时恢复其所有权
        s_class_user_data = s_device.pUserData[0];
        status = s_class_callbacks.DeInit(device, config);
        s_control_busy = control_busy;
        if (status != USBD_OK)
        {
            s_device.pUserData[0] = s_class_user_data;
            s_class_state = USB_CLASS_RELEASE_FAILED;
            s_class_free_pending = false;
            s_stop_required = true;
            s_last_result = SYS_ERR_IO;

            //释放失败仍清理可关闭的数据端点，内存所有权继续保留
            (void)Usb_ReleaseEndpoints();
            return status;
        }
        s_device.pUserData[0] = NULL;
        s_class_user_data = NULL;
        s_class_state = USB_CLASS_RELEASING;
    }

    //外部回调已成功，剩余端点失败时只重试清理，不重复DeInit
    if (Usb_ReleaseEndpoints() != SYS_OK)
    {
        s_stop_required = true;
        return USBD_FAIL;
    }

    //外部回调和端点清理全部成功后才提交最终回收状态
    s_class_state = USB_CLASS_INACTIVE;
    if (s_class_free_pending)
        s_class_memory_used = false;
    s_class_free_pending = false;
    if (s_class_memory_used)
    {
        s_stop_required = true;
        s_last_result = SYS_ERR_IO;
        return USBD_FAIL;
    }
    return USBD_OK;
}

//去重SOF并转交官方Core
static void Usb_Sof(uint16_t frame)
{

    //过滤相同硬件帧号，避免一次SOF重复增加序号
    if (s_sof_valid && frame == s_sof_frame) return;
    s_sof_valid = true;
    s_sof_frame = frame;
    s_sof_sequence++;

    //由官方Core转交当前Class，不在Platform调度具体视频源
    (void)USBD_LL_SOF(&s_device);

#if defined(APP_USB_ISO_TIMING_TEST_ENABLED) && APP_USB_ISO_TIMING_TEST_ENABLED

    //记录真实SOF通知前时刻，用于核对完成与唤醒的相位
    UsbIsoTiming_Record(USB_ISO_TIME_SOF, s_sof_sequence, frame);
#endif

    //每个真实SOF唤醒UsbTask，不在ISR构包，也不依赖20ms轮询
    Usb_Notify(USB_DEVICE_EVENT_IO);
}

//通知上层USB状态已经变化
static void Usb_Notify(uint32_t events)
{

    //沿用当前调用上下文执行静态回调，由Application选择任务或ISR通知接口
    if (s_event_callback != NULL)
        s_event_callback(events, s_event_context);
}
