//复用真实固定内存池及已有HAL桩
#include "../UsbCoreDecoupling/core_test.c"
#include "../../CM7/Service/usb_uvc_class.c"

//补齐无标准库测试链接所需的ARM运行时桩
void __aeabi_memset(void *dest, size_t size, int value)
{
    volatile uint8_t *p = dest;
    while (size--) *p++ = (uint8_t)value;
}

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
        for (i = 0U; i < sizeof(*ctx); i++) EQ(((uint8_t *)ctx)[i], 0U);
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
    else { EQ(0U, 1U); }
    return 0;
}
