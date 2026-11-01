//使用真实Class、Platform、BSP、ST Core，仅硬件HAL沿用原有桩
#include "../UsbCoreDecoupling/core_test.c"
#include "../../CM7/Service/usb_uvc_class.c"

//模拟器区分测试主动制造的busy事务与Class主动发送
volatile uint32_t g_fixture_tx;
static uint8_t s_busy_buffer[32] __attribute__((aligned(4)));
static uint8_t s_negotiated[34];

//完成真实EP0状态阶段
static int Ep1_Status(void);

//经过真实EP0执行Probe和Commit
static int Ep1_Commit(void);

//切换到Alt 1并完成状态阶段
static int Ep1_Open(void);

//验证请求失败STALL且没有成功状态包
static int Ep1_Stalled(void);

//仅测试夹具提交一次模拟传输，制造真实busy/PIO占用
static int Ep1_MakeBusy(void);

//通过真实GET_INTERFACE查询当前Alt
static int Ep1_CheckAlt(uint8_t alternate);

void __aeabi_memset(void *dest, size_t size, int value)
{
    volatile uint8_t *p = dest;
    while (size--) *p++ = (uint8_t)value;
}

//每项独立加载ARM内存，故障编号来自未修改的原HAL桩
int UvcEp1_TestRun(uint32_t id)
{
    UsbUvcContext_t *ctx;
    UsbDeviceEndpointState_t ep;
    uint32_t opens;
    uint32_t closes;
    uint32_t aborts;
    uint32_t offset;
    uint32_t interface_number = 0xFFU;
    uint32_t alternate = 0xFFU;
    uint32_t found = 0U;
    uint16_t length;
    uint8_t *desc;
    uint8_t saved_commit[34];

    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);
    RCC->CR |= RCC_CR_HSI48RDY;
    EQ(USB_Device_Init(), SYS_OK);
    EQ(USB_UVC_Register(), SYS_OK);
    EQ(USB_Device_Start(), SYS_OK);
    HAL_PCD_ResetCallback(&s_pcd);
    Configured();
    ctx = s_device.pUserData[0]; EQ(ctx != NULL, true);
    if (id != 0U) EQ(Ep1_Commit(), 0U);

    if (id == 0U)
    {
        opens = open_count;
        Request(0x01U, USB_REQ_SET_INTERFACE, 1U, 1U, 0U);
        EQ(Ep1_Stalled(), 0U); EQ(open_count, opens);
        EQ(ctx->interface_alt[1], 0U); EQ(ctx->ep1_opened, false);
    }
    else if (id == 1U || id == 2U)
    {
        EQ(Ep1_CheckAlt(0U), 0U); EQ(Ep1_Open(), 0U);
        EQ(USB_Device_GetEpState(0x81U, &ep), SYS_OK);
        EQ(ep.opened, true); EQ(ep.busy, false); EQ(ep.type, USBD_EP_TYPE_ISOC); EQ(ep.max_packet, 512U);
        EQ(s_pcd.IN_ep[1].maxpacket, 512U); EQ(s_pcd.IN_ep[1].type, EP_TYPE_ISOC);
        if (id == 2U)
        {
            desc = USB_UVC_GetFSConfigDescriptor(&length);
            for (offset = 0U; offset < length; offset += desc[offset])
            {
                if (desc[offset + 1U] == USB_DESC_TYPE_INTERFACE)
                { interface_number = desc[offset + 2U]; alternate = desc[offset + 3U]; }
                if (desc[offset + 1U] == USB_DESC_TYPE_ENDPOINT && interface_number == 1U && alternate == 1U)
                {
                    EQ(desc[offset + 2U], 0x81U); EQ(desc[offset + 3U] & 3U, ep.type);
                    EQ((uint16_t)desc[offset + 4U] | ((uint16_t)desc[offset + 5U] << 8), ep.max_packet);
                    found++;
                }
            }
            EQ(found, 1U);
        }
        Request(0x01U, USB_REQ_SET_INTERFACE, 0U, 1U, 0U); EQ(Ep1_Status(), 0U);
        EQ(ctx->interface_alt[1], 0U); EQ(ctx->ep1_opened, false); EQ(ctx->ep1_close_pending, false);
        EQ(USB_Device_GetEpState(0x81U, &ep), SYS_OK); EQ(ep.opened, false); EQ(ep.busy, false);
        EQ(s_pcd.IN_ep[1].maxpacket, 0U);
    }
    else if (id == 3U || id == 4U || id == 5U)
    {
        if (id != 4U) EQ(Ep1_Open(), 0U);
        opens = open_count; closes = close_count; aborts = abort_count;
        Request(0x01U, USB_REQ_SET_INTERFACE, id == 3U ? 1U : 0U, id == 5U ? 0U : 1U, 0U);
        EQ(Ep1_Status(), 0U); EQ(open_count, opens); EQ(close_count, closes); EQ(abort_count, aborts);
        EQ(ctx->interface_alt[1], id == 4U ? 0U : 1U); EQ(ctx->ep1_opened, id != 4U);
    }
    else if (id >= 6U && id <= 12U)
    {
        opens = open_count;
        Request(id == 11U ? 0x81U : 0x01U, USB_REQ_SET_INTERFACE,
            id == 7U ? 2U : id == 8U ? 0x0101U : 1U,
            id == 6U ? 0U : id == 9U ? 2U : id == 10U ? 0x0101U : 1U,
            id == 12U ? 1U : 0U);
        EQ(Ep1_Stalled(), 0U); EQ(open_count, opens); EQ(ctx->ep1_opened, false);
        EQ(ctx->interface_alt[1], 0U); EQ(ctx->commit_valid, true);
    }
    else if (id == 13U || id == 14U)
    {
        fault = id == 13U ? 13U : 19U;
        Request(0x01U, USB_REQ_SET_INTERFACE, 1U, 1U, 0U);
        EQ(Ep1_Stalled(), 0U); EQ(ctx->ep1_opened, false); EQ(ctx->interface_alt[1], 0U);
        EQ(USB_Device_GetEpState(0x81U, &ep), SYS_OK); EQ(ep.opened, false); EQ(ep.busy, false);
        EQ(s_pcd.IN_ep[1].maxpacket, 0U); EQ(ctx->commit_valid, true);
        fault = 0U; EQ(Ep1_Open(), 0U);
    }
    else if (id >= 15U && id <= 18U)
    {
        EQ(Ep1_Open(), 0U);
        if (id != 15U) EQ(Ep1_MakeBusy(), 0U);
        fault = id == 16U ? 20U : id == 17U ? 15U : 14U;
        Request(0x01U, USB_REQ_SET_INTERFACE, 0U, 1U, 0U);
        EQ(Ep1_Stalled(), 0U); EQ(ctx->ep1_opened, true); EQ(ctx->interface_alt[1], 1U);
        EQ(ctx->ep1_close_pending, true); EQ(USB_Device_GetEpState(0x81U, &ep), SYS_OK); EQ(ep.opened, true);
        EQ(ep.busy, id == 16U || id == 17U);
        EQ(s_tx_transfer[1].active, id == 16U || id == 17U);
        if (id == 16U || id == 17U) EQ(s_tx_transfer[1].source, s_busy_buffer);
        fault = 0U;
        Request(0x01U, USB_REQ_SET_INTERFACE, 0U, 1U, 0U); EQ(Ep1_Status(), 0U);
        EQ(ctx->ep1_opened, false); EQ(ctx->interface_alt[1], 0U); EQ(ctx->ep1_close_pending, false);
        EQ(s_endpoints[1][1].busy, false); EQ(s_tx_transfer[1].active, false);
        EQ(s_tx_transfer[1].source, NULL); EQ(Ep1_Open(), 0U);
    }
    else if (id >= 19U && id <= 21U)
    {
        EQ(Ep1_Open(), 0U);
        if (id != 19U) { EQ(Ep1_MakeBusy(), 0U); fault = id == 20U ? 20U : 14U; }
        EQ(USBD_ClrClassConfig(&s_device, 1U), id == 19U ? USBD_OK : USBD_FAIL);
        if (id != 19U)
        {
            EQ(s_device.pUserData[0], ctx); EQ(s_class_memory_used, true);
            EQ(ctx->ep1_opened, true); EQ(ctx->interface_alt[1], 1U); EQ(ctx->ep1_close_pending, true);
            EQ(s_class_state, USB_CLASS_RELEASE_FAILED);
            EQ(s_endpoints[1][1].busy, id == 20U); EQ(s_tx_transfer[1].active, id == 20U);
            fault = 0U; EQ(USBD_ClrClassConfig(&s_device, 1U), USBD_OK);
        }
        EQ(s_device.pUserData[0], NULL); EQ(s_class_memory_used, false); EQ(s_endpoints[1][1].opened, false);
    }
    else if (id >= 22U && id <= 24U)
    {
        EQ(Ep1_Open(), 0U);
        if (id != 22U) { EQ(Ep1_MakeBusy(), 0U); fault = id == 23U ? 20U : 14U; }
        HAL_PCD_ResetCallback(&s_pcd);
        if (id == 22U)
        {
            EQ(s_device.pUserData[0], NULL); EQ(s_class_memory_used, false);
            EQ(s_endpoints[1][1].opened, false); EQ(s_endpoints[0][0].opened, true); EQ(s_endpoints[1][0].opened, true);
            Configured(); ctx = s_device.pUserData[0]; EQ(ctx != NULL, true); EQ(ctx->commit_valid, false);
            Request(0x01U, USB_REQ_SET_INTERFACE, 1U, 1U, 0U); EQ(Ep1_Stalled(), 0U);
        }
        else
        {
            EQ(s_device.pUserData[0], ctx); EQ(s_class_memory_used, true); EQ(s_stop_required, true);
            EQ(s_tx_transfer[1].active, id == 23U);
            fault = 0U; EQ(USB_Device_Stop(), SYS_OK);
            EQ(s_device.pUserData[0], NULL); EQ(s_class_memory_used, false); EQ(s_endpoints[1][1].opened, false);
        }
    }
    else if (id == 25U)
    {
        EQ(Ep1_Open(), 0U); EQ(Ep1_MakeBusy(), 0U);
        closes = close_count; aborts = abort_count; opens = open_count;
        EQ(Ep1_Open(), 0U); EQ(close_count, closes); EQ(abort_count, aborts); EQ(open_count, opens);
        EQ(s_tx_transfer[1].source, s_busy_buffer); EQ(s_tx_transfer[1].active, true); EQ(s_endpoints[1][1].busy, true);
    }
    else if (id == 26U || id == 27U)
    {
        EQ(Ep1_Open(), 0U); memcpy(saved_commit, ctx->commit, 34U);
        if (id == 26U)
        {
            Request(0x21U, 1U, 0x0100U, 1U, 34U);
            memset(s_pcd.OUT_ep[0].xfer_buff, 0, 34U);
            HAL_PCD_DataOutStageCallback(&s_pcd, 0U); EQ(Ep1_Status(), 0U);
            EQ(ctx->commit_valid, true); EQ(memcmp(saved_commit, ctx->commit, 34U), 0);
            EQ(ctx->ep1_opened, true); EQ(ctx->interface_alt[1], 1U);
        }
        Request(0x01U, USB_REQ_SET_INTERFACE, 0U, 1U, 0U); EQ(Ep1_Status(), 0U);
        EQ(ctx->commit_valid, true); EQ(memcmp(saved_commit, ctx->commit, 34U), 0); EQ(Ep1_Open(), 0U);
    }
    else if (id == 28U)
    {
        EQ(Ep1_CheckAlt(0U), 0U); EQ(Ep1_Open(), 0U); EQ(Ep1_CheckAlt(1U), 0U);
        Request(0x01U, USB_REQ_SET_INTERFACE, 0U, 1U, 0U); EQ(Ep1_Status(), 0U); EQ(Ep1_CheckAlt(0U), 0U);
    }
    else if (id == 29U || id == 30U)
    {
        if (id == 30U) EQ(Ep1_Open(), 0U);
        fault = id == 29U ? 13U : 14U;
        Request(0x01U, USB_REQ_SET_INTERFACE, id == 29U ? 1U : 0U, 1U, 0U); EQ(Ep1_Stalled(), 0U);
        fault = 0U;
        if (id == 29U) Request(0x80U, USB_REQ_GET_DESCRIPTOR, 0x0100U, 0U, 18U);
        else Request(0x81U, USB_REQ_GET_STATUS, 0U, 1U, 2U);
        EQ(stall_count, 0U); EQ(tx_size, id == 29U ? 18U : 2U);
    }
    else if (id == 31U)
    {
        fault = 13U; Request(0x01U, USB_REQ_SET_INTERFACE, 1U, 1U, 0U); EQ(Ep1_Stalled(), 0U);
        USBD_CtlSendStatus(&s_device);
        EQ(tx_size, 999U); EQ(s_endpoints[1][0].busy, false); EQ(s_tx_transfer[0].active, false);
        EQ(s_device.ep0_state, USBD_EP0_STALL);
    }
    else if (id == 32U || id == 40U)
    {
        if (id == 32U) EQ(Ep1_Open(), 0U);
        EQ(USBD_ClrClassConfig(&s_device, 1U), USBD_OK);
        closes = close_count; aborts = abort_count;
        EQ(USBD_ClrClassConfig(&s_device, 1U), USBD_OK);
        EQ(close_count, closes); EQ(abort_count, aborts); EQ(s_class_memory_used, false);
    }
    else if (id >= 33U && id <= 35U)
    {
        EQ(Ep1_Open(), 0U);
        if (id != 33U) { EQ(Ep1_MakeBusy(), 0U); fault = id == 34U ? 20U : 14U; }
        Request(0x00U, USB_REQ_SET_CONFIGURATION, 0U, 0U, 0U);
        if (id == 33U)
        {
            EQ(Ep1_Status(), 0U); EQ(s_device.dev_state, USBD_STATE_ADDRESSED);
            EQ(s_class_memory_used, false); EQ(s_device.pUserData[0], NULL); EQ(s_endpoints[1][1].opened, false);
        }
        else
        {
            EQ(Ep1_Stalled(), 0U);
            //停止故障时Core仍写STATUS_IN，但LL必须拒绝，硬件必须保持STALL
            EQ(s_device.ep0_state, USBD_EP0_STATUS_IN);
            EQ(s_pcd.IN_ep[0].is_stall, 1U); EQ(s_pcd.OUT_ep[0].is_stall, 1U);
            EQ(s_class_memory_used, true); EQ(s_device.pUserData[0], ctx); EQ(ctx->ep1_opened, true);
            EQ(s_endpoints[1][1].busy, id == 34U); EQ(s_tx_transfer[1].active, id == 34U);
            fault = 0U; EQ(USB_Device_Stop(), SYS_OK); EQ(s_class_memory_used, false);
        }
    }
    else if (id == 36U || id == 37U)
    {
        EQ(Ep1_Open(), 0U);
        if (id == 37U)
        {
            EQ(Ep1_MakeBusy(), 0U); fault = 8U; EQ(USB_Device_Stop(), SYS_ERR_IO);
            EQ(s_class_memory_used, true); EQ(ctx->ep1_opened, true); EQ(s_device.pUserData[0], ctx);
            EQ(s_endpoints[1][1].busy, true); EQ(s_tx_transfer[1].active, true); fault = 0U;
        }
        EQ(USB_Device_Stop(), SYS_OK); EQ(s_class_memory_used, false); EQ(s_device.pUserData[0], NULL);
        EQ(USB_Device_Start(), SYS_OK); HAL_PCD_ResetCallback(&s_pcd); Configured();
        ctx = s_device.pUserData[0]; EQ(ctx != NULL, true); EQ(ctx->commit_valid, false); EQ(ctx->interface_alt[1], 0U);
    }
    else if (id == 38U)
    {
        EQ(Ep1_Open(), 0U); EQ(Ep1_MakeBusy(), 0U);
        HAL_PCD_DataInStageCallback(&s_pcd, 1U);
        EQ(s_endpoints[1][1].busy, false); EQ(s_tx_transfer[1].active, false);
        EQ(ctx->streaming, false); EQ(ctx->ep1_opened, true); EQ(ctx->interface_alt[1], 1U);
    }
    else if (id == 39U)
    {
        EQ(Ep1_Open(), 0U); HAL_PCD_DisconnectCallback(&s_pcd);
        EQ(s_class_memory_used, false); EQ(s_device.pUserData[0], NULL); EQ(s_endpoints[1][1].opened, false);
    }
    else if (id == 41U)
    {
        EQ(Ep1_Open(), 0U); fault = 14U;
        Request(0x01U, USB_REQ_SET_INTERFACE, 0U, 1U, 0U); EQ(Ep1_Stalled(), 0U); fault = 0U;
        opens = open_count;
        Request(0x01U, USB_REQ_SET_INTERFACE, 1U, 1U, 0U); EQ(Ep1_Stalled(), 0U); EQ(open_count, opens);
        EQ(ctx->ep1_opened, true); EQ(ctx->interface_alt[1], 1U);
        Request(0x01U, USB_REQ_SET_INTERFACE, 0U, 1U, 0U); EQ(Ep1_Status(), 0U); EQ(Ep1_Open(), 0U);
    }
    else if (id == 42U)
    {
        EQ(Ep1_Open(), 0U); opens = open_count; closes = close_count;
        Request(0x01U, USB_REQ_SET_INTERFACE, 0U, 1U, 1U); EQ(Ep1_Stalled(), 0U);
        EQ(ctx->ep1_opened, true); EQ(ctx->interface_alt[1], 1U); EQ(open_count, opens); EQ(close_count, closes);
    }
    else if (id == 43U || id == 44U)
    {
        EQ(Ep1_Open(), 0U); memcpy(saved_commit, ctx->commit, 34U);
        Request(0x21U, 1U, 0x0100U, 1U, 34U);
        memset(s_pcd.OUT_ep[0].xfer_buff, 0, 34U);
        if (id == 43U) UVC_Write32(s_pcd.OUT_ep[0].xfer_buff + 4, 2000000U);
        HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
        if (id == 43U) EQ(Ep1_Stalled(), 0U);
        else
        {
            EQ(Ep1_Status(), 0U);
            Request(0x21U, 1U, 0x0200U, 1U, 34U);
            memcpy(s_pcd.OUT_ep[0].xfer_buff, s_negotiated, 34U);
            HAL_PCD_DataOutStageCallback(&s_pcd, 0U); EQ(Ep1_Stalled(), 0U);
        }
        EQ(ctx->commit_valid, true); EQ(memcmp(saved_commit, ctx->commit, 34U), 0);
        EQ(ctx->ep1_opened, true); EQ(ctx->interface_alt[1], 1U); EQ(Ep1_Open(), 0U);
    }
    else EQ(0U, 1U);
    if (s_device.pUserData[0] != NULL) EQ(((UsbUvcContext_t *)s_device.pUserData[0])->streaming, false);
    EQ(s_uvc_class.DataIn, UVC_DataIn); EQ(s_uvc_class.SOF, UVC_Sof);
    return 0;
}

