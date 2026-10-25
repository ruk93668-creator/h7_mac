#include "usb_device.h"
#include "bsp_usb.h"
#include "framework_init.h"
#include "usbd_core.h"
#include "usbd_ctlreq.h"
#include "stm32h7xx_hal.h"
#include <stddef.h>
#include <string.h>

//1209:0001只供内部测试，不唯一，不得用于生产或分发
#define USB_TEST_VID 0x1209U
#define USB_TEST_PID 0x0001U
static USBD_HandleTypeDef s_device;
static bool s_initialized;
static bool s_started;
static volatile SysResult_t s_last_result;
static uint8_t s_class_memory[1024] __attribute__((aligned(32)));
static bool s_class_memory_used;

//描述符额外留出尾部空间，适配PIO的Word读取
static uint8_t s_device_desc[20] __attribute__((aligned(4))) =
{
    18U, USB_DESC_TYPE_DEVICE, 0x00U, 0x02U, 0U, 0U, 0U, 64U,
    LOBYTE(USB_TEST_VID), HIBYTE(USB_TEST_VID),
    LOBYTE(USB_TEST_PID), HIBYTE(USB_TEST_PID),
    0x00U, 0x01U, 1U, 2U, 3U, 1U
};
//一个Vendor接口，无非零端点，不声明视频能力
static uint8_t s_config_desc[20] __attribute__((aligned(4))) =
{
    9U, USB_DESC_TYPE_CONFIGURATION, 18U, 0U, 1U, 1U, 0U, 0x80U, 250U,
    9U, USB_DESC_TYPE_INTERFACE, 0U, 0U, 0U, 0xFFU, 0U, 0U, 0U
};
static uint8_t s_lang_desc[4] = {4U, USB_DESC_TYPE_STRING, 0x09U, 0x04U};
static uint8_t s_string_desc[64] __attribute__((aligned(4)));
static uint8_t s_interface_alt;

