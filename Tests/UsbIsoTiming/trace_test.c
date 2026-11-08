//直接编译真实采样器，USB寄存器和DWT由执行器模拟
#include "usb_iso_timing.c"

uint32_t SystemCoreClock = 480000000U;
volatile uint32_t uwTick;
volatile uint32_t g_trace_test_checks;
volatile uint32_t g_trace_test_line;
volatile uint32_t g_trace_test_context;

//测试中只替换库清零，不复制采样逻辑
void *memset(void *target, int value, size_t size)
{
    uint8_t *p = target;
    while (size-- != 0U)
        *p++ = (uint8_t)value;
    return target;
}

//执行器在此入口模拟IRQ上下文切换
__attribute__((noinline)) void TraceTest_Context(uint32_t value)
{
    g_trace_test_context = value;
    __NOP();
}

#define CHECK(value) do { g_trace_test_checks++; if (!(value)) { g_trace_test_line = __LINE__; return __LINE__; } } while (0)

//验证实际采样器的状态、边界、冻结和临界区行为
uint32_t TraceTest_Run(uint32_t id)
{
#if APP_USB_ISO_TIMING_TEST_ENABLED
    uint32_t i;
    USB_OTG_DeviceTypeDef *device = (USB_OTG_DeviceTypeDef *)(USB2_OTG_FS_PERIPH_BASE + USB_OTG_DEVICE_BASE);
    USB_OTG_INEndpointTypeDef *ep = (USB_OTG_INEndpointTypeDef *)(USB2_OTG_FS_PERIPH_BASE + USB_OTG_IN_ENDPOINT_BASE + USB_OTG_EP_REG_SIZE);

    TIM6->CR1 = TIM_CR1_CEN;
    TIM6->DIER = TIM_DIER_UIE;
    TIM6->ARR = 999U;
    RCC->APB1LLPENR |= RCC_APB1LLPENR_TIM6LPEN;
    if (id == 11U)
        DWT->CTRL |= DWT_CTRL_NOCYCCNT_Msk;
    if (id == 12U)
        SystemCoreClock = 0U;
    UsbIsoTiming_Init();
    if (id == 11U || id == 12U)
    {
        CHECK(g_usb_iso_timing.enabled == 0U);
        CHECK(g_usb_iso_timing.state == USB_ISO_TRACE_ERROR);
        g_usb_iso_timing.request = 1U;
        UsbIsoTiming_Process();
        UsbIsoTiming_Record(USB_ISO_TIME_SUBMIT, 1U, 1023U);
        CHECK(g_usb_iso_timing.count == 0U);
        return 0U;
    }
    CHECK(g_usb_iso_timing.enabled == 1U);
    CHECK(g_usb_iso_timing.version == 2U);
    CHECK(g_usb_iso_timing.state == USB_ISO_TRACE_IDLE);
    CHECK(g_usb_iso_timing.capacity == 512U && g_usb_iso_timing.record_bytes == 48U);
    CHECK(g_usb_iso_timing.buffer_address == USB_ISO_TIMING_BASE);
    if (id == 0U)
    {
        uint32_t event;
        g_usb_iso_timing.request = 1U;
        UsbIsoTiming_Process();
        for (event = USB_ISO_TIME_NOTIFY_BEGIN; event <= USB_ISO_TIME_STACK_END; event++)
            UsbIsoTiming_Record(event, event, 0U);
        CHECK(g_usb_iso_timing.count == 13U);
        CHECK(s_usb_iso_records[12].event == USB_ISO_TIME_STACK_END);
        UsbIsoTiming_Record(USB_ISO_TIME_STACK_END + 1U, 0U, 0U);
        CHECK(g_usb_iso_timing.count == 13U);
        return 0U;
    }
    if (id == 1U)
    {
        UsbIsoTiming_Record(USB_ISO_TIME_SUBMIT, 1U, 1023U);
        CHECK(g_usb_iso_timing.count == 0U);
        return 0U;
    }
    if (id == 8U || id == 13U)
    {
        g_usb_iso_timing.request = id == 8U ? 99U : 2U;
        UsbIsoTiming_Process();
        CHECK(g_usb_iso_timing.request == 0U);
        CHECK(g_usb_iso_timing.state == USB_ISO_TRACE_IDLE);
        CHECK(g_usb_iso_timing.rejected_requests == (id == 8U ? 1U : 0U));
        return 0U;
    }

    g_usb_iso_timing.request = 1U;
    UsbIsoTiming_Process();
    CHECK(g_usb_iso_timing.state == USB_ISO_TRACE_ACTIVE);
    device->DSTS = 7U << USB_OTG_DSTS_FNSOF_Pos;
    ep->DIEPCTL = 0x12345678U;
    ep->DIEPTSIZ = 1023U;
    ep->DTXFSTS = 256U;
    if (id == 3U)
        TraceTest_Context(16U);
    if (id == 9U)
        __disable_irq();
    if (id == 17U)
        TIM6->CR1 = 0U;
    if (id == 18U)
        TIM6->SR = TIM_SR_UIF;
    if (id == 19U)
        RCC->APB1LLPENR &= ~RCC_APB1LLPENR_TIM6LPEN;
    UsbIsoTiming_Record(USB_ISO_TIME_SUBMIT, 9U, 1023U);
    CHECK(g_usb_iso_timing.count == 1U && g_usb_iso_timing.writers == 0U);
    CHECK(s_usb_iso_records[0].event == USB_ISO_TIME_SUBMIT);
    CHECK(s_usb_iso_records[0].frame == 7U && s_usb_iso_records[0].sequence == 9U);
    CHECK(s_usb_iso_records[0].detail == 1023U);
    if (id == 17U || id == 19U)
        CHECK(s_usb_iso_records[0].time_flags == 8U);
    else
        CHECK((s_usb_iso_records[0].time_flags & 1U) != 0U);
    if (id == 18U)
        CHECK((s_usb_iso_records[0].time_flags & 2U) != 0U);
    CHECK(s_usb_iso_records[0].diepctl == 0x12345678U);
    CHECK(s_usb_iso_records[0].dieptsiz == 1023U && s_usb_iso_records[0].fifo_words == 256U);
    CHECK(s_usb_iso_records[0].ipsr == (id == 3U ? 16U : 0U));
    if (id == 3U)
        TraceTest_Context(0U);
    if (id == 9U)
    {
        CHECK(__get_PRIMASK() == 1U);
        __enable_irq();
    }
    if (id == 2U || id == 3U || id == 9U || id >= 17U)
        return 0U;
    if (id == 7U)
    {
        g_usb_iso_timing.request = 1U;
        UsbIsoTiming_Process();
        CHECK(g_usb_iso_timing.rejected_requests == 1U);
        CHECK(g_usb_iso_timing.count == 1U);
        return 0U;
    }
    if (id == 15U)
    {
        DWT->CYCCNT = 0xfffffff0U;
        g_usb_iso_timing.start_cycles = DWT->CYCCNT;
        UsbIsoTiming_Record(USB_ISO_TIME_SUBMIT, 10U, 1023U);
        CHECK((uint32_t)(s_usb_iso_records[1].cycles - g_usb_iso_timing.start_cycles) < 10000U);
        return 0U;
    }
    if (id == 4U || id == 6U || id == 16U)
    {
        g_usb_iso_timing.request = 2U;
        if (id == 16U)
            g_usb_iso_timing.writers = 1U;
        UsbIsoTiming_Process();
        if (id == 16U)
        {
            CHECK(g_usb_iso_timing.state == USB_ISO_TRACE_FROZEN);
            g_usb_iso_timing.writers = 0U;
            UsbIsoTiming_Process();
        }
        CHECK(g_usb_iso_timing.state == USB_ISO_TRACE_READY && g_usb_iso_timing.stop_reason == 2U);
        if (id == 6U)
        {
            g_usb_iso_timing.request = 1U;
            UsbIsoTiming_Process();
            CHECK(g_usb_iso_timing.count == 0U);
            UsbIsoTiming_Record(USB_ISO_TIME_SUBMIT, 20U, 512U);
            CHECK(g_usb_iso_timing.count == 1U && s_usb_iso_records[0].sequence == 20U);
        }
        return 0U;
    }
    if (id == 14U)
        g_usb_iso_timing.capacity = UINT32_MAX;
    if (id == 10U)
        SCB->CCR |= SCB_CCR_DC_Msk;
    for (i = 1U; i < 512U; i++)
        UsbIsoTiming_Record(USB_ISO_TIME_SUBMIT, i + 9U, 1023U);
    CHECK(g_usb_iso_timing.count == 512U);
    CHECK(g_usb_iso_timing.state == USB_ISO_TRACE_FROZEN && g_usb_iso_timing.stop_reason == 1U);
    UsbIsoTiming_Record(USB_ISO_TIME_SUBMIT, 1000U, 1023U);
    CHECK(g_usb_iso_timing.count == 512U);
    if (id == 10U)
    {
        TraceTest_Context(16U);
        UsbIsoTiming_Process();
        CHECK(g_usb_iso_timing.state == USB_ISO_TRACE_FROZEN);
        TraceTest_Context(0U);
    }
    UsbIsoTiming_Process();
    CHECK(g_usb_iso_timing.state == USB_ISO_TRACE_READY);
#else
    (void)id;
    CHECK(APP_USB_ISO_TIMING_TEST_ENABLED == 0);
#endif
    return 0U;
}
