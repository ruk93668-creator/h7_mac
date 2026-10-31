//复用真实固定内存池及已有HAL桩
#include "../UsbCoreDecoupling/core_test.c"
#include "../../CM7/Service/usb_uvc_class.c"

//补齐无标准库测试链接所需的ARM运行时桩
void __aeabi_memset(void *dest, size_t size, int value)
{
    volatile uint8_t *p = dest;
    while (size--) *p++ = (uint8_t)value;
}

//验证接口请求与Core状态阶段
static int UvcSetup_TestCase(uint32_t id);

//逐项验证UVC上下文申请和归还
int UvcLifecycle_TestRun(uint32_t id)
{
    USBD_HandleTypeDef device;
    UsbUvcContext_t *ctx;
    uint32_t i;
    memset(&device, 0, sizeof(device));
    if (id == 0U) { EQ(UVC_Init(NULL, 1U), USBD_FAIL); EQ(s_class_memory_used, false); }
    else if (id == 1U || id == 2U)
    {
        EQ(UVC_Init(&device, id == 1U ? 0U : 2U), USBD_FAIL);
        EQ(device.pUserData[0], NULL); EQ(s_class_memory_used, false);
    }
    else if (id == 3U)
    {
        EQ(UVC_Init(&device, 1U), USBD_OK); ctx = device.pUserData[0];
        ctx->interface_alt[1] = 1U;
        EQ(UVC_Init(&device, 1U), USBD_FAIL);
        EQ(device.pUserData[0], ctx); EQ(ctx->interface_alt[1], 1U);
    }
    else if (id == 4U)
    {
        EQ(USB_Device_Alloc(1U), s_class_memory); s_class_memory[0] = 0xA5U;
        EQ(UVC_Init(&device, 1U), USBD_EMEM); EQ(device.pUserData[0], NULL);
        EQ(s_class_memory_used, true); EQ(s_class_memory[0], 0xA5U);
    }
    else if (id == 5U)
    {
        memset(s_class_memory, 0xA5, sizeof(s_class_memory));
        EQ(UVC_Init(&device, 1U), USBD_OK); ctx = device.pUserData[0];
        EQ(ctx, s_class_memory); EQ(s_class_memory_used, true);
        EQ(ctx->interface_alt[0], 0U); EQ(ctx->interface_alt[1], 0U);
        EQ(ctx->configured, true);
        for (i = 0U; i < sizeof(*ctx); i++)
            EQ(((uint8_t *)ctx)[i], i == offsetof(UsbUvcContext_t, configured) ? 1U : 0U);
    }
    else if (id == 6U) { EQ(UVC_DeInit(NULL, 1U), USBD_FAIL); EQ(s_class_memory_used, false); }
    else if (id == 7U) { EQ(UVC_DeInit(&device, 1U), USBD_OK); EQ(s_class_memory_used, false); }
    else if (id == 8U)
    {
        EQ(UVC_Init(&device, 1U), USBD_OK); EQ(UVC_DeInit(&device, 1U), USBD_OK);
        EQ(device.pUserData[0], NULL); EQ(s_class_memory_used, false);
        EQ(UVC_DeInit(&device, 1U), USBD_OK);
    }
    else if (id == 9U)
    {
        EQ(UVC_Init(&device, 1U), USBD_OK); s_class_state = USB_CLASS_DEINITIALIZING;
        EQ(UVC_DeInit(&device, 1U), USBD_OK); EQ(s_class_free_pending, true);
        EQ(s_class_memory_used, true); EQ(device.pUserData[0], NULL);
    }
    else if (id == 10U)
    {
        EQ(UVC_Init(&device, 1U), USBD_OK); EQ(UVC_DeInit(&device, 1U), USBD_OK);
        EQ(UVC_Init(&device, 1U), USBD_OK); EQ(s_class_memory_used, true);
    }
    else if (id == 11U)
    {
        uint16_t length = 0U;
        EQ(s_uvc_class.Init, UVC_Init); EQ(s_uvc_class.DeInit, UVC_DeInit);
        EQ(s_uvc_class.Setup, UVC_Setup); EQ(s_uvc_class.EP0_TxSent, NULL);
        EQ(s_uvc_class.EP0_RxReady, NULL); EQ(s_uvc_class.DataIn, NULL);
        EQ(s_uvc_class.DataOut, NULL); EQ(s_uvc_class.SOF, NULL);
        EQ(s_uvc_class.IsoINIncomplete, NULL); EQ(s_uvc_class.IsoOUTIncomplete, NULL);
        EQ(s_uvc_class.GetHSConfigDescriptor, NULL);
        EQ(s_uvc_class.GetFSConfigDescriptor, USB_UVC_GetFSConfigDescriptor);
        EQ(s_uvc_class.GetOtherSpeedConfigDescriptor, NULL);
        EQ(s_uvc_class.GetDeviceQualifierDescriptor, NULL);
        EQ(s_uvc_class.GetFSConfigDescriptor(&length) != NULL, true);
        EQ(length, USB_UVC_FS_CONFIG_DESC_SIZE);
        EQ(s_uvc_class.Setup(NULL, NULL), USBD_FAIL);
    }
    else if (id >= 12U && id <= 39U) return UvcSetup_TestCase(id);
    else { EQ(0U, 1U); }
    return 0;
}