//官方Core描述符回调
static uint8_t *Usb_DeviceDesc(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *Usb_LangDesc(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *Usb_Manufacturer(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *Usb_Product(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *Usb_Serial(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *Usb_ConfigString(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *Usb_InterfaceString(USBD_SpeedTypeDef speed, uint16_t *length);
//基础Class只处理接口0的标准请求
static uint8_t Usb_ClassInit(USBD_HandleTypeDef *device, uint8_t config);
static uint8_t Usb_ClassDeInit(USBD_HandleTypeDef *device, uint8_t config);
static uint8_t Usb_ClassSetup(USBD_HandleTypeDef *device, USBD_SetupReqTypedef *request);
static uint8_t *Usb_ConfigDesc(uint16_t *length);
static uint8_t *Usb_NoDesc(uint16_t *length);
//PCD事件转交官方Core
static void Usb_Setup(uint8_t *data);
static void Usb_DataOut(uint8_t ep, uint8_t *data);
static void Usb_DataIn(uint8_t ep, uint8_t *data);
static void Usb_Reset(void);
static void Usb_Suspend(void);
static void Usb_Resume(void);
static void Usb_Connect(void);
static void Usb_Disconnect(void);
//记录BSP错误并转换为USBD结果
static USBD_StatusTypeDef Usb_LlResult(SysResult_t result);
//在Application等级启动，不改Application源码
static SysResult_t Usb_AutoStart(void);

static USBD_DescriptorsTypeDef s_descriptors =
{
    Usb_DeviceDesc, Usb_LangDesc, Usb_Manufacturer, Usb_Product,
    Usb_Serial, Usb_ConfigString, Usb_InterfaceString
};
static USBD_ClassTypeDef s_base_class =
{
    .Init = Usb_ClassInit,
    .DeInit = Usb_ClassDeInit,
    .Setup = Usb_ClassSetup,
    .GetHSConfigDescriptor = Usb_NoDesc,
    .GetFSConfigDescriptor = Usb_ConfigDesc,
    .GetOtherSpeedConfigDescriptor = Usb_NoDesc,
    .GetDeviceQualifierDescriptor = Usb_NoDesc
};
static const BspUsbCallbacks_t s_callbacks =
{
    Usb_Setup, Usb_DataOut, Usb_DataIn, Usb_Reset,
    Usb_Suspend, Usb_Resume, Usb_Connect, Usb_Disconnect
};

SysResult_t USB_Device_Init(void)
{
    USBD_StatusTypeDef status;
    if (s_initialized) return SYS_OK;
    s_last_result = SYS_OK;
    status = USBD_Init(&s_device, &s_descriptors, 0U);
    if (status != USBD_OK) return s_last_result != SYS_OK ? s_last_result : SYS_ERR_IO;
    status = USBD_RegisterClass(&s_device, &s_base_class);
    if (status != USBD_OK)
    {
        (void)USBD_DeInit(&s_device);
        return SYS_ERR_IO;
    }
    s_initialized = true;
    return SYS_OK;
}
SysResult_t USB_Device_RegisterClass(USBD_ClassTypeDef *driver, USBD_DescriptorsTypeDef *descriptors)
{
    uint16_t length = 0U;
    uint8_t *config;
    if (driver == NULL || descriptors == NULL || driver->Init == NULL ||
        driver->DeInit == NULL || driver->Setup == NULL || driver->GetFSConfigDescriptor == NULL ||
        descriptors->GetDeviceDescriptor == NULL || descriptors->GetLangIDStrDescriptor == NULL ||
        descriptors->GetManufacturerStrDescriptor == NULL || descriptors->GetProductStrDescriptor == NULL ||
        descriptors->GetSerialStrDescriptor == NULL || descriptors->GetConfigurationStrDescriptor == NULL ||
        descriptors->GetInterfaceStrDescriptor == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!s_initialized || s_started) return SYS_ERR_INVALID_STATE;
    config = driver->GetFSConfigDescriptor(&length);
    if (config == NULL || length < 9U || config[0] != 9U || config[1] != USB_DESC_TYPE_CONFIGURATION)
        return SYS_ERR_INVALID_ARG;
    //单Class模式替换注册，不累计旧Class数量
    s_device.NumClasses = 0U;
    if (USBD_RegisterClass(&s_device, driver) != USBD_OK) return SYS_ERR_IO;
    s_device.pDesc = descriptors;
    return SYS_OK;
}
SysResult_t USB_Device_Start(void)
{
    USBD_StatusTypeDef status;
    if (!s_initialized) return SYS_ERR_INVALID_STATE;
    if (s_started) return SYS_ERR_BUSY;
    s_last_result = SYS_OK;
    //重新连接前等待主机重新枚举，不沿用上次CONFIGURED状态
    s_device.dev_state = USBD_STATE_DEFAULT;
    s_device.dev_config = 0U;
    status = USBD_Start(&s_device);
    if (status != USBD_OK || s_last_result != SYS_OK)
        return s_last_result != SYS_OK ? s_last_result : SYS_ERR_IO;
    s_started = true;
    return SYS_OK;
}
SysResult_t USB_Device_Stop(void)
{
    USBD_StatusTypeDef status;
    if (!s_initialized) return SYS_ERR_INVALID_STATE;
    s_last_result = SYS_OK;
    //官方Stop会忽略LL错误，本层保留真实底层结果
    status = USBD_Stop(&s_device);
    if (status != USBD_OK || s_last_result != SYS_OK)
        return s_last_result != SYS_OK ? s_last_result : SYS_ERR_IO;
    s_started = false;
    return SYS_OK;
}
bool USB_Device_IsConfigured(void)
{
    return s_started && s_last_result == SYS_OK && s_device.dev_state == USBD_STATE_CONFIGURED;
}
SysResult_t USB_Device_GetLastResult(void)
{
    return s_last_result;
}

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *device)
{
    SysResult_t result;
    if (device != &s_device) return USBD_FAIL;
    result = BSP_USB_Init();
    if (result == SYS_OK) result = BSP_USB_SetCallbacks(&s_callbacks);
    return Usb_LlResult(result);
}
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *device)
{
    if (device != &s_device) return USBD_FAIL;
    return Usb_LlResult(BSP_USB_DeInit());
}
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *device)
{
    if (device != &s_device) return USBD_FAIL;
    return Usb_LlResult(BSP_USB_Start());
}
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *device)
{
    if (device != &s_device) return USBD_FAIL;
    return Usb_LlResult(BSP_USB_Stop());
}
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *device, uint8_t ep, uint8_t type, uint16_t size)
{
    if (device != &s_device) return USBD_FAIL;
    return Usb_LlResult(BSP_USB_OpenEp(ep, type, size));
}
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    if (device != &s_device) return USBD_FAIL;
    return Usb_LlResult(BSP_USB_CloseEp(ep));
}
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    if (device != &s_device) return USBD_FAIL;
    return Usb_LlResult(BSP_USB_FlushEp(ep));
}
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    if (device != &s_device) return USBD_FAIL;
    return Usb_LlResult(BSP_USB_StallEp(ep));
}
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    if (device != &s_device) return USBD_FAIL;
    return Usb_LlResult(BSP_USB_ClearStallEp(ep));
}
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    return device == &s_device && BSP_USB_IsStallEp(ep) ? 1U : 0U;
}
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *device, uint8_t address)
{
    if (device != &s_device) return USBD_FAIL;
    return Usb_LlResult(BSP_USB_SetAddress(address));
}
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *device, uint8_t ep, uint8_t *buffer, uint32_t size)
{
    if (device != &s_device) return USBD_FAIL;
    return Usb_LlResult(BSP_USB_Transmit(ep, buffer, size));
}
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *device, uint8_t ep, uint8_t *buffer, uint32_t size)
{
    if (device != &s_device) return USBD_FAIL;
    return Usb_LlResult(BSP_USB_Receive(ep, buffer, size));
}
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *device, uint8_t ep)
{
    return device == &s_device ? BSP_USB_GetRxSize(ep) : 0U;
}
void USBD_LL_Delay(uint32_t delay)
{
    HAL_Delay(delay);
}
void *USB_Device_Alloc(uint32_t size)
{
    if (size == 0U || size > sizeof(s_class_memory) || s_class_memory_used) return NULL;
    s_class_memory_used = true;
    return s_class_memory;
}
void USB_Device_Free(void *memory)
{
    if (memory == s_class_memory) s_class_memory_used = false;
}

