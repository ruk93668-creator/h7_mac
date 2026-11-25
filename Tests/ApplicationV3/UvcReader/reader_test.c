//复用真实Class/Platform/BSP和既有可控主机端点夹具。
#include "../../UvcStreaming/stream_test.c"
#include "video_config.h"

static unsigned Test_Proof(uint32_t accepted, uint32_t retired, UsbUvcReaderExit_t reason)
{
    UsbUvcReaderProof_t proof;
    EQ(USB_UVC_ReadReaderProof(&proof), SYS_OK);
    EQ(proof.accepted_id, accepted);
    EQ(proof.retired_id, retired);
    EQ(proof.retired_reason, reason);
    return 0U;
}

static unsigned Test_RawDescriptor(void)
{
    uint16_t length, offset;
    const uint8_t *descriptor = USB_UVC_GetFSConfigDescriptor(&length);
    bool format_seen = false, frame_seen = false;

    EQ(descriptor != NULL, true);
    for (offset = 0U; offset + 2U <= length; offset += descriptor[offset])
    {
        const uint8_t *entry = descriptor + offset;
        EQ(entry[0] >= 2U && offset + entry[0] <= length, true);
        if (entry[1] != 0x24U)
            continue;
        if (entry[0] == 27U && entry[2] == 0x04U)
        {
            EQ(memcmp(entry + 5U, "YUY2", 4U), 0);
            format_seen = true;
        }
        if (entry[0] == 30U && entry[2] == 0x05U)
        {
            EQ(UVC_Read16(entry + 5U), 160U);
            EQ(UVC_Read16(entry + 7U), 120U);
            EQ(UVC_Read32(entry + 17U), 38400U);
            EQ(UVC_Read32(entry + 21U), 1000000U);
            EQ(UVC_Read32(entry + 26U), 1000000U);
            frame_seen = true;
        }
    }
    EQ(format_seen && frame_seen, true);
    return 0U;
}

static unsigned Test_ConfigureRaw(void)
{
    const VideoConfig_t *profile = VideoConfig_GetDefault();
    VideoStreamConfig_t stream = {0};

    EQ(profile->camera.format, CAMERA_PIXEL_FORMAT_YUV422);
    EQ(profile->camera.width, 160U);
    EQ(profile->camera.height, 120U);
    EQ(profile->output_fps, 10U);
    stream.camera = profile->camera;
    stream.fps = profile->output_fps;
    stream.max_frame_bytes = profile->output_max_frame_bytes;

    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);
    RCC->CR |= RCC_CR_HSI48RDY;
    EQ(USB_Device_Init(), SYS_OK);
    EQ(USB_UVC_Configure(&stream), SYS_OK);
    EQ(Test_RawDescriptor(), 0U);
    EQ(USB_UVC_Register(), SYS_OK);
    EQ(USB_Device_Start(), SYS_OK);
    HAL_PCD_ResetCallback(&s_pcd);
    Configured();
    return 0U;
}

static unsigned Test_FullFrame(void)
{
    uint32_t packet, index, length, offset = 0U;

    for (index = 0U; index < sizeof(s_frame); ++index)
        s_frame[index] = (uint8_t)(index ^ (index >> 8));
    EQ(Start(), 0U);
    EQ(USB_UVC_SubmitFrame(s_frame, sizeof(s_frame)), SYS_ERR_BUSY);
    for (packet = 0U; packet < 38U; ++packet)
    {
        EQ(Send(), 0U);
        length = packet == 37U ? 623U : 1021U;
        EQ(s_tx_transfer[1].total, length + 2U);
        EQ(s_tx_pio_pool[1][0], 2U);
        EQ(s_tx_pio_pool[1][1] & 2U, packet == 37U ? 2U : 0U);
        for (index = 0U; index < length; ++index)
            EQ(s_tx_pio_pool[1][2U + index], s_frame[offset + index]);
        Done();
        EQ(Test_Proof(1U, 0U, USB_UVC_READER_EXIT_NONE), 0U);
        USB_UVC_Process();
        offset += length;
    }
    EQ(offset, sizeof(s_frame));
    EQ(USB_UVC_GetCompletedFrameCount(), 1U);
    EQ(Test_Proof(1U, 1U, USB_UVC_READER_EXIT_EOF), 0U);
    EQ(UVC_PayloadQuiet(), true);
    return 0U;
}