//直接验证非法请求，再用真实Core验证控制传输
static int UvcSetup_TestCase(uint32_t id)
{
    USBD_HandleTypeDef device;
    USBD_SetupReqTypedef req;
    UsbUvcContext_t *ctx;
    UsbUvcContext_t saved;
    uint32_t opens;
    memset(&device, 0, sizeof(device));
    memset(&req, 0, sizeof(req));

    if (id >= 35U)
    {
        //仅用现有字符串桩补齐测试描述符，不改生产注册代码
        USBD_DescriptorsTypeDef descriptors = s_descriptors;
        descriptors.GetDeviceDescriptor = USB_UVC_GetDeviceDescriptor;

        //沿用已有USB回归的就绪时钟现场
        __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);
        RCC->CR |= RCC_CR_HSI48RDY;
        EQ(USB_Device_Init(), SYS_OK);
        EQ(USB_Device_RegisterClass(&s_uvc_class, &descriptors), SYS_OK);
        EQ(USB_Device_Start(), SYS_OK);
        HAL_PCD_ResetCallback(&s_pcd);
        Configured();
        EQ(USB_Device_IsConfigured(), true);
        ctx = s_device.pUserData[0];
        EQ(ctx != NULL, true);
        opens = open_count;
        if (id == 35U || id == 36U)
        {
            uint16_t index = id == 35U ? 0U : 1U;
            Request(0x81U, USB_REQ_GET_INTERFACE, 0U, index, 1U);
            EQ(tx_size, 1U); EQ(tx_data[0], 0U);
            EQ(s_device.ep0_state, USBD_EP0_DATA_IN);
            EQ(s_tx_transfer[0].source, &ctx->interface_alt[index]);
            EQ(stall_count, 0U);
        }
        else if (id == 37U || id == 38U)
        {
            Request(0x01U, USB_REQ_SET_INTERFACE, 0U, id == 37U ? 0U : 1U, 0U);
            EQ(tx_size, 0U); EQ(s_device.ep0_state, USBD_EP0_STATUS_IN);
            EQ(USB_Device_GetLastOperationResult(), SYS_OK);
            EQ(s_endpoints[1][0].busy, true); EQ(stall_count, 0U);
            HAL_PCD_DataInStageCallback(&s_pcd, 0U);
            EQ(s_endpoints[1][0].busy, false);
        }
        else
        {
            Request(0x01U, USB_REQ_SET_INTERFACE, 1U, 1U, 0U);
            EQ(stall_count > 0U, true); EQ(tx_size, 999U);
            EQ(ctx->interface_alt[1], 0U);
        }
        EQ(open_count, opens); EQ(ctx->ep1_opened, false);
        EQ(ctx->streaming, false); EQ(s_pcd.IN_ep[1].maxpacket, 0U);
        return 0;
    }

    device.dev_state = USBD_STATE_CONFIGURED;
    req.bmRequest = 0x81U; req.bRequest = USB_REQ_GET_INTERFACE; req.wLength = 1U;
    if (id != 14U) EQ(UVC_Init(&device, 1U), USBD_OK);
    if (id == 12U) { EQ(UVC_Setup(NULL, &req), USBD_FAIL); return 0; }
    if (id == 13U) { EQ(UVC_Setup(&device, NULL), USBD_FAIL); return 0; }
    if (id == 14U) { EQ(UVC_Setup(&device, &req), USBD_FAIL); return 0; }
    ctx = device.pUserData[0];
    if (id == 15U) ctx->configured = false;
    if (id == 16U) device.dev_state = USBD_STATE_DEFAULT;
    if (id == 17U) req.bmRequest = 0xA1U;
    if (id == 18U) req.bmRequest = 0xC1U;
    if (id == 19U) req.bmRequest = 0x80U;
    if (id == 20U) req.bmRequest = 0x82U;
    if (id == 21U) req.bmRequest = 0x85U;
    if (id == 22U) req.wIndex = 2U;
    if (id == 23U) req.wIndex = 0x100U;
    if (id == 24U) req.bmRequest = 0x01U;
    if (id == 25U) req.wValue = 1U;
    if (id == 26U) req.wLength = 0U;
    if (id == 27U) req.wLength = 2U;
    if (id >= 28U && id <= 31U)
    {
        req.bmRequest = 0x01U; req.bRequest = USB_REQ_SET_INTERFACE; req.wLength = 0U;
        if (id == 28U) req.wValue = 1U;
        if (id == 29U) req.wValue = 0x100U;
        if (id == 30U) req.bmRequest = 0x81U;
        if (id == 31U) req.wLength = 1U;
    }
    if (id == 32U) req.bRequest = USB_REQ_GET_STATUS;
    if (id == 33U || id == 34U)
    {
        uint16_t index = id == 33U ? 0U : 1U;
        req.bmRequest = 0x01U; req.bRequest = USB_REQ_SET_INTERFACE;
        req.wIndex = index; req.wLength = 0U;
        ctx->interface_alt[index] = 7U;
        ctx->interface_alt[1U-index] = 3U;
        tx_size = 999U;
        EQ(UVC_Setup(&device, &req), USBD_OK);
        EQ(ctx->interface_alt[index], 0U); EQ(ctx->interface_alt[1U-index], 3U);
        EQ(device.ep0_state, USBD_EP0_IDLE); EQ(tx_size, 999U);
        EQ(open_count, 0U); EQ(ctx->ep1_opened, false); EQ(ctx->streaming, false);
        return 0;
    }
    saved = *ctx;
    tx_size = 999U;
    EQ(UVC_Setup(&device, &req), USBD_FAIL);
    EQ(memcmp(ctx, &saved, sizeof(saved)), 0);
    EQ(tx_size, 999U); EQ(open_count, 0U);
    return 0;
}
