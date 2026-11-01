//复用真实09B调度与HAL桩，不复制生产分包和图像逻辑
#include "../UvcStreaming/stream_test.c"

//仅避免单个测试翻译单元的私有变量重名
#define s_initialized pattern_initialized
#define s_last_result pattern_last_result
#include "../../CM7/Application/app_uvc_test.c"
#undef s_initialized
#undef s_last_result
#include "../../CM7/Application/app_main.c"

volatile uint32_t g_image_frozen;
static uint32_t camera_init_count, camera_run_count;
static SysResult_t camera_init_result;

//原有相机应用只保留调用边界，本套件不执行Camera或Media
SysResult_t App_CameraInit(void) { camera_init_count++; return camera_init_result; }
void App_CameraRun(void) { camera_run_count++; }

static int Pattern_Boot(bool configured);
static bool Pattern_Ready(void);
#if USB_MODE_UVC_TEST_ENABLED
static int Pattern_SendFrame(void);
static int Pattern_CheckImage(void);
#endif

int UvcPattern_TestRun(uint32_t id)
{
    UsbUvcContext_t *ctx;
    UsbUvcStreamStatus_t status;
    uint32_t i, opens, starts, token;
    uint8_t saved_commit[34];

    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);
    RCC->CR |= RCC_CR_HSI48RDY;
    g_result.case_id = id;

    if (id == 4U || id == 24U)
    {
        EQ(USB_Device_Init(), SYS_OK);
        if (id == 4U) EQ(Usb_TestRegister(), SYS_OK);
        else EQ(USB_UVC_Register(), SYS_OK);
        EQ(USB_Mode_Init(), SYS_OK);
        EQ(USB_Mode_Init(), SYS_OK);
        EQ(s_class_callbacks.Init, id == 4U ? Usb_ClassInit : UVC_Init);
        return 0;
    }
    if (id == 5U || id == 25U)
    {
        //DEVICE尚未完成时不能提前注册或启动
        EQ(USB_Mode_Init(), SYS_ERR_INVALID_STATE);
        EQ(s_usb_mode.initialized, false);
        EQ(start_count, 0U);
        EQ(USB_Device_Init(), SYS_OK);
        EQ(USB_Mode_Init(), SYS_OK);
        return 0;
    }
    #if USB_MODE_UVC_TEST_ENABLED
    if (id == 26U)
    {
        EQ(USB_Device_Init(), SYS_OK); EQ(USB_Mode_Init(), SYS_OK);
        camera_init_result = SYS_ERR_IO;
        EQ(App_Init(), SYS_OK);
        EQ(camera_init_count, 0U); EQ(Pattern_Ready(), true);
        return 0;
    }
    #endif
    EQ(Pattern_Boot(id != 13U), 0U);

    if (id < 10U)
    {
        EQ(USB_Mode_GetMode(), USB_MODE_TEST);
        EQ(s_class_callbacks.Init, Usb_ClassInit); EQ(s_uvc_active, NULL);
        EQ(Pattern_Ready(), false);
        if (id == 1U)
        {
            starts = start_count;
            EQ(App_Init(), SYS_OK); EQ(USB_Mode_Init(), SYS_OK);
            EQ(USB_Mode_Start(), SYS_ERR_BUSY);
            EQ(start_count, starts); EQ(init_count, 1U);
        }
        if (id == 2U)
        {
            EQ(USB_Mode_RequestReconnect(USB_MODE_UVC), SYS_ERR_NOT_SUPPORTED);
            EQ(s_usb_mode.state, USB_MODE_STATE_RUNNING);
        }
        if (id == 3U)
        {
            EQ(USB_Mode_RequestReconnect(USB_MODE_TEST), SYS_OK);
            USB_Mode_Process(); EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_RECONNECT);
            s_test_tick += 499U; USB_Mode_Process(); EQ(start_count, 1U);
            s_test_tick++; USB_Mode_Process(); USB_Mode_Process(); EQ(start_count, 2U);
        }
        for (i = 0U; i < 200U; i++) { Tick(); USB_UVC_Process(); App_Run(); }
        EQ(Pattern_Ready(), false); EQ(s_tx_transfer[1].active, false);
        //TEST不再有图像符号，由执行器检查ELF中确实不存在
        EQ(App_UvcTestInit(), SYS_OK); App_UvcTestRun();
        return 0;
    }

    #if USB_MODE_UVC_TEST_ENABLED
    EQ(USB_Mode_GetMode(), USB_MODE_UVC); EQ(s_class_callbacks.Init, UVC_Init);
    EQ(camera_init_count, 0U); EQ(camera_run_count, 0U);
    EQ(Pattern_Ready(), true); EQ(Pattern_CheckImage(), 0U);
    if (id == 13U)
    {
        EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_CONFIGURED);
        for (i = 0U; i < 100U; i++) { Tick(); USB_UVC_Process(); App_Run(); }
        EQ(s_uvc_active, NULL); EQ(s_tx_transfer[1].active, false);
        s_test_tick = 4999U; USB_Mode_Process(); EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_CONFIGURED);
        s_test_tick++; USB_Mode_Process(); EQ(s_usb_mode.state, USB_MODE_STATE_ERROR);
        EQ(s_usb_mode.last_result, SYS_ERR_TIMEOUT); return 0;
    }
    ctx = s_device.pUserData[0];
    if (id == 10U || id == 11U)
    {
        for (i = 0U; i < 100U; i++) { Tick(); USB_UVC_Process(); App_Run(); }
        EQ(s_uvc_payload.frame, NULL); EQ(ctx->commit_valid, false);
        EQ(USB_UVC_GetStreamStatus(&status), SYS_OK); EQ(status.can_submit, false);
        if (id == 11U)
        {
            EQ(Ep1_Commit(), 0U);
            for (i = 0U; i < 100U; i++) { Tick(); USB_UVC_Process(); App_Run(); }
            EQ(s_uvc_payload.frame, NULL); EQ(ctx->interface_alt[1], 0U);
        }
        return 0;
    }
    EQ(Ep1_Commit(), 0U); EQ(Ep1_Open(), 0U);
    if (id == 12U)
    {
        EQ(USB_UVC_GetStreamStatus(&status), SYS_OK); EQ(status.can_submit, true);
        EQ(Pattern_SendFrame(), 0U); EQ(Pattern_SendFrame(), 0U);
        EQ(g_packet_count, 152U); EQ(g_sofs[76] - g_sofs[0], 100U);
        EQ(ctx->completed_frames, 2U); return 0;
    }
    if (id == 14U)
    {
        starts = start_count; opens = open_count;
        EQ(App_Init(), SYS_OK); EQ(USB_Mode_Init(), SYS_OK);
        EQ(USB_Mode_Start(), SYS_ERR_BUSY);
        EQ(start_count, starts); EQ(open_count, opens); EQ(init_count, 1U);
        EQ(Pattern_CheckImage(), 0U); return 0;
    }
    if (id == 23U)
    {
        EQ(USB_Mode_RequestReconnect(USB_MODE_TEST), SYS_ERR_NOT_SUPPORTED);
        EQ(USB_Mode_RequestReconnect(USB_MODE_MSC), SYS_ERR_NOT_SUPPORTED);
        EQ(start_count, 1U); EQ(stop_count, 0U); return 0;
    }
    App_Run(); EQ(s_uvc_payload.frame, s_test_frame);
    EQ(USB_UVC_GetStreamStatus(&status), SYS_OK); EQ(status.can_submit, false);
    Tick(); USB_UVC_Process(); EQ(ctx->stream_state, USB_UVC_IN_FLIGHT);
    token = ctx->packet.token;

    if (id == 15U)
    {
        for (i = 0U; i < 200U; i++) App_Run();
        EQ(s_uvc_payload.frame, s_test_frame); EQ(ctx->packet.token, token);
        EQ(s_uvc_payload.frame_offset, 0U); EQ(Pattern_CheckImage(), 0U); return 0;
    }
    if (id == 16U)
    {
        Request(1U, USB_REQ_SET_INTERFACE, 0U, 1U, 0U); EQ(Ep1_Status(), 0U);
        for (i = 0U; i < 100U; i++) { Tick(); USB_UVC_Process(); App_Run(); }
        EQ(s_uvc_payload.frame, NULL); EQ(ctx->interface_alt[1], 0U);
        EQ(Ep1_Open(), 0U); App_Run(); EQ(s_uvc_payload.frame, s_test_frame); return 0;
    }
    if (id == 17U || id == 18U)
    {
        if (id == 17U) HAL_PCD_ResetCallback(&s_pcd);
        else HAL_PCD_DisconnectCallback(&s_pcd);
        App_Run(); EQ(s_uvc_active, NULL); EQ(s_uvc_payload.frame, NULL);
        HAL_PCD_ResetCallback(&s_pcd); Configured(); ctx = s_device.pUserData[0];
        App_Run(); EQ(s_uvc_payload.frame, NULL); EQ(ctx->commit_valid, false);
        EQ(Ep1_Commit(), 0U); EQ(Ep1_Open(), 0U); App_Run();
        EQ(s_uvc_payload.frame, s_test_frame); Tick(); USB_UVC_Process();
        EQ(ctx->packet.token > token, true); return 0;
    }
    if (id == 19U || id == 20U || id == 21U)
    {
        fault = 20U;
        if (id == 19U) HAL_PCD_ISOINIncompleteCallback(&s_pcd, 1U);
        else if (id == 20U) { Request(1U, USB_REQ_SET_INTERFACE, 0U, 1U, 0U); EQ(Ep1_Stalled(), 0U); }
        else HAL_PCD_ResetCallback(&s_pcd);
        for (i = 0U; i < 10U; i++) { USB_UVC_Process(); App_Run(); }
        EQ(s_uvc_payload.frame, s_test_frame); EQ(ctx->packet.token, token);
        EQ(Pattern_CheckImage(), 0U); EQ(s_class_memory_used, true);
        fault = 0U;
        if (id == 21U)
        {
            EQ(USB_Device_Stop(), SYS_OK); App_Run(); EQ(s_uvc_payload.frame, NULL);
            return 0;
        }
        if (id == 20U) { Request(1U, USB_REQ_SET_INTERFACE, 0U, 1U, 0U); EQ(Ep1_Status(), 0U); }
        else USB_UVC_Process();
        EQ(s_uvc_payload.frame, NULL); App_Run();
        EQ(s_uvc_payload.frame, id == 19U ? s_test_frame : NULL); return 0;
    }
    if (id == 22U)
    {
        memcpy(saved_commit, ctx->commit, 34U);
        Done(); USB_UVC_Process(); App_Run();
        EQ(s_uvc_payload.frame_offset, 510U); EQ(ctx->completed_frames, 0U);
        EQ(memcmp(saved_commit, ctx->commit, 34U), 0U); return 0;
    }
    if (id == 27U)
    {
        fault = 9U; Done(); USB_UVC_Process(); Tick(); USB_UVC_Process(); App_Run();
        EQ(s_uvc_payload.frame_offset, 510U); EQ(s_uvc_payload.frame, s_test_frame);
        fault = 0U; Tick(); USB_UVC_Process(); EQ(ctx->stream_state, USB_UVC_IN_FLIGHT); return 0;
    }
    #endif
    EQ(0U, 1U); return 1;
}

