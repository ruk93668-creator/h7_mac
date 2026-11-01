//真实Core处理SETUP，HAL硬件沿用原有测试桩
#include "../UsbCoreDecoupling/core_test.c"
#include "../../CM7/Service/usb_uvc_class.c"

void __aeabi_memset(void *dest, size_t size, int value)
{
    volatile uint8_t *p = dest;
    while (size--) *p++ = (uint8_t)value;
}

//每个用例重新加载ARM内存，不继承上一用例状态
int UvcCancellation_TestRun(uint32_t id)
{
    UsbUvcContext_t *ctx;
    UvcStreamingControl_t control;
    USBD_SetupReqTypedef req;
    uint8_t data[34];
    uint8_t saved_probe[34];
    uint8_t saved_commit[34];
    uint32_t flushes;
    uint32_t stalls;
    uint32_t i;
    uint32_t length;

    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);
    RCC->CR |= RCC_CR_HSI48RDY;
    EQ(USB_Device_Init(), SYS_OK);
    EQ(USB_UVC_Register(), SYS_OK);
    EQ(USB_Device_Start(), SYS_OK);
    HAL_PCD_ResetCallback(&s_pcd);
    Configured();
    ctx = s_device.pUserData[0];
    EQ(ctx != NULL, true);
    UVC_DefaultControl(&control);
    UVC_EncodeControl(&control, data);

    if (id == 0U)
    {
        EQ(UVC_ProbeSet(ctx, data), true);
        Request(0xA1U, 0x81U, 0x0100U, 1U, 34U);
        EQ(ctx->probe_reply_pending, true); EQ(ctx->probe_observed, false);

        //不触发旧IN完成，直接由新SETUP取消旧传输
        Request(0x80U, USB_REQ_GET_DESCRIPTOR, 0x0100U, 0U, 18U);
        EQ(s_device.request.bmRequest, 0x80U); EQ(tx_size, 18U);
        HAL_PCD_DataInStageCallback(&s_pcd, 0U);
        EQ(ctx->probe_observed, false); EQ(ctx->probe_reply_pending, false);
        EQ(UVC_CommitSet(ctx, data), false); EQ(ctx->commit_valid, false);
    }
    else if (id == 1U || id == 2U || id == 3U)
    {
        EQ(UVC_ProbeSet(ctx, data), true); ctx->probe_observed = true;
        memcpy(saved_probe, ctx->probe, 34U); memcpy(saved_commit, ctx->commit, 34U);
        Request(0x21U, 1U, id == 2U ? 0x0200U : 0x0100U, 1U, 34U);
        memcpy(ctx->control_rx, data, 34U);
        flushes = flush_by_ep[0][0];
        if (id == 3U)
        {
            //设备SET_DESCRIPTOR不受当前Core支持，但仍是新的设备OUT SETUP
            Request(0x00U, USB_REQ_SET_DESCRIPTOR, 0x0301U, 0U, 34U);
            EQ(stall_count > 0U, true);
        }
        else Request(0x80U, USB_REQ_GET_DESCRIPTOR, 0x0100U, 0U, 18U);
        length = s_device.ep0_state; stalls = stall_count;

        //额外注入迟到的Class回调，不能解码旧control_rx或STALL新请求
        EQ(UVC_EP0_RxReady(&s_device), USBD_FAIL);
        EQ(ctx->pending_selector, 0U); EQ(ctx->probe_observed, true);
        EQ(ctx->commit_valid, false); EQ(memcmp(saved_probe, ctx->probe, 34U), 0);
        EQ(memcmp(saved_commit, ctx->commit, 34U), 0);
        EQ(s_device.ep0_state, length); EQ(stall_count, stalls);
        EQ(flush_by_ep[0][0], flushes);
    }
    else if (id == 4U)
    {
        Request(0x21U, 1U, 0x0100U, 1U, 34U);
        memcpy(ctx->control_rx, data, 34U);
        Request(0x80U, USB_REQ_GET_DESCRIPTOR, 0x0100U, 0U, 18U);

        //在新事务DATA IN阶段注入旧OUT通知，Core不能交付旧SET_CUR
        HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
        EQ(ctx->probe_valid, false); EQ(ctx->commit_valid, false);
        EQ(s_device.ep0_state, USBD_EP0_DATA_IN);
        EQ(s_tx_transfer[0].active, true); EQ(s_endpoints[1][0].busy, true);
        HAL_PCD_DataInStageCallback(&s_pcd, 0U);
        EQ(ctx->probe_observed, false);
    }
    else if (id == 5U)
    {
        Request(0x21U, 1U, 0x0100U, 1U, 34U);
        memcpy(s_pcd.OUT_ep[0].xfer_buff, data, 34U);
        s_pcd.OUT_ep[0].xfer_count = 34U;
        HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
        HAL_PCD_DataInStageCallback(&s_pcd, 0U);
        Request(0xA1U, 0x81U, 0x0100U, 1U, 34U);
        EQ(ctx->probe_observed, false);
        HAL_PCD_DataInStageCallback(&s_pcd, 0U);
        EQ(ctx->probe_observed, true);
        HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
        Request(0x21U, 1U, 0x0200U, 1U, 34U);
        memcpy(s_pcd.OUT_ep[0].xfer_buff, data, 34U);
        s_pcd.OUT_ep[0].xfer_count = 34U;
        HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
        EQ(ctx->commit_valid, true); EQ(memcmp(ctx->commit, data, 34U), 0);
        EQ(stall_count, 0U); EQ(tx_size, 0U);
    }
    else if (id == 6U || id == 7U || id == 8U)
    {
        for (i = 0U; i < (id == 8U ? 3U : 1U); i++)
        {
            Request(0x81U, USB_REQ_GET_STATUS, 0U, id == 6U ? 0U : 1U, 2U);
            EQ(tx_size, 2U); EQ(tx_data[0], 0U); EQ(tx_data[1], 0U);
            EQ(stall_count, 0U);
            EQ(s_tx_transfer[0].source, ctx->control_tx);
            EQ((uint32_t)ctx->control_tx & 3U, 0U);
            EQ(ctx->control_tx[2], 0U); EQ(ctx->control_tx[3], 0U);
            HAL_PCD_DataInStageCallback(&s_pcd, 0U);
            HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
            EQ(ctx->probe_observed, false); EQ(ctx->commit_valid, false);
        }
    }
    else if (id >= 9U && id <= 20U)
    {
        req.bmRequest = 0x81U; req.bRequest = USB_REQ_GET_STATUS;
        req.wValue = 0U; req.wIndex = 0U; req.wLength = 2U;
        switch (id - 9U)
        {
            case 0: req.bmRequest = 0x01U; break;
            case 1: req.bmRequest = 0xA1U; break;
            case 2: req.bmRequest = 0x80U; break;
            case 3: req.bmRequest = 0x82U; break;
            case 4: req.bmRequest = 0xC1U; break;
            case 5: req.wValue = 1U; break;
            case 6: req.wValue = 0x0100U; break;
            case 7: req.wIndex = 2U; break;
            case 8: req.wIndex = 0x0101U; break;
            case 9: req.wLength = 0U; break;
            case 10: req.wLength = 1U; break;
            default: req.wLength = 3U; break;
        }
        tx_size = 999U; s_device.request = req;
        EQ(UVC_Setup(&s_device, &s_device.request), USBD_FAIL);
        EQ(tx_size, 999U); EQ(ctx->probe_valid, false); EQ(ctx->commit_valid, false);
    }
    else if (id == 21U || id == 22U)
    {
        Request(0x21U, 1U, 0x0200U, 1U, 34U);
        memcpy(s_pcd.OUT_ep[0].xfer_buff, data, 34U);
        s_pcd.OUT_ep[0].xfer_count = 34U;
        HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
        EQ(s_device.ep0_state, USBD_EP0_STALL); EQ(tx_size, 999U);
        EQ(s_endpoints[1][0].busy, false); EQ(s_tx_transfer[0].active, false);
        if (id == 21U) Request(0x81U, USB_REQ_GET_STATUS, 0U, 0U, 2U);
        else Request(0x80U, USB_REQ_GET_DESCRIPTOR, 0x0100U, 0U, 18U);
        EQ(tx_size, id == 21U ? 2U : 18U); EQ(stall_count, 0U);
        EQ(s_device.ep0_state, USBD_EP0_DATA_IN); EQ(s_endpoints[1][0].stalled, false);
    }
    else if (id >= 23U && id <= 30U)
    {
        EQ(UVC_ProbeSet(ctx, data), true);
        Request(0xA1U, 0x81U, 0x0100U, 1U, 34U);
        switch (id - 23U)
        {
            case 0: s_device.request.bmRequest = 0x80U; break;
            case 1: s_device.request.bRequest = USB_REQ_GET_DESCRIPTOR; break;
            case 2: s_device.request.wValue = 0x0200U; break;
            case 3: s_device.request.wIndex = 0U; break;
            case 4: s_device.request.wIndex = 0x0101U; break;
            case 5: s_device.request.wLength = 33U; break;
            case 6: s_device.ep0_state = USBD_EP0_STATUS_IN; break;
            default: s_device.ep_in[0].total_length = 18U; break;
        }
        EQ(UVC_EP0_TxSent(&s_device), USBD_OK);
        EQ(ctx->probe_reply_pending, false); EQ(ctx->probe_observed, false);
        EQ(UVC_CommitSet(ctx, data), false);
    }
    else if (id >= 31U && id <= 38U)
    {
        EQ(UVC_ProbeSet(ctx, data), true); ctx->probe_observed = true;
        memcpy(saved_probe, ctx->probe, 34U);
        Request(0x21U, 1U, 0x0100U, 1U, 34U);
        memcpy(ctx->control_rx, data, 34U); ctx->control_rx[0] = 1U;
        switch (id - 31U)
        {
            case 0: s_device.request.bmRequest = 0x00U; break;
            case 1: s_device.request.bRequest = USB_REQ_SET_DESCRIPTOR; break;
            case 2: s_device.request.wValue = 0x0200U; break;
            case 3: s_device.request.wIndex = 0U; break;
            case 4: s_device.request.wLength = 26U; break;
            case 5: s_device.request.wValue = 0x0101U; break;
            case 6: s_device.ep0_state = USBD_EP0_DATA_IN; break;
            default: s_device.ep_out[0].total_length = 18U; break;
        }
        stalls = stall_count;
        EQ(UVC_EP0_RxReady(&s_device), USBD_FAIL);
        EQ(ctx->pending_selector, 0U); EQ(memcmp(saved_probe, ctx->probe, 34U), 0);
        EQ(ctx->probe_observed, true); EQ(ctx->commit_valid, false); EQ(stall_count, stalls);
    }
    else EQ(0U, 1U);
    EQ(ctx->ep1_opened, false); EQ(ctx->streaming, false); EQ(s_pcd.IN_ep[1].maxpacket, 0U);
    return 0;
}
