//通用USB管理：官方Core负责协议，BSP负责硬件
#include "usb_device.h"
#include "bsp_usb.h"
#include "framework_init.h"
#include "usbd_core.h"
#include "usbd_ctlreq.h"
#include "stm32h7xx_hal.h"
#include <stddef.h>
#include <string.h>

//对应官方Core的16个端点编号，不预设Class用途
#define USB_DEVICE_EP_COUNT 16U

static USBD_HandleTypeDef s_device;
static volatile bool s_initialized;
static volatile bool s_started;
static volatile bool s_registered;
static volatile bool s_stop_required;

//生命周期回调执行期间禁止重入Init、Register、Start和Stop
static volatile bool s_control_busy;
static volatile SysResult_t s_last_result;
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

static volatile UsbClassState_t s_class_state;
static USBD_ClassTypeDef s_class_callbacks;
static USBD_ClassTypeDef s_class_driver;

//官方DeInit会清空pUserData，失败时保留Class当前的外部绑定
static void *s_class_user_data;
static volatile bool s_hardware_stop_failed;
static volatile bool s_class_free_pending;
static USBD_DescriptorsTypeDef s_descriptor_driver;
static uint8_t *s_configuration;
static uint16_t s_configuration_length;

//注册描述符声明的精确端点集合，包含不同Alternate Setting
static BspUsbEndpointConfig_t s_endpoint_requirements[BSP_USB_MAX_ENDPOINT_CONFIGS];
static uint32_t s_endpoint_requirement_count;

//保留固定Class内存块，不使用malloc
static uint8_t s_class_memory[1024] __attribute__((aligned(32)));
static volatile bool s_class_memory_used;

//两个方向独立管理资源，不规定具体端点类型或地址
static volatile UsbDeviceEndpointState_t s_endpoints[2][USB_DEVICE_EP_COUNT];

//准备硬件与官方Core，不登记具体功能
static SysResult_t Usb_InitDevice(void);

//校验并登记外部Class和描述符
static SysResult_t Usb_RegisterClass(USBD_ClassTypeDef *driver, USBD_DescriptorsTypeDef *descriptors);

//在生命周期保护内启动连接
static SysResult_t Usb_StartDevice(void);

//在生命周期保护内停止并释放资源
static SysResult_t Usb_StopDevice(void);

//包装外部生命周期，成功Init才形成一次活动生命周期
static uint8_t Usb_ManagedClassInit(USBD_HandleTypeDef *device, uint8_t config);

//同一次成功释放只调用外部DeInit一次，失败保留所有权
static uint8_t Usb_ManagedClassDeInit(USBD_HandleTypeDef *device, uint8_t config);

//从通用Endpoint Descriptor提取需求，不理解Class专用描述符
static SysResult_t Usb_PlanEndpoints(uint8_t *config, uint16_t length);

//校验外部配置边界、接口和端点数量
static SysResult_t Usb_ValidateConfig(uint8_t *config, uint16_t length,
                                             USBD_DescriptorsTypeDef *descriptors);

//按地址取得端点，不接受保留地址位
static volatile UsbDeviceEndpointState_t *Usb_Endpoint(uint8_t ep);

//Class数据接口不能直接申请或关闭EP0
static bool Usb_ClassEndpoint(uint8_t ep);

//只屏蔽USB IRQ，保留SysTick以供HAL超时使用
static uint32_t Usb_Lock(void);

//恢复进入接口前的USB IRQ使能状态
static void Usb_Unlock(uint32_t enabled);

//清理Class未释放的非EP0端点
static SysResult_t Usb_ReleaseEndpoints(void);

//将官方状态转换为最近一次通用结果
static SysResult_t Usb_PublicResult(USBD_StatusTypeDef status);

//锁存底层失败并转换为官方Core状态
static USBD_StatusTypeDef Usb_LlResult(SysResult_t result);

//转交SETUP并保证失败请求STALL
static void Usb_Setup(uint8_t *data);

//释放OUT缓冲区后转交外部DataOut
static void Usb_DataOut(uint8_t ep, uint8_t *data);

//释放IN缓冲区后转交外部DataIn
static void Usb_DataIn(uint8_t ep, uint8_t *data);
static void Usb_DataInError(uint8_t ep, SysResult_t result);
static void Usb_HardwareError(SysResult_t result);

//总线复位由官方Core重建EP0
static void Usb_Reset(void);

//将挂起事件交给官方Core
static void Usb_Suspend(void);