//成功SET_INTERFACE仅由Core发送一次状态阶段
static int Ep1_Status(void)
{
    EQ(tx_size, 0U); EQ(stall_count, 0U); EQ(s_device.ep0_state, USBD_EP0_STATUS_IN);
    HAL_PCD_DataInStageCallback(&s_pcd, 0U);
    EQ(s_endpoints[1][0].busy, false); EQ(s_tx_transfer[0].active, false);
    return 0;
}

//真实协商结果必须经过完整GET_CUR后才能Commit
static int Ep1_Commit(void)
{
    UsbUvcContext_t *ctx = s_device.pUserData[0];
    Request(0x21U, 1U, 0x0100U, 1U, 34U);
    memset(s_pcd.OUT_ep[0].xfer_buff, 0, 34U);
    s_pcd.OUT_ep[0].xfer_count = 34U;
    HAL_PCD_DataOutStageCallback(&s_pcd, 0U); EQ(Ep1_Status(), 0U);
    Request(0xA1U, 0x81U, 0x0100U, 1U, 34U);
    EQ(tx_size, 34U); memcpy(s_negotiated, tx_data, 34U);
    HAL_PCD_DataInStageCallback(&s_pcd, 0U); HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
    EQ(ctx->probe_observed, true);
    Request(0x21U, 1U, 0x0200U, 1U, 34U);
    memcpy(s_pcd.OUT_ep[0].xfer_buff, s_negotiated, 34U);
    s_pcd.OUT_ep[0].xfer_count = 34U;
    HAL_PCD_DataOutStageCallback(&s_pcd, 0U); EQ(Ep1_Status(), 0U);
    EQ(ctx->commit_valid, true);
    return 0;
}