static int Pattern_Boot(bool configured)
{
    EQ(USB_Mode_Start(), SYS_ERR_INVALID_STATE);
    App_Run(); EQ(Pattern_Ready(), false);
    EQ(USB_Device_Init(), SYS_OK); EQ(USB_Mode_Init(), SYS_OK);
    EQ(s_usb_mode.state, USB_MODE_STATE_IDLE); EQ(start_count, 0U);
    EQ(App_Init(), SYS_OK); EQ(camera_init_count, USB_MODE_UVC_TEST_ENABLED ? 0U : 1U);
    g_image_frozen = 1U;
    EQ(UsbMode_AppStart(), SYS_OK); EQ(start_count, 1U);
    EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_CONFIGURED);
    if (configured)
    {
        HAL_PCD_ResetCallback(&s_pcd); Configured(); USB_Mode_Process();
        EQ(s_usb_mode.state, USB_MODE_STATE_RUNNING);
    }
    return 0;
}

static bool Pattern_Ready(void)
{
#if USB_MODE_UVC_TEST_ENABLED
    return s_pattern_ready;
#else
    return false;
#endif
}

#if USB_MODE_UVC_TEST_ENABLED
static int Pattern_SendFrame(void)
{
    uint32_t i, j, length, offset = 0U, wait;
    uint8_t fid = s_uvc_payload.fid;
    App_Run(); EQ(s_uvc_payload.frame, s_test_frame);
    for (i = 0U; i < 76U; i++)
    {
        for (wait = 0U; s_uvc_active->stream_state != USB_UVC_IN_FLIGHT && wait < 200U; wait++)
        { Tick(); USB_UVC_Process(); App_Run(); }
        EQ(s_uvc_active->stream_state, USB_UVC_IN_FLIGHT);
        length = i == 75U ? 152U : 512U;
        EQ(s_tx_transfer[1].total, length); EQ(s_tx_pio_pool[1][0], 2U);
        EQ(s_tx_pio_pool[1][1], 0x80U | fid | (i == 75U ? 2U : 0U));
        for (j = 0U; j < length - 2U; j++) EQ(s_tx_pio_pool[1][j + 2U], s_test_frame[offset + j]);
        g_lengths[g_packet_count] = length; g_headers[g_packet_count] = s_tx_pio_pool[1][1];
        g_sofs[g_packet_count++] = USB_Device_GetSofSequence();
        Done(); EQ(s_uvc_payload.frame_offset, offset); USB_UVC_Process();
        offset += length - 2U;
    }
    EQ(offset, 38400U); EQ(s_uvc_payload.frame, NULL);
    EQ(s_uvc_payload.fid, fid ^ 1U); EQ(Pattern_CheckImage(), 0U);
    return 0;
}

static int Pattern_CheckImage(void)
{
    uint32_t y, x, i, offset;
    //与生产生成器独立的预期顺序，不借用其颜色查表
    static const uint8_t expected[32] = {
        235,128,235,128,210,16,210,146,170,166,170,16,145,54,145,34,
        106,202,106,222,81,90,81,240,41,240,41,110,16,128,16,128
    };
    EQ(sizeof(s_test_frame), 38400U); EQ((uint32_t)s_test_frame & 3U, 0U);
    for (y = 0U; y < 120U; y++) for (x = 0U; x < 160U; x += 2U)
    {
        offset = (y * 160U + x) * 2U;
        for (i = 0U; i < 4U; i++) EQ(s_test_frame[offset + i], expected[(x / 20U) * 4U + i]);
    }
    return 0;
}

#endif