//将恢复事件交给官方Core
static void Usb_Resume(void);

//将连接事件交给官方Core
static void Usb_Connect(void);

//断线时释放Class和端点资源
static void Usb_Disconnect(void);

//将IN未完成事件交给外部IsoINIncomplete
static void Usb_IsoInIncomplete(uint8_t ep);

//将OUT未完成事件交给外部IsoOUTIncomplete
static void Usb_IsoOutIncomplete(uint8_t ep);

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
    Usb_HardwareError
};

SysResult_t USB_Device_Init(void)
{
    SysResult_t result;
    if (s_control_busy)
        return SYS_ERR_BUSY;
    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;
    s_control_busy = true;
    result = Usb_InitDevice();
    s_control_busy = false;
    return result;
}

SysResult_t USB_Device_RegisterClass(USBD_ClassTypeDef *driver, USBD_DescriptorsTypeDef *descriptors)
{
    SysResult_t result;
    if (s_control_busy)
        return SYS_ERR_BUSY;
    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;
    s_control_busy = true;
    result = Usb_RegisterClass(driver, descriptors);
    s_control_busy = false;
    return result;
}

uint8_t *USB_Device_GetConfigDescriptor(uint16_t *length)
{
    if (length == NULL)
        return NULL;
    *length = s_configuration_length;
    return s_configuration;
}

SysResult_t USB_Device_Start(void)
{
    SysResult_t result;
    if (s_control_busy)
        return SYS_ERR_BUSY;
    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;
    s_control_busy = true;
    result = Usb_StartDevice();
    s_control_busy = false;
    return result;
}

SysResult_t USB_Device_Stop(void)
{
    SysResult_t result;
    if (s_control_busy)
        return SYS_ERR_BUSY;
    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;
    s_control_busy = true;
    result = Usb_StopDevice();
    s_control_busy = false;
    return result;
}

bool USB_Device_IsConfigured(void)
{
    return s_started && !s_stop_required && s_device.dev_state == USBD_STATE_CONFIGURED;
}

SysResult_t USB_Device_GetLastResult(void)
{
    return s_last_result;
}

SysResult_t USB_Device_GetLastOperationResult(void)
{
    return s_operation_result;
}

SysResult_t USB_Device_OpenEp(uint8_t ep, uint8_t type, uint16_t size)
{
    if (!Usb_ClassEndpoint(ep))
        return SYS_ERR_INVALID_ARG;
    return Usb_PublicResult(USBD_LL_OpenEP(&s_device, ep, type, size));
}

SysResult_t USB_Device_AbortEp(uint8_t ep)
{
    volatile UsbDeviceEndpointState_t *entry = Usb_Endpoint(ep);
    SysResult_t result;
    uint32_t enabled;
    if (!Usb_ClassEndpoint(ep) || entry == NULL)
        return SYS_ERR_INVALID_ARG;
    enabled = Usb_Lock();
    if (!s_initialized || !entry->opened)
        result = SYS_ERR_INVALID_STATE;
    else
    {
        result = BSP_USB_AbortEp(ep);
        if (result == SYS_OK)
        {
            entry->busy = false;
            entry->last_result = SYS_OK;
        }
        else entry->last_result = result;
    }
    Usb_Unlock(enabled);
    return result;
}

SysResult_t USB_Device_CloseEp(uint8_t ep)
{
    if (!Usb_ClassEndpoint(ep))
        return SYS_ERR_INVALID_ARG;
    return Usb_PublicResult(USBD_LL_CloseEP(&s_device, ep));
}

SysResult_t USB_Device_FlushEp(uint8_t ep)
{
    if (!Usb_ClassEndpoint(ep))
        return SYS_ERR_INVALID_ARG;
    return Usb_PublicResult(USBD_LL_FlushEP(&s_device, ep));
}

SysResult_t USB_Device_Transmit(uint8_t ep, uint8_t *data, uint32_t size)
{
    if (!Usb_ClassEndpoint(ep))
        return SYS_ERR_INVALID_ARG;
    return Usb_PublicResult(USBD_LL_Transmit(&s_device, ep, data, size));
}

SysResult_t USB_Device_Receive(uint8_t ep, uint8_t *data, uint32_t size)
{
    if (!Usb_ClassEndpoint(ep))
        return SYS_ERR_INVALID_ARG;
    return Usb_PublicResult(USBD_LL_PrepareReceive(&s_device, ep, data, size));
}

