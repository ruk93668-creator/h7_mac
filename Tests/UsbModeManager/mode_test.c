//复用既有HAL桩，实际执行当前正式BSP、Platform、Core和Mode代码
#define Stream_TestRun UsbLegacy_TestRun
#include "../UsbDeviceStage1/usb_test.c"
#undef Stream_TestRun
#include "../../CM7/Platform/usb_mode.c"
#include "../../CM7/Framework/framework_init.c"

//可控制的32位毫秒时钟
static uint32_t s_test_tick;

//失败时也保存实际状态与硬件调用计数
volatile uint32_t g_mode_observation[12];

static int Mode_Boot(bool configure);

static int Mode_RunCase(uint32_t id);

uint32_t Platform_TimeGetMs(void)
{
    return s_test_tick;
}

int Mode_TestRun(uint32_t id)
{
    int result = Mode_RunCase(id);
    g_mode_observation[0] = s_usb_mode.state;
    g_mode_observation[1] = s_usb_mode.mode;
    g_mode_observation[2] = s_usb_mode.target_mode;
    g_mode_observation[3] = (uint32_t)s_usb_mode.last_result;
    g_mode_observation[4] = s_usb_mode.state_enter_ms;
    g_mode_observation[5] = s_test_tick;
    g_mode_observation[6] = start_count;
    g_mode_observation[7] = stop_count;
    g_mode_observation[8] = s_device.dev_state;
    g_mode_observation[9] = (uint32_t)USB_Device_GetLastResult();
    g_mode_observation[10] = nvic;
    g_mode_observation[11] = s_usb_mode.initialized;
    return result;
}

//建立TEST连接，可选经过真实Core配置请求进入RUNNING
static int Mode_Boot(bool configure)
{
    EQ(USB_Device_Init(), SYS_OK);
    EQ(USB_Mode_Init(), SYS_OK);
    EQ(USB_Mode_Start(), SYS_OK);
    EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_CONFIGURED);
    if (configure)
    {
        HAL_PCD_ResetCallback(&s_pcd);
        Configured();
        USB_Mode_Process();
        EQ(s_usb_mode.state, USB_MODE_STATE_RUNNING);
    }
    return 0;
}

