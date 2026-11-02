//真实Class、Platform、Core和BSP，仅外设HAL使用既有桩
#include "../UsbCoreDecoupling/core_test.c"
#include "../../CM7/Service/usb_uvc_class.c"

void __aeabi_memset(void *dest, size_t size, int value)
{
    volatile uint8_t *p = dest;
    while (size--) *p++ = (uint8_t)value;
}

//直接调用Setup前模拟Core保存当前请求，完成回调才能核对同一事务
static uint8_t UvcTest_Setup(USBD_SetupReqTypedef *req);

//每次测试由模拟器重新加载，互不继承状态
int UvcProbe_TestRun(uint32_t id)
{
    UsbUvcContext_t *ctx;
    UvcStreamingControl_t c;
    USBD_SetupReqTypedef req;
    uint8_t data[36];
    uint8_t saved[34];
    uint32_t i;
    uint32_t opens;
    uint8_t selector = 1U;
    uint8_t request = 0x81U;
    static const uint8_t probe_requests[] = {0x81,0x82,0x83,0x84,0x87,0x85,0x86,1};
    static const uint8_t commit_requests[] = {0x81,0x85,0x86,1};
    static const uint8_t unsupported[] = {0x82,0x83,0x84,0x87};

    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);
    RCC->CR |= RCC_CR_HSI48RDY;
    EQ(USB_Device_Init(), SYS_OK);
    EQ(USB_UVC_Register(), SYS_OK);
    EQ(USB_Device_Start(), SYS_OK);
    HAL_PCD_ResetCallback(&s_pcd);
    Configured();
    ctx = s_device.pUserData[0];
    EQ(ctx != NULL, true);
    opens = open_count;
    tx_size = 999U;
    UVC_DefaultControl(&c);
    memset(data, 0xA5, sizeof(data));
    UVC_EncodeControl(&c, data);
    memset(&req, 0, sizeof(req));
    req.bmRequest = 0xA1U; req.wIndex = 1U; req.wValue = 0x100U;
    req.wLength = 34U; req.bRequest = 0x81U;

    if (id == 0U)
    {
        for (i = 0U; i < 34U; i++) data[i] = (uint8_t)(i * 7U + 3U);
        EQ(UVC_DecodeControl(data, &c), true);
        memset(saved, 0, sizeof(saved));
        EQ(UVC_EncodeControl(&c, saved), true);
        for (i = 0U; i < 34U; i++) EQ(saved[i], data[i]);
        EQ(data[34], 0xA5U); EQ(data[35], 0xA5U);
        //按Table 4-47独立常量核对每个字段，避免编解码同错仍通过
        EQ(c.bmHint, 0xA03UL);
        EQ(c.bFormatIndex, 0x11UL);
        EQ(c.bFrameIndex, 0x18UL);
        EQ(c.dwFrameInterval, 0x342D261FUL);
        EQ(c.wKeyFrameRate, 0x423BUL);
        EQ(c.wPFrameRate, 0x5049UL);
        EQ(c.wCompQuality, 0x5E57UL);
        EQ(c.wCompWindowSize, 0x6C65UL);
        EQ(c.wDelay, 0x7A73UL);
        EQ(c.dwMaxVideoFrameSize, 0x968F8881UL);
        EQ(c.dwMaxPayloadTransferSize, 0xB2ABA49DUL);
        EQ(c.dwClockFrequency, 0xCEC7C0B9UL);
        EQ(c.bmFramingInfo, 0xD5UL);
        EQ(c.bPreferredVersion, 0xDCUL);
        EQ(c.bMinVersion, 0xE3UL);
        EQ(c.bMaxVersion, 0xEAUL);
    }
    else if (id == 1U)
    {
        EQ(UVC_DecodeControl(NULL, &c), false); EQ(UVC_DecodeControl(data, NULL), false);
        EQ(UVC_EncodeControl(NULL, data), false); EQ(UVC_EncodeControl(&c, NULL), false);
        EQ(UVC_ValidateControl(NULL), false); EQ(UVC_ProbeSet(NULL, data), false);
        EQ(UVC_CommitSet(NULL, data), false);
    }
    else if (id == 2U)
    {
        EQ(UVC_ValidateControl(&c), true); EQ(c.dwClockFrequency, 1000U);
        EQ(c.dwMaxPayloadTransferSize, 512U); EQ(c.dwMaxVideoFrameSize, 38400U);
        EQ(c.dwFrameInterval, 1000000U); EQ(data[18], 0U); EQ(data[19], 0x96U);
        EQ(data[22], 0U); EQ(data[23], 2U); EQ(data[26], 0xE8U); EQ(data[27], 3U);
        EQ(data[30], 3U); EQ(data[31], 1U); EQ(data[32], 1U); EQ(data[33], 1U);
    }
    else if (id == 3U) EQ(UVC_CommitSet(ctx, data), false);
    else if (id >= 4U && id < 20U)
    {
        EQ(UVC_ProbeSet(ctx, data), true); ctx->probe_observed = true;
        EQ(UVC_CommitSet(ctx, data), true);
        if (id < 12U) request = probe_requests[id - 4U];
        else if (id < 16U) { selector = 2U; request = commit_requests[id - 12U]; }
        else { selector = 2U; request = unsupported[id - 16U]; }
        req.wValue = (uint16_t)selector << 8; req.bRequest = request;
        if (request == 1U) req.bmRequest = 0x21U;
        EQ(UvcTest_Setup(&req), id < 16U ? USBD_OK : USBD_FAIL);
        if (id >= 16U) { EQ(tx_size, 999U); }
        else if (request == 1U)
        {
            EQ(ctx->pending_selector, selector); EQ(rx_size, 34U);
            EQ((uint32_t)s_pcd.OUT_ep[0].xfer_buff, (uint32_t)ctx->control_rx);
            memcpy(ctx->control_rx, data, 34U);
            EQ(UVC_EP0_RxReady(&s_device), USBD_OK); EQ(ctx->pending_selector, 0U);
        }
        else if (request == 0x85U) { EQ(tx_size, 2U); EQ(tx_data[0], 34U); EQ(tx_data[1], 0U); }
        else if (request == 0x86U) { EQ(tx_size, 1U); EQ(tx_data[0], 3U); }
        else
        {
            EQ(tx_size, 34U);
            if (request == 0x84U) for(i=0;i<34U;i++) EQ(tx_data[i], 0U);
            else EQ(memcmp(tx_data, data, 34U), 0);
        }
    }
    else if (id >= 20U && id < 32U)
    {
        req.bRequest = 0x85U;
        switch (id - 20U)
        {
            case 0: req.wIndex = 0U; break;
            case 1: req.wIndex = 0x101U; break;
            case 2: req.wValue = 0U; break;
            case 3: req.wValue = 0x300U; break;
            case 4: req.wValue = 0x101U; break;
            case 5: req.bmRequest = 0xA2U; break;
            case 6: req.bmRequest = 0x21U; break;
            case 7: req.wLength = 0U; break;
            case 8: req.bRequest = 1U; req.bmRequest = 0x21U; req.wLength = 26U; break;
            case 9: req.bRequest = 1U; req.bmRequest = 0x21U; req.wLength = 35U; break;
            case 10: req.bRequest = 0xFFU; break;
            default: req.bmRequest = 0xC1U; break;
        }
        EQ(UvcTest_Setup(&req), USBD_FAIL); EQ(ctx->pending_selector, 0U);
    }
    else if (id >= 32U && id < 66U)
    {
        EQ(UVC_ProbeSet(ctx, data), true); ctx->probe_observed = true;
        EQ(UVC_CommitSet(ctx, data), true); memcpy(saved, ctx->commit, 34U);
        data[id - 32U] ^= 0x80U;
        EQ(UVC_CommitSet(ctx, data), false); EQ(memcmp(saved, ctx->commit, 34U), 0);
    }
    else if (id == 66U)
    {
        memset(data, 0, 34U); data[2] = 99U; data[3] = 99U;
        EQ(UVC_ProbeSet(ctx, data), true); UVC_DecodeControl(ctx->probe, &c);
        EQ(UVC_ValidateControl(&c), true); EQ(ctx->probe_observed, false);
    }
    else if (id >= 67U && id <= 69U)
    {
        if (id == 67U) UVC_Write32(data + 4, 2000000U);
        if (id == 68U) UVC_Write32(data + 22, 128U);
        if (id == 69U) data[31] = 2U;
        EQ(UVC_ProbeSet(ctx, data), false); EQ(ctx->probe_valid, false);
    }
    else if (id == 70U || id == 71U)
    {
        EQ(UVC_ProbeSet(ctx, data), true);
        if (id == 71U)
        {
            req.wLength = 26U;
            EQ(UvcTest_Setup(&req), USBD_OK);
            EQ(UVC_EP0_TxSent(&s_device), USBD_OK);
        }
        EQ(ctx->probe_observed, false); EQ(UVC_CommitSet(ctx, data), false);
    }
    else if (id == 72U)
    {
        req.bmRequest = 0x21U; req.bRequest = 1U;
        EQ(UvcTest_Setup(&req), USBD_OK); EQ(ctx->pending_selector, 1U);
        req.bmRequest = 0xA1U; req.bRequest = 0x85U;
        EQ(UvcTest_Setup(&req), USBD_OK); EQ(ctx->pending_selector, 0U);
        EQ(UVC_EP0_RxReady(&s_device), USBD_FAIL); EQ(ctx->probe_valid, false);
    }
    else if (id == 73U)
    {
        EQ(UVC_ProbeSet(ctx, data), true); ctx->probe_observed = true;
        EQ(UVC_ProbeSet(ctx, data), true); EQ(UVC_CommitSet(ctx, data), false);
    }
    else if (id >= 74U && id <= 76U)
    {
        if (id == 74U)
        {
            Request(0x21U, 1U, 0x100U, 1U, 34U);
            memcpy(s_pcd.OUT_ep[0].xfer_buff, data, 34U);
            s_pcd.OUT_ep[0].xfer_count = 34U;
            HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
            EQ(ctx->probe_valid, true); EQ(tx_size, 0U); EQ(stall_count, 0U);
            HAL_PCD_DataInStageCallback(&s_pcd, 0U);
            Request(0xA1U, 0x81U, 0x100U, 1U, 34U);
            EQ(ctx->probe_observed, false); EQ(tx_size, 34U);
            memcpy(data, tx_data, 34U);
            HAL_PCD_DataInStageCallback(&s_pcd, 0U);
            EQ(ctx->probe_observed, true);
            HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
            Request(0x21U, 1U, 0x200U, 1U, 34U);
            memcpy(s_pcd.OUT_ep[0].xfer_buff, data, 34U);
            s_pcd.OUT_ep[0].xfer_count = 34U;
            HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
            EQ(ctx->commit_valid, true); EQ(memcmp(ctx->commit, data, 34U), 0);
            EQ(stall_count, 0U); EQ(tx_size, 0U);
        }
        else
        {
            Request(0x21U, 1U, id == 75U ? 0x200U : 0x100U, 1U, 34U);
            memcpy(s_pcd.OUT_ep[0].xfer_buff, data, 34U);
            s_pcd.OUT_ep[0].xfer_count = id == 75U ? 34U : 12U;
            HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
            EQ(ctx->commit_valid, false); EQ(ctx->probe_valid, false);
            EQ(stall_count > 0U, true);

            //Core尝试提交状态包，但Platform必须拒绝且不启动PIO
            EQ(s_device.ep0_state, USBD_EP0_STALL);
            EQ(tx_size, 999U); EQ(s_endpoints[1][0].busy, false);
            EQ(s_tx_transfer[0].active, false);
            EQ(s_pcd.IN_ep[0].is_stall, 1U);
        }
    }
    else if (id == 77U)
    {
        EQ((uint32_t)ctx->control_rx & 3U, 0U); EQ((uint32_t)ctx->control_tx & 3U, 0U);
        EQ(sizeof(ctx->control_tx), 36U); EQ(sizeof(*ctx) <= 1024U, true);
        req.bRequest = 0x87U; EQ(UvcTest_Setup(&req), USBD_OK);
        EQ(tx_size, 34U); EQ(ctx->control_tx[34], 0U); EQ(ctx->control_tx[35], 0U);
    }
    else if (id == 78U)
    {
        //未协商查询返回默认能力，读取成功仍不能授权Commit
        Request(0xA1U, 0x81U, 0x100U, 1U, 34U);
        EQ(tx_size, 34U); EQ(memcmp(tx_data, data, 34U), 0);
        EQ(ctx->probe_valid, false); EQ(ctx->probe_reply_pending, false);
        HAL_PCD_DataInStageCallback(&s_pcd, 0U);
        HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
        Request(0xA1U, 0x81U, 0x200U, 1U, 34U);
        EQ(tx_size, 34U); EQ(memcmp(tx_data, data, 34U), 0);
        EQ(ctx->commit_valid, false); EQ(ctx->probe_observed, false);
    }
    else if (id == 79U || id == 80U)
    {
        req.bRequest = id == 79U ? 0x87U : 0x85U; req.wLength = 1U;
        EQ(UvcTest_Setup(&req), USBD_OK); EQ(tx_size, 1U);
        EQ(tx_data[0], id == 79U ? 0U : 34U);
    }
    else if (id == 81U)
    {
        EQ(UVC_ProbeSet(ctx, data), true); ctx->probe_observed = true;
        EQ(UVC_CommitSet(ctx, data), true); EQ(UVC_DeInit(&s_device, 1U), USBD_OK);
        EQ(s_device.pUserData[0], NULL); EQ(UVC_Init(&s_device, 1U), USBD_OK);
        ctx = s_device.pUserData[0]; EQ(ctx->probe_valid, false); EQ(ctx->commit_valid, false);
    }
    else if (id == 82U)
    {
        //失败请求之后的新SETUP必须正常返回，不继承STALL快照
        Request(0x21U, 1U, 0x200U, 1U, 34U);
        memcpy(s_pcd.OUT_ep[0].xfer_buff, data, 34U);
        HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
        EQ(s_device.ep0_state, USBD_EP0_STALL); EQ(tx_size, 999U);
        Request(0xA1U, 0x85U, 0x100U, 1U, 2U);
        EQ(tx_size, 2U); EQ(tx_data[0], 34U); EQ(tx_data[1], 0U);
        EQ(s_endpoints[1][0].stalled, false); EQ(s_device.ep0_state, USBD_EP0_DATA_IN);
    }
    else if (id == 83U)
    {
        //新Probe失败后返回默认能力，不交付旧授权或覆盖有效Commit
        EQ(UVC_ProbeSet(ctx, data), true); ctx->probe_observed = true;
        EQ(UVC_CommitSet(ctx, data), true); memcpy(saved, ctx->commit, 34U);
        UVC_Write32(data + 4, 2000000U);
        EQ(UVC_ProbeSet(ctx, data), false);
        EQ(UvcTest_Setup(&req), USBD_OK);
        UVC_DefaultControl(&c); UVC_EncodeControl(&c, data);
        EQ(tx_size, 34U); EQ(memcmp(tx_data, data, 34U), 0);
        EQ(ctx->probe_valid, false); EQ(ctx->probe_reply_pending, false);
        EQ(ctx->probe_observed, false); EQ(ctx->commit_valid, true);
        EQ(memcmp(saved, ctx->commit, 34U), 0);
    }
    else if (id == 84U)
    {
        //主机不能写入只读能力字段，Probe响应恢复真实能力
        UVC_Write32(data + 18, 123U); UVC_Write32(data + 26, 48000000U);
        data[30] = 0xFFU; UVC_Write16(data + 12, 9999U);
        EQ(UVC_ProbeSet(ctx, data), true);
        UVC_DecodeControl(ctx->probe, &c); EQ(UVC_ValidateControl(&c), true);
    }
    else if (id == 85U)
    {
        //不把旧26字节SET_CUR当作有效UVC 1.1数据
        Request(0x21U, 1U, 0x100U, 1U, 26U);
        EQ(ctx->pending_selector, 0U); EQ(stall_count > 0U, true);
        EQ(ctx->probe_valid, false); EQ(tx_size, 999U);
    }
    else if (id == 86U)
    {
        Request(0x21U, 1U, 0x100U, 1U, 34U);
        s_pcd.OUT_ep[0].xfer_count = 35U;
        HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
        EQ(ctx->probe_valid, false); EQ(s_device.ep0_state, USBD_EP0_STALL);
        EQ(tx_size, 999U); EQ(s_endpoints[1][0].busy, false);
    }
    else if (id == 87U)
    {
        //真实Core对未定义的Commit GET_DEF必须STALL
        Request(0xA1U, 0x87U, 0x200U, 1U, 34U);
        EQ(stall_count > 0U, true); EQ(tx_size, 999U);
        EQ(ctx->commit_valid, false);
    }
    else if (id == 88U)
    {
        EQ(UVC_ProbeSet(ctx, data), true); ctx->probe_observed = true;
        Request(0x21U, 1U, 0x100U, 1U, 34U);
        s_pcd.OUT_ep[0].xfer_count = 12U;
        HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
        EQ(ctx->probe_valid, false); EQ(ctx->probe_observed, false);
        EQ(UVC_CommitSet(ctx, data), false); EQ(tx_size, 999U);
    }
    else EQ(0U, 1U);
    EQ(open_count, opens); EQ(ctx->ep1_opened, false); EQ(ctx->streaming, false);
    EQ(s_pcd.IN_ep[1].maxpacket, 0U);
    return 0;
}

//真实Core使用pdev->request；直接调用测试也必须保持相同现场
static uint8_t UvcTest_Setup(USBD_SetupReqTypedef *req)
{
    s_device.request = *req;
    return UVC_Setup(&s_device, &s_device.request);
}