FRAMEWORK_INIT_REGISTER(USB_Device_Init, INIT_LEVEL_DEVICE);
FRAMEWORK_INIT_REGISTER(Usb_AutoStart, INIT_LEVEL_APPLICATION);

//内部功能函数实现
static SysResult_t Usb_AutoStart(void) { return USB_Device_Start(); }
static USBD_StatusTypeDef Usb_LlResult(SysResult_t result)
{
    if (result == SYS_OK) return USBD_OK;
    s_last_result = result;
    return result == SYS_ERR_BUSY ? USBD_BUSY : USBD_FAIL;
}
static void Usb_Setup(uint8_t *data) { (void)USBD_LL_SetupStage(&s_device, data); }
static void Usb_DataOut(uint8_t ep, uint8_t *data) { (void)USBD_LL_DataOutStage(&s_device, ep, data); }
static void Usb_DataIn(uint8_t ep, uint8_t *data) { (void)USBD_LL_DataInStage(&s_device, ep, data); }
static void Usb_Reset(void)
{
    (void)USBD_LL_SetSpeed(&s_device, USBD_SPEED_FULL);
    (void)USBD_LL_Reset(&s_device);
}
static void Usb_Suspend(void) { (void)USBD_LL_Suspend(&s_device); }
static void Usb_Resume(void) { (void)USBD_LL_Resume(&s_device); }
static void Usb_Connect(void) { (void)USBD_LL_DevConnected(&s_device); }
static void Usb_Disconnect(void) { (void)USBD_LL_DevDisconnected(&s_device); }
static uint8_t *Usb_DeviceDesc(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; *length = 18U; return s_device_desc; }
static uint8_t *Usb_LangDesc(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; *length = sizeof(s_lang_desc); return s_lang_desc; }
static uint8_t *Usb_Manufacturer(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; USBD_GetString((uint8_t *)"h7_mac", s_string_desc, length); return s_string_desc; }
static uint8_t *Usb_Product(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; USBD_GetString((uint8_t *)"h7_mac USB FS Test", s_string_desc, length); return s_string_desc; }
static uint8_t *Usb_Serial(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; USBD_GetString((uint8_t *)"H7MAC-STAGE1", s_string_desc, length); return s_string_desc; }
static uint8_t *Usb_ConfigString(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; USBD_GetString((uint8_t *)"Enumeration", s_string_desc, length); return s_string_desc; }
static uint8_t *Usb_InterfaceString(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; USBD_GetString((uint8_t *)"EP0 only", s_string_desc, length); return s_string_desc; }
static uint8_t *Usb_ConfigDesc(uint16_t *length) { *length = 18U; return s_config_desc; }
static uint8_t *Usb_NoDesc(uint16_t *length) { *length = 0U; return NULL; }
static uint8_t Usb_ClassInit(USBD_HandleTypeDef *device, uint8_t config)
{ (void)device; return config == 1U ? USBD_OK : USBD_FAIL; }
static uint8_t Usb_ClassDeInit(USBD_HandleTypeDef *device, uint8_t config)
{ (void)device; (void)config; return USBD_OK; }
static uint8_t Usb_ClassSetup(USBD_HandleTypeDef *device, USBD_SetupReqTypedef *request)
{
    static uint16_t status;
    //仅有接口0、备用设置0，没有UVC切换或厂商命令
    if ((request->bmRequest & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_STANDARD && request->wIndex == 0U)
    {
        if (request->bRequest == USB_REQ_GET_INTERFACE && request->wLength == 1U)
        {
            s_interface_alt = 0U;
            (void)USBD_CtlSendData(device, &s_interface_alt, 1U);
            return USBD_OK;
        }
        if (request->bRequest == USB_REQ_GET_STATUS && request->wLength == 2U)
        {
            status = 0U;
            (void)USBD_CtlSendData(device, (uint8_t *)&status, 2U);
            return USBD_OK;
        }
    }
    USBD_CtlError(device, request);
    return USBD_FAIL;
}
