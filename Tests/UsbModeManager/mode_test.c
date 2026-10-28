//复用已有HAL桩和请求工具，正式BSP、Platform、Core仍直接参与编译
#define Stream_TestRun UsbLegacy_TestRun
#include "../UsbDeviceStage1/usb_test.c"
#undef Stream_TestRun
#include "../../CM7/Platform/usb_mode.c"
#include "../../CM7/Framework/framework_init.c"

int Mode_TestRun(uint32_t id)
{
    uint32_t i;
    const FrameworkInitEntry_t *entry;
    uint32_t service_entries = 0U;
    uint32_t start_entries = 0U;

    g_result.case_id = id;

    //当前正式BSP直接读RCC，模拟HSI48时钟源和就绪位
    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);
    RCC->CR |= RCC_CR_HSI48RDY;

    //保留原有52项回归，第5项改为当前BSP实际检查的未就绪条件
    if (id < 52U)
    {
        if (id == 4U)
            RCC->CR &= ~RCC_CR_HSI48RDY;

        return UsbLegacy_TestRun(id);
    }

    if (id == 100U)
    {
        EQ(USB_Mode_Start(), SYS_ERR_INVALID_STATE);
        EQ(USB_Mode_RequestReconnect(), SYS_ERR_INVALID_STATE);
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
        for (entry = Image$$ER_FW_INIT$$Base;
             entry < Image$$ER_FW_INIT$$Limit; entry++)
        {
            if (entry->init == USB_Mode_Init)
            {
                EQ(entry->level, INIT_LEVEL_SERVICE);
                service_entries++;
            }

            if (entry->init == UsbMode_AppStart)
            {
                EQ(entry->level, INIT_LEVEL_APPLICATION);
                start_entries++;
            }
        }

        EQ(service_entries, 1U);
        EQ(start_entries, 1U);
        EQ(Framework_InitLevel(INIT_LEVEL_DEVICE), SYS_OK);
        EQ(s_initialized, true);
        EQ(s_usb_mode.initialized, false);
        EQ(start_count, 0U);
        EQ(Framework_InitLevel(INIT_LEVEL_SERVICE), SYS_OK);
        EQ(s_usb_mode.state, USB_MODE_STATE_IDLE);
        EQ(start_count, 0U);
        EQ(Framework_InitLevel(INIT_LEVEL_APPLICATION), SYS_OK);
        EQ(start_count, 1U);
        EQ(s_usb_mode.state, USB_MODE_STATE_RUNNING);

        if (id == 110U)
        {
            HAL_PCD_ResetCallback(&s_pcd);
            Request(0x80, USB_REQ_GET_DESCRIPTOR, 0x302, 0x409, 64);
            EQ(tx_size, 38U);
            EQ(stall_count, 0U);

            for (i = 0U; i < 18U; i++)
            {
                EQ(tx_data[2U + 2U * i], "h7_mac USB FS Test"[i]);
                EQ(tx_data[3U + 2U * i], 0U);
            }

            Request(0x80, USB_REQ_GET_DESCRIPTOR, 0x100, 0U, 18U);
            EQ(tx_data[8], 0x09U);
            EQ(tx_data[9], 0x12U);
            EQ(tx_data[10], 0x01U);
            EQ(tx_data[11], 0x00U);
            Configured();
            EQ(USB_Device_IsConfigured(), true);
        }

        return 0;
    }

    EQ(USB_Device_Init(), SYS_OK);
    EQ(USB_Mode_Init(), SYS_OK);

    if (id == 106U)
    {
        fault = 7U;
        EQ(USB_Mode_Start(), SYS_ERR_BUSY);
        EQ(s_usb_mode.state, USB_MODE_STATE_ERROR);
        EQ(s_usb_mode.last_result, SYS_ERR_BUSY);
        USB_Mode_Process();
        EQ(start_count, 1U);
        EQ(USB_Mode_Start(), SYS_ERR_INVALID_STATE);
        return 0;
    }

    if (id == 108U)
    {
        s_usb_mode.mode = USB_MODE_UVC;
        EQ(USB_Mode_Start(), SYS_ERR_NOT_SUPPORTED);
        s_usb_mode.mode = USB_MODE_MSC;
        EQ(USB_Mode_Start(), SYS_ERR_NOT_SUPPORTED);
        EQ(start_count, 0U);
        EQ(s_usb_mode.state, USB_MODE_STATE_IDLE);
        return 0;
    }

    if (id == 111U)
    {
        for (i = USB_MODE_STATE_STOPPING; i <= USB_MODE_STATE_ERROR; i++)
        {
            s_usb_mode.state = (UsbModeState_t)i;
            USB_Mode_Process();
            EQ(s_usb_mode.state, i);
        }

        EQ(start_count, 0U);
        EQ(stop_count, 0U);
        return 0;
    }

    EQ(USB_Mode_Start(), SYS_OK);

    if (id == 102U)
    {
        EQ(USB_Mode_Start(), SYS_ERR_BUSY);
        EQ(start_count, 1U);
        return 0;
    }

    if (id == 103U)
    {
        EQ(USB_Mode_Init(), SYS_OK);
        EQ(s_usb_mode.state, USB_MODE_STATE_RUNNING);
        EQ(USB_Mode_Start(), SYS_ERR_BUSY);
        EQ(start_count, 1U);
        return 0;
    }

    if (id == 104U)
    {
        EQ(USB_Mode_RequestReconnect(), SYS_ERR_NOT_SUPPORTED);
        EQ(s_usb_mode.state, USB_MODE_STATE_RUNNING);
        EQ(start_count, 1U);
        EQ(stop_count, 0U);
        return 0;
    }

    if (id == 105U)
    {
        for (i = 0U; i < 1000U; i++)
            USB_Mode_Process();

        EQ(s_usb_mode.state, USB_MODE_STATE_RUNNING);
        EQ(start_count, 1U);
        EQ(stop_count, 0U);
        return 0;
    }

    EQ(0U, 1U);
    return 1;
}
