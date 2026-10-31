//复用真实Core和BSP，仅HAL外设使用既有桩
#include "../UsbCoreDecoupling/core_test.c"
#include "../../CM7/Service/usb_uvc_class.c"

//供模拟器检查实际字符串源地址和缓冲区边界
volatile uint32_t g_string_watch;
volatile uint32_t g_string_source;
volatile uint32_t g_string_storage_size;

//经真实注册和EP0请求验证字符串分发
int UvcString_TestRun(uint32_t id)
{
    uint16_t length = 0U;
    uint8_t *source;
    uint32_t i;
    uint32_t opens;
    static const uint8_t expected[22] =
    {
        22U, 3U, 'U', 0U, 'V', 0U, 'C', 0U, ' ', 0U,
        'C', 0U, 'a', 0U, 'm', 0U, 'e', 0U, 'r', 0U, 'a', 0U
    };
    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);
    RCC->CR |= RCC_CR_HSI48RDY;
    EQ(USB_Device_Init(), SYS_OK);
    EQ(USB_UVC_Register(), SYS_OK);
    EQ(s_device.pDesc->GetLangIDStrDescriptor, USB_UVC_GetLangIDStrDescriptor);
    EQ(s_device.pDesc->GetInterfaceStrDescriptor, USB_UVC_GetInterfaceStrDescriptor);
    EQ(USBD_SUPPORT_USER_STRING_DESC, 0U);
    EQ(USBD_CLASS_USER_STRING_DESC, 0U);
    EQ(USB_Device_Start(), SYS_OK);
    HAL_PCD_ResetCallback(&s_pcd);
    opens = open_count;
    if (id == 0U)
    {
        EQ(s_registered, true); EQ(s_device.pUserData[0], NULL);
        EQ(s_class_memory_used, false);
    }
    else if (id == 1U)
    {
        Request(0x80U, USB_REQ_GET_DESCRIPTOR, 0x0300U, 0U, 255U);
        EQ(tx_size, 4U); EQ(tx_data[0], 4U); EQ(tx_data[1], 3U);
        EQ(tx_data[2], 9U); EQ(tx_data[3], 4U); EQ(stall_count, 0U);
    }
    else if (id == 2U || id == 3U || id == 4U || id == 6U)
    {
        source = USB_UVC_GetInterfaceStrDescriptor(USBD_SPEED_FULL, &length);
        EQ(length, 22U);
        g_string_source = (uint32_t)source;
        g_string_storage_size = 24U;
        g_string_watch = 1U;
        Request(0x80U, USB_REQ_GET_DESCRIPTOR, 0x0305U, 0x0409U,
                id == 3U ? 255U : id == 4U ? 8U : 22U);
        g_string_watch = 0U;
        EQ(tx_size, id == 4U ? 8U : 22U);
        for (i = 0U; i < tx_size; i++) EQ(tx_data[i], expected[i]);
        EQ(stall_count, 0U); EQ(s_device.ep0_state, USBD_EP0_DATA_IN);
        EQ(s_tx_transfer[0].source, source);
        if (id == 6U)
        {
            EQ((uint32_t)source & 3U, 0U);
            EQ((uint32_t)s_pcd.IN_ep[0].xfer_buff & 3U, 0U);
            EQ(s_tx_pio_pool[0][22], 0U); EQ(s_tx_pio_pool[0][23], 0U);
            EQ(source[22], 0U); EQ(source[23], 0U);
        }
    }
    else if (id == 5U)
    {
        static const uint8_t unsupported[] = {1U, 2U, 3U, 4U, 6U, 255U};
        for (i = 0U; i < sizeof(unsupported); i++)
        {
            Request(0x80U, USB_REQ_GET_DESCRIPTOR, 0x0300U | unsupported[i], 0x0409U, 255U);
            EQ(stall_count > 0U, true); EQ(tx_size, 999U);
        }
    }
    else if (id == 7U)
    {
        uint8_t *first = USB_UVC_GetInterfaceStrDescriptor(USBD_SPEED_FULL, &length);
        for (i = 0U; i < 3U; i++)
        {
            Request(0x80U, USB_REQ_GET_DESCRIPTOR, 0x0305U, 0x0409U, 22U);
            EQ(tx_size, 22U); EQ(memcmp(tx_data, expected, 22U), 0);
            EQ(s_tx_transfer[0].source, first);
            HAL_PCD_DataInStageCallback(&s_pcd, 0U);
            HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
            Request(0x80U, USB_REQ_GET_DESCRIPTOR, 0x0300U, 0U, 4U);
            EQ(tx_size, 4U);
            HAL_PCD_DataInStageCallback(&s_pcd, 0U);
            HAL_PCD_DataOutStageCallback(&s_pcd, 0U);
        }
    }
    else { EQ(0U, 1U); }
    EQ(open_count, opens); EQ(s_pcd.IN_ep[1].maxpacket, 0U);
    EQ(s_class_memory_used, false);
    return 0;
}
