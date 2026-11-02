//复用真实USB路径和外设桩，不依赖Camera或Media
#include "../UvcStreaming/stream_test.c"

static int Contract_ReadDefault(uint8_t selector, uint16_t length);

int UvcContract_TestRun(uint32_t id)
{
    UsbUvcContext_t *ctx;
    UsbUvcStreamStatus_t status;
    uint8_t saved[34];
    uint32_t token;

    if (id == 10U)
    {
        //执行器把IPSR置为非零，验证公开API拒绝IRQ调用
        memset(&status, 0xA5, sizeof(status));
        EQ(USB_UVC_GetStreamStatus(&status), SYS_ERR_INVALID_STATE);
        EQ(status.completed_frames, 0xA5A5A5A5U);
        EQ(USB_UVC_SubmitFrame(s_frame, sizeof(s_frame)), SYS_ERR_INVALID_STATE);
        EQ(USB_UVC_CancelFrame(), SYS_ERR_INVALID_STATE);
        USB_UVC_Process(); EQ(s_uvc_active, NULL); return 0;
    }

    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48); RCC->CR |= RCC_CR_HSI48RDY;
    EQ(USB_Device_Init(), SYS_OK); EQ(USB_UVC_Register(), SYS_OK);
    EQ(USB_Device_Start(), SYS_OK); HAL_PCD_ResetCallback(&s_pcd); Configured();
    ctx = s_device.pUserData[0];
    if (id < 5U)
    {
        EQ(Contract_ReadDefault(id == 1U || id == 2U ? 2U : 1U,
            id == 1U || id == 3U ? 1U : id == 4U ? 255U : 34U), 0U);
        EQ(ctx->probe_valid, false); EQ(ctx->probe_observed, false);
        EQ(ctx->probe_reply_pending, false); EQ(ctx->commit_valid, false);
        EQ(USB_UVC_GetStreamStatus(&status), SYS_OK); EQ(status.can_submit, false);
        Request(1U, USB_REQ_SET_INTERFACE, 1U, 1U, 0U); EQ(Ep1_Stalled(), 0U);
        EQ(ctx->ep1_opened, false); return 0;
    }
    if (id == 5U)
    {
        //复现实板Windows先读默认值，然后正式协商并开流
        EQ(Contract_ReadDefault(1U, 34U), 0U);
        Request(0x21U, 1U, 0x100U, 1U, 34U);
        memset(s_pcd.OUT_ep[0].xfer_buff, 0, 34U);
        s_pcd.OUT_ep[0].xfer_count = 34U;
        HAL_PCD_DataOutStageCallback(&s_pcd, 0U); EQ(Ep1_Status(), 0U);
        Request(0xA1U, 0x81U, 0x100U, 1U, 34U);
        memcpy(s_negotiated, tx_data, 34U);
        HAL_PCD_DataInStageCallback(&s_pcd, 0U); HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
        Request(0xA1U, 0x83U, 0x100U, 1U, 34U);
        EQ(tx_size, 34U); HAL_PCD_DataInStageCallback(&s_pcd, 0U); HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
        Request(0xA1U, 0x82U, 0x100U, 1U, 34U);
        EQ(tx_size, 34U); HAL_PCD_DataInStageCallback(&s_pcd, 0U); HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
        //第二轮SET/GET后才能把实际协商结果提交为Commit
        EQ(Ep1_Commit(), 0U); EQ(Ep1_Open(), 0U);
        EQ(Frame(), 0U); EQ(ctx->completed_frames, 1U);
        EQ(USB_UVC_GetStreamStatus(&status), SYS_OK);
        EQ(status.frame_owned, false); EQ(status.can_submit, true); return 0;
    }
    if (id == 6U)
    {
        Request(0xA1U, 0x81U, 0x100U, 1U, 34U);
        Request(0x80U, USB_REQ_GET_DESCRIPTOR, 0x100U, 0U, 18U);
        Done(); HAL_PCD_DataInStageCallback(&s_pcd, 0U);
        EQ(ctx->probe_valid, false); EQ(ctx->probe_observed, false);
        EQ(ctx->probe_reply_pending, false); EQ(ctx->commit_valid, false); return 0;
    }
    EQ(Ep1_Commit(), 0U); memcpy(saved, ctx->commit, sizeof(saved));
    if (id == 7U)
    {
        Request(0x21U, 1U, 0x100U, 1U, 34U);
        memcpy(s_pcd.OUT_ep[0].xfer_buff, saved, 34U);
        UVC_Write32(s_pcd.OUT_ep[0].xfer_buff + 4U, 2000000U);
        s_pcd.OUT_ep[0].xfer_count = 34U; HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
        EQ(Ep1_Stalled(), 0U); EQ(Contract_ReadDefault(1U, 34U), 0U);
        EQ(ctx->probe_valid, false); EQ(ctx->probe_observed, false);
        EQ(ctx->commit_valid, true); EQ(memcmp(saved, ctx->commit, 34U), 0U);
        //默认读取不能替代一次成功的Probe，旧Commit仍独立保留
        Request(0x21U, 1U, 0x200U, 1U, 34U);
        memcpy(s_pcd.OUT_ep[0].xfer_buff, saved, 34U);
        s_pcd.OUT_ep[0].xfer_count = 34U; HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
        EQ(Ep1_Stalled(), 0U); return 0;
    }
    EQ(Ep1_Open(), 0U); EQ(Start(), 0U); EQ(Send(), 0U);
    EQ(USB_UVC_GetStreamStatus(&status), SYS_OK); EQ(status.frame_owned, true);
    EQ(status.frame_offset, 0U); token = status.token;
    if (id == 8U)
    {
        EQ(USB_UVC_CancelFrame(), SYS_OK); fault = 20U; USB_UVC_Process();
        EQ(USB_UVC_GetStreamStatus(&status), SYS_OK);
        EQ(status.frame_owned, true); EQ(status.can_submit, false); EQ(status.token, token);
        fault = 0U; USB_UVC_Process(); EQ(USB_UVC_GetStreamStatus(&status), SYS_OK);
        EQ(status.frame_owned, false); return 0;
    }
    if (id == 9U)
    {
        HAL_PCD_ISOINIncompleteCallback(&s_pcd, 1U); fault = 20U; USB_UVC_Process();
        EQ(USB_UVC_GetStreamStatus(&status), SYS_OK); EQ(status.frame_owned, true);
        EQ(status.state, USB_UVC_RECOVERY); Done(); EQ(s_uvc_payload.frame_offset, 0U);
        fault = 0U; USB_UVC_Process(); EQ(USB_UVC_GetStreamStatus(&status), SYS_OK);
        EQ(status.frame_owned, false); EQ(ctx->completed_frames, 0U); return 0;
    }
    if (id == 12U || id == 13U)
    {
        fault = 20U;
        if (id == 12U) HAL_PCD_ResetCallback(&s_pcd);
        else EQ(USBD_ClrClassConfig(&s_device, 1U), USBD_FAIL);
        EQ(USB_UVC_GetStreamStatus(&status), SYS_OK);
        EQ(status.frame_owned, true); EQ(s_device.pUserData[0], ctx);
        fault = 0U; EQ(USB_Device_Stop(), SYS_OK);
        EQ(s_uvc_payload.frame, NULL); EQ(s_device.pUserData[0], NULL);
        EQ(USB_UVC_GetStreamStatus(&status), SYS_ERR_INVALID_STATE); return 0;
    }
    if (id == 11U)
    {
        HAL_PCD_DisconnectCallback(&s_pcd); EQ(s_uvc_payload.frame, NULL);
        memset(&status, 0xA5, sizeof(status));
        EQ(USB_UVC_GetStreamStatus(&status), SYS_ERR_INVALID_STATE);
        EQ(status.completed_frames, 0xA5A5A5A5U); return 0;
    }
    EQ(0U, 1U); return 1;
}

static int Contract_ReadDefault(uint8_t selector, uint16_t length)
{
    UvcStreamingControl_t control;
    uint8_t expected[34];
    uint16_t actual = length < 34U ? length : 34U;
    UsbUvcContext_t *ctx = s_device.pUserData[0];
    UVC_DefaultControl(&control); UVC_EncodeControl(&control, expected);
    Request(0xA1U, 0x81U, (uint16_t)selector << 8, 1U, length);
    EQ(stall_count, 0U); EQ(tx_size, actual); EQ(memcmp(tx_data, expected, actual), 0U);
    EQ(ctx->probe_reply_pending, false);
    HAL_PCD_DataInStageCallback(&s_pcd, 0U); HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
    return 0;
}