static unsigned Test_AbortFailure(void)
{
    UsbUvcReaderProof_t proof;
    SysResult_t first_error;

    EQ(Send(), 0U);
    EQ(USB_UVC_CancelFrame(), SYS_OK);
    EQ(Test_Proof(1U, 0U, USB_UVC_READER_EXIT_NONE), 0U);
    fault = 20U;
    USB_UVC_Process();
    EQ(USB_UVC_ReadReaderProof(&proof), SYS_OK);
    first_error = proof.accepted_first_fault;
    EQ(first_error != SYS_OK, true);
    EQ(proof.retired_id, 0U);
    EQ(s_uvc_payload.frame, s_frame);
    fault = 0U;
    USB_UVC_Process();
    EQ(Test_Proof(1U, 1U, USB_UVC_READER_EXIT_CANCELLED), 0U);
    EQ(USB_UVC_ReadReaderProof(&proof), SYS_OK);
    EQ(proof.accepted_first_fault, first_error);
    EQ(UVC_PayloadQuiet(), true);
    EQ(USB_UVC_GetCompletedFrameCount(), 0U);
    return 0U;
}

static unsigned Test_NewClassCannotRetireOld(void)
{
    UsbUvcContext_t *old = s_uvc_active;
    UsbUvcContext_t *fresh;

    EQ(Send(), 0U);
    //负对照强制丢弃Context，保留真实Payload与旧提交凭据。
    s_device.pUserData[0] = NULL;
    s_uvc_active = NULL;
    USBD_free(old);
    EQ(Test_Proof(1U, 0U, USB_UVC_READER_EXIT_NONE), 0U);
    EQ(UVC_Init(&s_device, 1U), USBD_OK);
    fresh = s_uvc_active;
    EQ(fresh->reader_id, 0U);
    EQ(UVC_DeInit(&s_device, 1U), USBD_FAIL);
    fresh->commit_valid = true;
    EQ(UVC_SetStreamAlt(fresh, 1U), USBD_FAIL);
    //即使诊断模拟新Class已可提交，Class自身也必须拒绝覆盖旧帧。
    fresh->ep1_opened = true;
    fresh->interface_alt[1] = 1U;
    fresh->stream_state = USB_UVC_WAIT_FRAME;
    EQ(USB_UVC_SubmitFrame(s_frame, sizeof(s_frame)), SYS_ERR_BUSY);
    USB_UVC_Process();
    EQ(Test_Proof(1U, 0U, USB_UVC_READER_EXIT_NONE), 0U);
    EQ(s_uvc_payload.frame, s_frame);

    //继续注入快照已空但历史凭据未退休，不能靠frame_owned=false触发新提交。
    s_uvc_payload.frame = NULL;
    s_uvc_payload.frame2 = NULL;
    s_uvc_payload.pending_token = 0U;
    s_uvc_payload.state = USB_UVC_PAYLOAD_IDLE;
    EQ(USB_UVC_SubmitFrame(s_frame, sizeof(s_frame)), SYS_ERR_BUSY);
    EQ(UVC_DeInit(&s_device, 1U), USBD_FAIL);
    EQ(Test_Proof(1U, 0U, USB_UVC_READER_EXIT_NONE), 0U);
    return 0U;
}

