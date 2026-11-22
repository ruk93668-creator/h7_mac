//复用HAL夹具，直接执行生产USB回调、官方Core与快照事件环
#include "../UsbCoreDecoupling/core_test.c"

volatile uint32_t g_failure;

#define CHECK_EQUAL(actual, expected) do { \
    if (!Eq(__LINE__, (uint32_t)(actual), (uint32_t)(expected))) { \
        g_failure = g_result.fail_line; return g_failure; \
    } \
} while (0)

uint32_t UsbTrace_TestRun(uint32_t scenario)
{
    UsbDeviceR2Trace_t snapshot = {0};
    uint32_t event_index;

    //初始化正式BSP依赖的时钟事实，保持与原USB夹具一致
    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);
    RCC->CR |= RCC_CR_HSI48RDY;
    if (Core_Boot() != 0)
    {
        g_failure = g_result.fail_line;
        return g_failure;
    }
    CHECK_EQUAL(USB_Device_ReadR2Trace(&snapshot), true);
    CHECK_EQUAL(snapshot.events[snapshot.event_count - 1U].kind, USB_DEVICE_TRACE_SET_CONFIGURATION);
    CHECK_EQUAL(snapshot.events[snapshot.event_count - 1U].configuration, 1U);
    if (scenario == 0U)
    {
        HAL_PCD_SuspendCallback(&s_pcd);
        __disable_irq();
        CHECK_EQUAL(USB_Device_ReadR2Trace(&snapshot), false);
        CHECK_EQUAL(__get_PRIMASK(), 1U);
        __enable_irq();
        CHECK_EQUAL(snapshot.dev_state, USBD_STATE_SUSPENDED);
        CHECK_EQUAL(snapshot.dev_old_state, USBD_STATE_CONFIGURED);
        CHECK_EQUAL(snapshot.started, true);
        CHECK_EQUAL(snapshot.stop_required, false);
        CHECK_EQUAL(snapshot.last_result, SYS_OK);
        CHECK_EQUAL(snapshot.events[snapshot.event_count - 1U].kind, USB_DEVICE_TRACE_SUSPEND);
        HAL_PCD_ResumeCallback(&s_pcd);
        CHECK_EQUAL(USB_Device_ReadR2Trace(&snapshot), true);
        CHECK_EQUAL(__get_PRIMASK(), 0U);
        CHECK_EQUAL(snapshot.events[snapshot.event_count - 1U].kind, USB_DEVICE_TRACE_RESUME);
        return 0U;
    }

    //溢出后只保留最近八条，按时间顺序读取，能区分复位和断开
    for (event_index = 0U; event_index < 6U; ++event_index)
    {
        HAL_PCD_SuspendCallback(&s_pcd);
        HAL_PCD_ResumeCallback(&s_pcd);
    }
    HAL_PCD_ResetCallback(&s_pcd);
    HAL_PCD_DisconnectCallback(&s_pcd);
    CHECK_EQUAL(USB_Device_ReadR2Trace(&snapshot), false);
    CHECK_EQUAL(snapshot.event_count, USB_DEVICE_TRACE_CAPACITY);
    for (event_index = 1U; event_index < snapshot.event_count; ++event_index)
    {
        CHECK_EQUAL(snapshot.events[event_index].sequence, snapshot.events[event_index - 1U].sequence + 1U);
        CHECK_EQUAL(snapshot.events[event_index].tick_ms > snapshot.events[event_index - 1U].tick_ms, true);
    }
    CHECK_EQUAL(snapshot.events[6].kind, USB_DEVICE_TRACE_RESET);
    CHECK_EQUAL(snapshot.events[7].kind, USB_DEVICE_TRACE_DISCONNECT);
    CHECK_EQUAL(snapshot.events[7].sequence, snapshot.event_sequence);
    return 0U;
}
