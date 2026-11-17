//独立MSC Class：管理描述符、端点和BOT任务入口
#include "usb_msc_class.h"
#include "usb_msc_desc.h"
#include "usb_device.h"
#include "usbd_ioreq.h"
#include "usb_msc_bot.h"
#include "usb_msc_storage.h"
#include "msc_test_config.h"
#include "platform_time.h"
#include <stddef.h>

//关闭失败时保留各方向资源标记，供后续DeInit重试
static bool s_in_open;
static bool s_out_open;

//只有两个Bulk端点均已打开时，才允许处理接口请求
static bool s_configured;

//EP0异步发送使用持久缓冲，额外空间满足PIO按Word读取
static uint8_t s_reply[4] __attribute__((aligned(4)));

//SET_CONFIGURATION时打开MSC的Bulk IN与OUT端点
static uint8_t Msc_Init(USBD_HandleTypeDef *device, uint8_t config);

//逐方向关闭端点，失败时保留尚未释放的资源
static uint8_t Msc_DeInit(USBD_HandleTypeDef *device, uint8_t config);

//路由标准接口请求及两个BOT Class控制请求
static uint8_t Msc_Setup(USBD_HandleTypeDef *device, USBD_SetupReqTypedef *request);

//拒绝高速、其他速度和设备限定描述符
static uint8_t Msc_DataIn(USBD_HandleTypeDef *device, uint8_t endpoint);
static uint8_t Msc_DataOut(USBD_HandleTypeDef *device, uint8_t endpoint);
static uint8_t *Msc_NoDescriptor(uint16_t *length);

//沿用单Class注册，Bulk完成只登记BOT事件
static USBD_ClassTypeDef s_class =
{
    .Init = Msc_Init,
    .DeInit = Msc_DeInit,
    .Setup = Msc_Setup,
    .DataIn = Msc_DataIn,
    .DataOut = Msc_DataOut,
    .GetFSConfigDescriptor = USB_MSC_GetFSConfigDescriptor,
    .GetHSConfigDescriptor = Msc_NoDescriptor,
    .GetOtherSpeedConfigDescriptor = Msc_NoDescriptor,
    .GetDeviceQualifierDescriptor = Msc_NoDescriptor
};

//登记MSC枚举Class与静态描述符，不在注册时连接主机
SysResult_t USB_MSC_Register(void)
{
    return USB_Device_RegisterClass(&s_class, USB_MSC_GetDescriptors());
}

//内部功能函数实现

//SET_CONFIGURATION时打开MSC的Bulk IN与OUT端点
static uint8_t Msc_Init(USBD_HandleTypeDef *device, uint8_t config)
{
    if (device == NULL || config != 1U || s_in_open || s_out_open)
        return USBD_FAIL;
    if (USB_Device_OpenEp(USB_MSC_IN_EP, USBD_EP_TYPE_BULK, USB_MSC_PACKET_SIZE) != SYS_OK)
        return USBD_FAIL;
    s_in_open = true;

    //OUT打开失败时回收已打开的IN，回收失败仍保留其资源标记
    if (USB_Device_OpenEp(USB_MSC_OUT_EP, USBD_EP_TYPE_BULK, USB_MSC_PACKET_SIZE) != SYS_OK)
    {
        (void)Msc_DeInit(device, config);
        return USBD_FAIL;
    }
    s_out_open = true;
    if (USB_MSC_BotStart(USB_MSC_SD_TEST ? USB_MSC_StorageBackend() : USB_MSC_RamBackend()) != SYS_OK)
    {
        (void)Msc_DeInit(device, config);
        return USBD_FAIL;
    }
    s_configured = true;
    return USBD_OK;
}