int Stage02_TestRun(uint32_t scenario)
{
    UsbUvcStreamStatus_t status;
    UsbUvcContext_t *active;
    uint32_t token;

    EQ(Test_ConfigureRaw(), 0U);
    EQ(USB_UVC_GetStreamStatus(&status), SYS_OK);
    EQ(status.can_submit, false);
    EQ(USB_UVC_SubmitFrame(s_frame, sizeof(s_frame)), SYS_ERR_INVALID_STATE);
    EQ(Ep1_Commit(), 0U);
    EQ(UVC_Read32(s_negotiated + 4U), 1000000U);
    EQ(UVC_Read32(s_negotiated + 18U), 38400U);
    EQ(UVC_Read32(s_negotiated + 22U), 1023U);
    EQ(Ep1_Open(), 0U);
    EQ(USB_UVC_GetStreamStatus(&status), SYS_OK);
    EQ(status.can_submit, true);
    if (scenario == 0U)
        return 0;
    if (scenario == 1U)
        return Test_FullFrame();

    if (scenario == 9U)
    {
        s_reader_proof.accepted_id = UINT32_MAX - 1U;
        s_reader_proof.retired_id = UINT32_MAX - 1U;
        EQ(Start(), 0U);
        EQ(USB_UVC_CancelFrame(), SYS_OK);
        USB_UVC_Process();
        EQ(Test_Proof(UINT32_MAX, UINT32_MAX, USB_UVC_READER_EXIT_CANCELLED), 0U);
        EQ(USB_UVC_SubmitFrame(s_frame, sizeof(s_frame)), SYS_ERR_NO_RESOURCE);
        return 0;
    }

    EQ(Start(), 0U);
    active = s_uvc_active;
    if (scenario == 2U)
    {
        EQ(USB_UVC_CancelFrame(), SYS_OK);
        EQ(Test_Proof(1U, 0U, USB_UVC_READER_EXIT_NONE), 0U);
        USB_UVC_Process();
        EQ(Test_Proof(1U, 1U, USB_UVC_READER_EXIT_CANCELLED), 0U);
    }
    else if (scenario == 3U)
        return Test_AbortFailure();
    else if (scenario == 4U)
    {
        EQ(Send(), 0U);
        EQ(UVC_SetStreamAlt(active, 0U), USBD_OK);
        EQ(Test_Proof(1U, 1U, USB_UVC_READER_EXIT_CLOSED), 0U);
        EQ(UVC_SetStreamAlt(active, 1U), USBD_OK);
        EQ(Start(), 0U);
        EQ(Test_Proof(2U, 1U, USB_UVC_READER_EXIT_CLOSED), 0U);
    }
    else if (scenario == 5U || scenario == 6U)
    {
        EQ(Send(), 0U);
        if (scenario == 5U)
            HAL_PCD_ResetCallback(&s_pcd);
        else
            Request(0x00U, USB_REQ_SET_CONFIGURATION, 0U, 0U, 0U);
        EQ(s_uvc_active, NULL);
        EQ(USB_UVC_GetStreamStatus(&status), SYS_ERR_INVALID_STATE);
        EQ(Test_Proof(1U, 1U, USB_UVC_READER_EXIT_CLOSED), 0U);
    }
    else if (scenario == 7U)
        return Test_NewClassCannotRetireOld();
    else if (scenario == 8U)
    {
        EQ(Send(), 0U);
        token = s_uvc_payload.pending_token;
        s_uvc_payload.pending_token = 0U;
        EQ(UVC_SetStreamAlt(active, 0U), USBD_FAIL);
        EQ(s_uvc_payload.frame, s_frame);
        EQ(Test_Proof(1U, 0U, USB_UVC_READER_EXIT_NONE), 0U);
        EQ(UVC_DeInit(&s_device, 1U), USBD_FAIL);
        //上下文逻辑端点为关也不能跳过未结清的引用检查。
        active->ep1_opened = false;
        EQ(UVC_DeInit(&s_device, 1U), USBD_FAIL);
        EQ(s_uvc_active, active);
        s_uvc_payload.pending_token = token;
    }
    else if (scenario == 10U)
    {
        EQ(Send(), 0U);
        HAL_PCD_SuspendCallback(&s_pcd);
        Tick();
        USB_UVC_Process();
        EQ(Test_Proof(1U, 0U, USB_UVC_READER_EXIT_NONE), 0U);
        HAL_PCD_ResumeCallback(&s_pcd);
        Done();
        USB_UVC_Process();
        EQ(s_uvc_payload.frame_offset, 1021U);
        EQ(Test_Proof(1U, 0U, USB_UVC_READER_EXIT_NONE), 0U);
    }
    return 0;
}