SysResult_t USB_Device_StallEp(uint8_t ep)
{
    if (!Usb_ClassEndpoint(ep))
        return SYS_ERR_INVALID_ARG;
    return Usb_PublicResult(USBD_LL_StallEP(&s_device, ep));
}

SysResult_t USB_Device_ClearStallEp(uint8_t ep)
{
    if (!Usb_ClassEndpoint(ep))
        return SYS_ERR_INVALID_ARG;
    return Usb_PublicResult(USBD_LL_ClearStallEP(&s_device, ep));
}

SysResult_t USB_Device_GetEpState(uint8_t ep, UsbDeviceEndpointState_t *state)
{
    volatile UsbDeviceEndpointState_t *entry = Usb_Endpoint(ep);
    uint32_t enabled;
    if (entry == NULL || state == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!s_initialized)
        return SYS_ERR_INVALID_STATE;
    enabled = Usb_Lock();
    *state = *entry;
    Usb_Unlock(enabled);
    return SYS_OK;
}

uint32_t USB_Device_GetRxSize(uint8_t ep)
{
    if (!Usb_ClassEndpoint(ep) || (ep & 0x80U) != 0U)
        return 0U;
    return USBD_LL_GetRxDataSize(&s_device, ep);
}

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *device)
{
    SysResult_t result;
    if (device != &s_device)
        return USBD_FAIL;
    result = BSP_USB_Init();
    if (result == SYS_OK) result = BSP_USB_SetCallbacks(&s_callbacks);
    return Usb_LlResult(result);
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *device)
{
    SysResult_t result = SYS_ERR_IO;
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

USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *device)
{
    if (device != &s_device)
        return USBD_FAIL;
    return Usb_LlResult(BSP_USB_Start());
}

USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *device)
{
    SysResult_t result;
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

USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *device, uint8_t ep, uint8_t type, uint16_t size)
{
    volatile UsbDeviceEndpointState_t *entry = Usb_Endpoint(ep);
    SysResult_t result;
    uint32_t enabled;
    if (device != &s_device || entry == NULL || size == 0U || type > USBD_EP_TYPE_INTR)
        return Usb_LlResult(SYS_ERR_INVALID_ARG);
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
    //Open必须与注册时某个Alternate Setting的端点声明完全匹配
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

USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    volatile UsbDeviceEndpointState_t *entry = Usb_Endpoint(ep);
    SysResult_t result;
    uint32_t enabled;
    if (device != &s_device || entry == NULL)
        return Usb_LlResult(SYS_ERR_INVALID_ARG);
    if (!s_initialized)
        return Usb_LlResult(SYS_ERR_INVALID_STATE);
    enabled = Usb_Lock();
    result = entry->opened ? BSP_USB_CloseEp(ep) : SYS_OK;
    entry->last_result = result;
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

USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    volatile UsbDeviceEndpointState_t *entry = Usb_Endpoint(ep);
    SysResult_t result;
    uint32_t enabled;
    uint32_t index;
    if (device != &s_device || entry == NULL)
        return Usb_LlResult(SYS_ERR_INVALID_ARG);
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

USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    volatile UsbDeviceEndpointState_t *entry = Usb_Endpoint(ep);
    SysResult_t result;
    uint32_t enabled;
    if (device != &s_device || entry == NULL)
        return Usb_LlResult(SYS_ERR_INVALID_ARG);
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

USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    volatile UsbDeviceEndpointState_t *entry = Usb_Endpoint(ep);
    SysResult_t result;
    uint32_t enabled;
    if (device != &s_device || entry == NULL)
        return Usb_LlResult(SYS_ERR_INVALID_ARG);
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

uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    return device == &s_device && BSP_USB_IsStallEp(ep) ? 1U : 0U;
}

USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *device, uint8_t address)
{
    if (device != &s_device)
        return USBD_FAIL;
    return Usb_LlResult(BSP_USB_SetAddress(address));
}

USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *device, uint8_t ep, uint8_t *buffer, uint32_t size)
{
    volatile UsbDeviceEndpointState_t *entry;
    SysResult_t result;
    uint32_t enabled;
    if (ep == 0U) ep = 0x80U;
    entry = Usb_Endpoint(ep);
    if (device != &s_device || entry == NULL || (ep & 0x80U) == 0U ||
        (buffer == NULL && size != 0U))
        return Usb_LlResult(SYS_ERR_INVALID_ARG);
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
    entry->busy = true;
    result = BSP_USB_Transmit(ep, buffer, size);
    entry->last_result = result;
    if (result != SYS_OK)
        entry->busy = false;
    Usb_Unlock(enabled);
    return Usb_LlResult(result);
}

USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *device, uint8_t ep, uint8_t *buffer, uint32_t size)
{
    volatile UsbDeviceEndpointState_t *entry;
    SysResult_t result;
    uint32_t enabled;
    entry = Usb_Endpoint(ep);
    if (device != &s_device || entry == NULL || (ep & 0x80U) != 0U ||
        (buffer == NULL && size != 0U))
        return Usb_LlResult(SYS_ERR_INVALID_ARG);
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
    entry->busy = true;
    result = BSP_USB_Receive(ep, buffer, size);
    entry->last_result = result;
    if (result != SYS_OK)
        entry->busy = false;
    Usb_Unlock(enabled);
    return Usb_LlResult(result);
}

uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *device, uint8_t ep)
{
    volatile UsbDeviceEndpointState_t *entry = Usb_Endpoint(ep);
    if (device != &s_device || entry == NULL || (ep & 0x80U) != 0U || !entry->opened)
        return 0U;
    return BSP_USB_GetRxSize(ep);
}

void USBD_LL_Delay(uint32_t delay)
{
    HAL_Delay(delay);
}

void *USB_Device_Alloc(uint32_t size)
{
    void *memory = NULL;
    uint32_t enabled = Usb_Lock();
    if (size != 0U && size <= sizeof(s_class_memory) && !s_class_memory_used &&
        s_class_state != USB_CLASS_DEINITIALIZING && s_class_state != USB_CLASS_RELEASE_FAILED && s_class_state != USB_CLASS_RELEASING)
    {
        s_class_memory_used = true;
        memory = s_class_memory;
    }
    Usb_Unlock(enabled);
    return memory;
}