//逐方向关闭端点，失败时保留尚未释放的资源
static uint8_t Msc_DeInit(USBD_HandleTypeDef *device, uint8_t config)
{
    bool failed = false;
    (void)device;
    (void)config;

    //先禁止接口请求，再分别尝试关闭两个方向，不因单方向失败而提前退出
    s_configured = false;
    USB_MSC_BotStop();
    if (s_out_open)
    {
        if (USB_Device_CloseEp(USB_MSC_OUT_EP) == SYS_OK)
            s_out_open = false;
        else
            failed = true;
    }
    if (s_in_open)
    {
        if (USB_Device_CloseEp(USB_MSC_IN_EP) == SYS_OK)
            s_in_open = false;
        else
            failed = true;
    }
    return failed ? USBD_FAIL : USBD_OK;
}

//路由标准接口请求及两个BOT Class控制请求
static uint8_t Msc_Setup(USBD_HandleTypeDef *device, USBD_SetupReqTypedef *request)
{
    if (device == NULL || request == NULL || !s_configured ||
        device->dev_state != USBD_STATE_CONFIGURED)
        return USBD_FAIL;

    //Core已清除端点HALT并发送状态，这里只确认请求，不重复发送状态包
    if (request->bmRequest == 0x02U && request->bRequest == USB_REQ_CLEAR_FEATURE &&
        request->wValue == 0U && request->wLength == 0U &&
        (request->wIndex == USB_MSC_IN_EP || request->wIndex == USB_MSC_OUT_EP))
    {
        USB_MSC_BotClearHalt((uint8_t)request->wIndex);
        return USBD_OK;
    }
    if (request->wIndex != 0U)
        return USBD_FAIL;

    if (request->bmRequest == 0xA1U && request->bRequest == 0xFEU &&
        request->wValue == 0U && request->wLength == 1U)
    {
        s_reply[0] = 0U;
        return USBD_CtlSendData(device, s_reply, 1U);
    }
    if (request->bmRequest == 0x21U && request->bRequest == 0xFFU &&
        request->wValue == 0U && request->wLength == 0U)
    {
        uint32_t token;
        if (USB_Device_DeferControlStatus(request, &token) != SYS_OK ||
            USB_MSC_BotReset(token, Platform_TimeGetMs()) != SYS_OK)
            return USBD_FAIL;
        return USBD_BUSY;
    }
    //接口0没有状态位，且只提供Alt0；发送缓冲在控制事务结束前保持有效
    s_reply[0] = 0U;
    s_reply[1] = 0U;
    if (request->bmRequest == 0x81U && request->wValue == 0U)
    {
        if (request->bRequest == USB_REQ_GET_INTERFACE && request->wLength == 1U)
            return USBD_CtlSendData(device, s_reply, 1U);
        if (request->bRequest == USB_REQ_GET_STATUS && request->wLength == 2U)
            return USBD_CtlSendData(device, s_reply, 2U);
    }
    if (request->bmRequest == 0x01U && request->bRequest == USB_REQ_SET_INTERFACE &&
        request->wValue == 0U && request->wLength == 0U)
        return USBD_OK;

    //其余请求交上层STALL
    return USBD_FAIL;
}

//拒绝高速、其他速度和设备限定描述符
static uint8_t *Msc_NoDescriptor(uint16_t *length)
{
    if (length != NULL)
        *length = 0U;
    return NULL;
}

static uint8_t Msc_DataIn(USBD_HandleTypeDef *device, uint8_t endpoint)
{
    (void)device;
    if (!s_configured || endpoint != (USB_MSC_IN_EP & 0x7FU)) return USBD_FAIL;
    USB_MSC_BotDataIn();
    return USBD_OK;
}
static uint8_t Msc_DataOut(USBD_HandleTypeDef *device, uint8_t endpoint)
{
    (void)device;
    if (!s_configured || endpoint != USB_MSC_OUT_EP) return USBD_FAIL;
    USB_MSC_BotDataOut(USB_Device_GetRxSize(USB_MSC_OUT_EP));
    return USBD_OK;
}
void USB_MSC_Process(uint32_t now_ms)
{
    USB_MSC_BotProcess(now_ms);
}