//每个用例从全新的模拟器内存开始
static int Mode_RunCase(uint32_t id)
{
    uint32_t i;
    const FrameworkInitEntry_t *entry;
    uint32_t service_entries = 0U, start_entries = 0U;
    g_result.case_id = id;

    //准备当前正式BSP直接检查的RCC寄存器
    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);
    RCC->CR |= RCC_CR_HSI48RDY;
    if (id < 52U)
    {
        if (id == 4U) RCC->CR &= ~RCC_CR_HSI48RDY;
        return UsbLegacy_TestRun(id);
    }
    if (id == 100U)
    {
        EQ(USB_Mode_Start(), SYS_ERR_INVALID_STATE);
        EQ(USB_Mode_RequestReconnect(USB_MODE_TEST), SYS_ERR_INVALID_STATE);
        USB_Mode_Process();
        EQ(s_usb_mode.state, USB_MODE_STATE_UNINITIALIZED);
        EQ(start_count, 0U);
        return 0;
    }
    if (id == 101U)
    {
        EQ(USB_Mode_Init(), SYS_OK);
        EQ(USB_Mode_Init(), SYS_OK);
        EQ(s_usb_mode.mode, USB_MODE_TEST);
        EQ(s_usb_mode.state, USB_MODE_STATE_IDLE);
        EQ(init_count, 0U);
        EQ(start_count, 0U);
        return 0;
    }
    if (id == 107U)
    {
        EQ(USB_Mode_Init(), SYS_OK);
        EQ(USB_Mode_Start(), SYS_ERR_INVALID_STATE);
        EQ(s_usb_mode.state, USB_MODE_STATE_ERROR);
        EQ(s_usb_mode.last_result, SYS_ERR_INVALID_STATE);
        EQ(start_count, 0U);
        return 0;
    }
    if (id == 109U || id == 110U)
    {
        for (entry = Image$$ER_FW_INIT$$Base; entry < Image$$ER_FW_INIT$$Limit; entry++)
        {
            if (entry->init == USB_Mode_Init) {EQ(entry->level, INIT_LEVEL_SERVICE); service_entries++;}
            if (entry->init == UsbMode_AppStart) {EQ(entry->level, INIT_LEVEL_APPLICATION); start_entries++;}
        }
        EQ(service_entries, 1U); EQ(start_entries, 1U);
        EQ(Framework_InitLevel(INIT_LEVEL_DEVICE), SYS_OK);
        EQ(s_initialized, true); EQ(s_usb_mode.initialized, false); EQ(start_count, 0U);
        EQ(Framework_InitLevel(INIT_LEVEL_SERVICE), SYS_OK);
        EQ(s_usb_mode.state, USB_MODE_STATE_IDLE); EQ(start_count, 0U);
        EQ(Framework_InitLevel(INIT_LEVEL_APPLICATION), SYS_OK);
        EQ(start_count, 1U); EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_CONFIGURED);
        if (id == 110U)
        {
            HAL_PCD_ResetCallback(&s_pcd);
            Request(0x80, USB_REQ_GET_DESCRIPTOR, 0x302, 0x409, 64);
            EQ(tx_size, 38U); EQ(stall_count, 0U);
            for (i = 0U; i < 18U; i++)
            {
                EQ(tx_data[2U + 2U * i], "h7_mac USB FS Test"[i]);
                EQ(tx_data[3U + 2U * i], 0U);
            }
            Configured(); USB_Mode_Process();
            EQ(USB_Device_IsConfigured(), true); EQ(s_usb_mode.state, USB_MODE_STATE_RUNNING);
        }
        return 0;
    }
    if (id == 106U || id == 108U || id == 111U)
    {
        EQ(USB_Device_Init(), SYS_OK); EQ(USB_Mode_Init(), SYS_OK);
        if (id == 106U)
        {
            fault = 7U;
            EQ(USB_Mode_Start(), SYS_ERR_BUSY); EQ(s_usb_mode.state, USB_MODE_STATE_ERROR);
            EQ(s_usb_mode.last_result, SYS_ERR_BUSY); USB_Mode_Process(); EQ(start_count, 1U);
            EQ(USB_Mode_Start(), SYS_ERR_INVALID_STATE);
        }
        else if (id == 108U)
        {
            s_usb_mode.mode = USB_MODE_UVC; EQ(USB_Mode_Start(), SYS_ERR_NOT_SUPPORTED);
            s_usb_mode.mode = USB_MODE_MSC; EQ(USB_Mode_Start(), SYS_ERR_NOT_SUPPORTED);
            EQ(start_count, 0U);
        }
        else
        {
            s_usb_mode.state = USB_MODE_STATE_REGISTERING; USB_Mode_Process();
            EQ(s_usb_mode.state, USB_MODE_STATE_REGISTERING); EQ(start_count, 0U); EQ(stop_count, 0U);
        }
        return 0;
    }
    if (id == 112U || id == 113U || id == 114U || id == 115U || id == 125U || id == 129U)
    {
        s_test_tick = id == 129U ? 0xFFFFFF00U : 100U;
        EQ(Mode_Boot(false), 0);
        if (id == 125U)
        {
            EQ(USB_Mode_RequestReconnect(USB_MODE_TEST), SYS_ERR_BUSY);
            EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_CONFIGURED); EQ(stop_count, 0U); return 0;
        }
        if (id == 115U)
        {
            fault = 9U;
            EQ(USBD_LL_Transmit(&s_device, 0x80U, tx_data, 1U), USBD_BUSY);
            USB_Mode_Process(); EQ(s_usb_mode.state, USB_MODE_STATE_ERROR);
            EQ(s_usb_mode.last_result, SYS_ERR_BUSY); return 0;
        }
        s_test_tick += 4999U; USB_Mode_Process(); EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_CONFIGURED);
        if (id == 112U) return 0;
        if (id == 114U) {HAL_PCD_ResetCallback(&s_pcd); Configured();}
        s_test_tick++; USB_Mode_Process();
        EQ(s_usb_mode.state, id == 114U ? USB_MODE_STATE_RUNNING : USB_MODE_STATE_ERROR);
        EQ(s_usb_mode.last_result, id == 114U ? SYS_OK : SYS_ERR_TIMEOUT); return 0;
    }
    EQ(Mode_Boot(true), 0);
    if (id == 102U || id == 103U || id == 105U)
    {
        if (id == 103U) EQ(USB_Mode_Init(), SYS_OK);
        if (id == 105U) for (i = 0U; i < 1000U; i++) USB_Mode_Process();
        EQ(s_usb_mode.state, USB_MODE_STATE_RUNNING); EQ(USB_Mode_Start(), SYS_ERR_BUSY);
        EQ(start_count, 1U); EQ(stop_count, 0U); return 0;
    }
    if (id == 124U)
    {
        s_usb_mode.state = USB_MODE_STATE_ERROR;
        EQ(USB_Mode_RequestReconnect(USB_MODE_TEST), SYS_ERR_BUSY);
        EQ(s_usb_mode.state, USB_MODE_STATE_ERROR); return 0;
    }
    if (id == 126U)
    {
        EQ(USB_Mode_RequestReconnect(USB_MODE_UVC), SYS_ERR_NOT_SUPPORTED);
        EQ(USB_Mode_RequestReconnect(USB_MODE_MSC), SYS_ERR_NOT_SUPPORTED);
        EQ(s_usb_mode.state, USB_MODE_STATE_RUNNING); EQ(stop_count, 0U); return 0;
    }
    s_test_tick = id == 128U ? 0xFFFFFF00U : 1000U;
    EQ(USB_Mode_RequestReconnect(USB_MODE_TEST), SYS_OK);
    EQ(s_usb_mode.target_mode, USB_MODE_TEST); EQ(s_usb_mode.state, USB_MODE_STATE_STOPPING);
    if (id == 104U) {EQ(stop_count, 0U); EQ(start_count, 1U); return 0;}
    if (id == 127U)
    {
        EQ(USB_Mode_RequestReconnect(USB_MODE_TEST), SYS_ERR_BUSY);
        EQ(s_usb_mode.state, USB_MODE_STATE_STOPPING); EQ(stop_count, 0U); return 0;
    }
    if (id == 117U) fault = 8U;
    USB_Mode_Process(); EQ(stop_count, 1U);
    if (id == 117U)
    {
        EQ(s_usb_mode.state, USB_MODE_STATE_ERROR); EQ(s_usb_mode.last_result, SYS_ERR_IO);
        USB_Mode_Process(); EQ(stop_count, 1U); return 0;
    }
    EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_RECONNECT);
    EQ(s_usb_mode.state_enter_ms, s_test_tick); EQ(nvic, 0U);
    if (id == 116U) {USB_Mode_Process(); EQ(stop_count, 1U); EQ(start_count, 1U); return 0;}
    s_test_tick += 499U; USB_Mode_Process();
    EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_RECONNECT); EQ(start_count, 1U);
    if (id == 118U) return 0;
    s_test_tick++; USB_Mode_Process();
    EQ(s_usb_mode.state, USB_MODE_STATE_STARTING); EQ(start_count, 1U);
    if (id == 119U || id == 128U) return 0;
    if (id == 122U || id == 123U) fault = 7U;
    USB_Mode_Process(); EQ(start_count, 2U);
    if (id == 122U)
    {
        EQ(s_usb_mode.state, USB_MODE_STATE_ERROR); EQ(s_usb_mode.last_result, SYS_ERR_BUSY); return 0;
    }
    if (id == 123U) {USB_Mode_Process(); EQ(start_count, 2U); return 0;}
    EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_CONFIGURED);
    if (id == 120U) {EQ(s_usb_mode.state_enter_ms, s_test_tick); return 0;}
    if (id == 121U)
    {
        s_test_tick += 4999U; USB_Mode_Process();
        EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_CONFIGURED); return 0;
    }
    if (id == 130U || id == 131U)
    {
        HAL_PCD_ResetCallback(&s_pcd); Configured(); USB_Mode_Process();
        EQ(s_usb_mode.state, USB_MODE_STATE_RUNNING); EQ(USB_Device_IsConfigured(), true);
        EQ(stop_count, 1U); EQ(start_count, 2U);
        if (id == 131U)
        {
            Request(0x80, USB_REQ_GET_DESCRIPTOR, 0x302, 0x409, 64U);
            EQ(tx_size, 38U); EQ(stall_count, 0U);
        }
        return 0;
    }
    EQ(0U, 1U); return 1;
}