void USB_Device_Free(void *memory)
{
    uint32_t enabled = Usb_Lock();
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

static USBD_StatusTypeDef Usb_LlResult(SysResult_t result)
{
    s_operation_result = result;
    if (result == SYS_OK) return USBD_OK;
    //只有真正的底层I/O故障升级为设备级故障
    if (result == SYS_ERR_IO) s_last_result = result;
    return result == SYS_ERR_BUSY ? USBD_BUSY : USBD_FAIL;
}

static SysResult_t Usb_PublicResult(USBD_StatusTypeDef status)
{
    (void)status;
    return s_operation_result;
}

static volatile UsbDeviceEndpointState_t *Usb_Endpoint(uint8_t ep)
{
    if ((ep & 0x70U) != 0U || (ep & 0x0FU) >= USB_DEVICE_EP_COUNT)
        return NULL;
    return &s_endpoints[(ep & 0x80U) != 0U][ep & 0x0FU];
}

static bool Usb_ClassEndpoint(uint8_t ep)
{
    return Usb_Endpoint(ep) != NULL && (ep & 0x0FU) != 0U;
}

static uint32_t Usb_Lock(void)
{
    uint32_t enabled = NVIC_GetEnableIRQ(OTG_FS_IRQn);
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    return enabled;
}

static void Usb_Unlock(uint32_t enabled)
{
    if (enabled != 0U)
        HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}

static SysResult_t Usb_ReleaseEndpoints(void)
{
    uint8_t direction;
    uint8_t index;
    SysResult_t result = SYS_OK;
    for (direction = 0U; direction < 2U; ++direction)
    {
        for (index = 1U; index < USB_DEVICE_EP_COUNT; ++index)
        {
            if (s_endpoints[direction][index].opened &&
                USBD_LL_CloseEP(&s_device, index | (direction ? 0x80U : 0U)) != USBD_OK &&
                result == SYS_OK)
                result = s_last_result;
        }
    }
    return result;
}

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
    if (config == NULL || descriptors == NULL || length < 9U ||
        config[0] != 9U || config[1] != USB_DESC_TYPE_CONFIGURATION ||
        ((uint16_t)config[2] | ((uint16_t)config[3] << 8)) != length ||
        config[4] == 0U || config[4] > 16U || config[5] == 0U ||
        (config[6] != 0U && descriptors->GetConfigurationStrDescriptor == NULL))
        return SYS_ERR_INVALID_ARG;
    if ((uint32_t)config[4] - 1U > USBD_MAX_NUM_INTERFACES) return SYS_ERR_NOT_SUPPORTED;
    while (offset < length)
    {
        uint8_t *desc = config + offset;
        if (length - offset < 2U || desc[0] < 2U || desc[0] > length - offset)
            return SYS_ERR_INVALID_ARG;
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
            interface_number = desc[2]; alternate = desc[3];
            interfaces |= (uint16_t)(1UL << interface_number);
            if (alternate == 0U) alt_zero |= (uint16_t)(1UL << interface_number);
            declared_endpoints = desc[4]; endpoint_count = 0U; endpoint_mask = 0U; have_interface = true;
        }
        else if (desc[1] == USB_DESC_TYPE_ENDPOINT)
        {
            if (desc[0] < 7U || !have_interface || (desc[2] & 0x70U) != 0U ||
                !Usb_ClassEndpoint(desc[2])) return SYS_ERR_INVALID_ARG;
            type = desc[3] & 3U;
            packet = (uint16_t)desc[4] | ((uint16_t)desc[5] << 8);
            if ((desc[3] & 0xC0U) != 0U || (packet & 0xF800U) != 0U)
                return SYS_ERR_INVALID_ARG;
            packet &= 0x07FFU;
            if (type == EP_TYPE_CTRL) return SYS_ERR_NOT_SUPPORTED;
            if (type == EP_TYPE_BULK && !(packet == 8U || packet == 16U || packet == 32U || packet == 64U))
                return SYS_ERR_NOT_SUPPORTED;
            if (type == EP_TYPE_INTR && (packet == 0U || packet > 64U))
                return SYS_ERR_NOT_SUPPORTED;
            if (type == EP_TYPE_ISOC && (packet == 0U || packet > 1023U))
                return SYS_ERR_NOT_SUPPORTED;
            if ((type == EP_TYPE_INTR || type == EP_TYPE_ISOC) && (desc[6] == 0U || desc[6] > 16U))
                return SYS_ERR_INVALID_ARG;
            endpoint_bit = 1UL << ((desc[2] & 0x0FU) +
                            ((desc[2] & 0x80U) != 0U ? 16U : 0U));
            if ((endpoint_mask & endpoint_bit) != 0U)
                return SYS_ERR_INVALID_ARG;
            endpoint_mask |= endpoint_bit;
            ++endpoint_count;
        }
        else if (desc[1] == 0x0BU)
        {
            if (desc[0] != 8U || (uint16_t)desc[2] + desc[3] > config[4] ||
                desc[3] == 0U || (desc[7] != 0U && descriptors->GetInterfaceStrDescriptor == NULL))
                return SYS_ERR_INVALID_ARG;
        }
        offset += desc[0];
    }
    if (!have_interface || endpoint_count != declared_endpoints ||
        interfaces != (uint16_t)((1UL << config[4]) - 1UL) ||
        alt_zero != (uint16_t)((1UL << config[4]) - 1UL))
        return SYS_ERR_INVALID_ARG;
    return SYS_OK;
}

static void Usb_Setup(uint8_t *data)
{
    USBD_StatusTypeDef status;
    uint8_t recipient;
    uint8_t ep;
    if (!s_registered || data == NULL)
        return;
    recipient = data[0] & 0x1FU;

    //新SETUP开始新控制事务，前一轮EP0所有权作废
    s_endpoints[0][0].busy = false;
    s_endpoints[1][0].busy = false;

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
}

static void Usb_DataOut(uint8_t ep, uint8_t *data)
{
    if (!s_registered || ep >= USB_DEVICE_EP_COUNT || !s_endpoints[0][ep].opened ||
        (ep != 0U && s_class_state != USB_CLASS_ACTIVE))
        return;
    s_endpoints[0][ep].busy = false;
    if (USBD_LL_DataOutStage(&s_device, ep, data) != USBD_OK)
    {
        s_endpoints[0][ep].last_result = SYS_ERR_IO;
        s_last_result = SYS_ERR_IO;
    }
}

static void Usb_DataIn(uint8_t ep, uint8_t *data)
{
    if (!s_registered || ep >= USB_DEVICE_EP_COUNT || !s_endpoints[1][ep].opened ||
        (ep != 0U && s_class_state != USB_CLASS_ACTIVE))
        return;
    s_endpoints[1][ep].busy = false;
    if (USBD_LL_DataInStage(&s_device, ep, data) != USBD_OK)
    {
        s_endpoints[1][ep].last_result = SYS_ERR_IO;
        s_last_result = SYS_ERR_IO;
    }
}