//打开成功后状态标志才能提交为Alt 1
static int Ep1_Open(void)
{
    UsbUvcContext_t *ctx = s_device.pUserData[0];
    Request(0x01U, USB_REQ_SET_INTERFACE, 1U, 1U, 0U);
    EQ(Ep1_Status(), 0U); EQ(ctx->interface_alt[1], 1U); EQ(ctx->ep1_opened, true);
    EQ(ctx->streaming, false); EQ(ctx->ep1_close_pending, false);
    return 0;
}

//失败请求必须STALL，且不能提交ZLP或占有EP0发送缓冲区
static int Ep1_Stalled(void)
{
    EQ(tx_size, 999U); EQ(stall_count > 0U, true);
    EQ(s_endpoints[0][0].stalled, true); EQ(s_endpoints[1][0].stalled, true);
    EQ(s_endpoints[1][0].busy, false); EQ(s_tx_transfer[0].active, false);
    return 0;
}

//此模拟缓冲区只用于故障验收，不是视频数据或业务发送逻辑
static int Ep1_MakeBusy(void)
{
    SysResult_t result;
    g_fixture_tx = 1U;
    result = USB_Device_Transmit(0x81U, s_busy_buffer, sizeof(s_busy_buffer));
    g_fixture_tx = 0U;
    EQ(result, SYS_OK); EQ(s_endpoints[1][1].busy, true); EQ(s_tx_transfer[1].active, true);
    EQ(s_tx_transfer[1].source, s_busy_buffer);
    return 0;
}

//检查Core按当前Alt返回真实接口状态
static int Ep1_CheckAlt(uint8_t alternate)
{
    Request(0x81U, USB_REQ_GET_INTERFACE, 0U, 1U, 1U);
    EQ(tx_size, 1U); EQ(tx_data[0], alternate); EQ(stall_count, 0U);
    HAL_PCD_DataInStageCallback(&s_pcd, 0U); HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
    return 0;
}