//分包续传失败只影响对应端点，不污染设备级故障
static void Usb_DataInError(uint8_t ep, SysResult_t result)
{
    if (ep >= USB_DEVICE_EP_COUNT || !s_endpoints[1][ep].opened) return;
    s_endpoints[1][ep].busy = false;
    s_endpoints[1][ep].last_result = result;
}

//PCD全局错误属于设备级故障
static void Usb_HardwareError(SysResult_t result)
{
    s_last_result = result;
    s_stop_required = true;
}


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
}

static void Usb_Suspend(void)
{
    (void)USBD_LL_Suspend(&s_device);
}

static void Usb_Resume(void)
{
    (void)USBD_LL_Resume(&s_device);
}

static void Usb_Connect(void)
{
    (void)USBD_LL_DevConnected(&s_device);
}

static void Usb_Disconnect(void)
{
    if (!s_registered)
        return;
    if (USBD_LL_DevDisconnected(&s_device) != USBD_OK)
    {
        s_last_result = SYS_ERR_IO;
        s_stop_required = true;
    }
    if (Usb_ReleaseEndpoints() != SYS_OK)
        s_stop_required = true;
}

static void Usb_IsoInIncomplete(uint8_t ep)
{
    if (!s_registered || ep == 0U || ep >= USB_DEVICE_EP_COUNT || !s_endpoints[1][ep].opened ||
        (ep != 0U && s_class_state != USB_CLASS_ACTIVE) || s_endpoints[1][ep].type != USBD_EP_TYPE_ISOC)
        return;
    s_endpoints[1][ep].busy = false;
    s_endpoints[1][ep].last_result = SYS_ERR_TIMEOUT;
    if (USBD_LL_IsoINIncomplete(&s_device, ep) != USBD_OK)
    {
        //官方Core无法分发事件时，记录真正的Core错误
        s_endpoints[1][ep].last_result = SYS_ERR_IO;
        s_last_result = SYS_ERR_IO;
    }
}

static void Usb_IsoOutIncomplete(uint8_t ep)
{
    if (!s_registered || ep == 0U || ep >= USB_DEVICE_EP_COUNT || !s_endpoints[0][ep].opened ||
        (ep != 0U && s_class_state != USB_CLASS_ACTIVE) || s_endpoints[0][ep].type != USBD_EP_TYPE_ISOC)
        return;
    s_endpoints[0][ep].busy = false;
    s_endpoints[0][ep].last_result = SYS_ERR_TIMEOUT;
    if (USBD_LL_IsoOUTIncomplete(&s_device, ep) != USBD_OK)
    {
        s_endpoints[0][ep].last_result = SYS_ERR_IO;
        s_last_result = SYS_ERR_IO;
    }
}

//准备Core和BSP
SysResult_t Usb_InitDevice(void)
{
    USBD_StatusTypeDef status;
    if (s_initialized)
        return SYS_OK;

    //只准备Core和BSP，未注册Class时禁止Start
    s_last_result = SYS_OK;
    s_operation_result = SYS_OK;
    status = USBD_Init(&s_device, NULL, 0U);
    if (status != USBD_OK)
        return Usb_PublicResult(status);
    s_initialized = true;
    return SYS_OK;
}

//登记外部Class
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

//启动连接
SysResult_t Usb_StartDevice(void)
{
    USBD_StatusTypeDef status;
    if (!s_initialized || !s_registered || s_stop_required || __get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;
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

//停止并释放资源
SysResult_t Usb_StopDevice(void)
{
    SysResult_t result;
    SysResult_t release_result;
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

//提取所有备用设置的最大需求，同一地址和类型合并
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
    result = BSP_USB_ConfigureEndpoints(entries, count);
    if (result == SYS_OK)
    {
        memcpy(s_endpoint_requirements, entries, count * sizeof(entries[0]));
        s_endpoint_requirement_count = count;
    }
    return result;
}

//只包装生命周期，不实现任何Class功能
static uint8_t Usb_ManagedClassInit(USBD_HandleTypeDef *device, uint8_t config)
{
    uint8_t status;
    if (device != &s_device || s_stop_required || s_class_state != USB_CLASS_INACTIVE || s_class_memory_used)
        return USBD_FAIL;
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

//确认外部成功释放后才提交内存回收
static uint8_t Usb_ManagedClassDeInit(USBD_HandleTypeDef *device, uint8_t config)
{
    uint8_t status;
    bool control_busy;
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
