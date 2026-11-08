//独立USB时序采样，只观察现有控制流，不执行设备业务
#include "usb_iso_timing.h"
#include "stm32h7xx.h"
#include <string.h>

#if APP_USB_ISO_TIMING_TEST_ENABLED

//控制区使用DTCM，调试器写request可由CPU直接观察
volatile UsbIsoTimingDebug_t g_usb_iso_timing;

//记录区只由CPU访问，冻结后统一Clean供调试器导出
static volatile UsbIsoTimingRecord_t s_usb_iso_records[USB_ISO_TIMING_CAPACITY]
    __attribute__((section(".usb_iso_timing"), zero_init, aligned(32)));

//锁定记录格式，避免导出脚本按错误偏移解释
typedef char UsbIsoTiming_SizeCheck[(sizeof(UsbIsoTimingRecord_t) == 48U) ? 1 : -1];

//HAL Tick由项目原TIM6中断维护，采样器只读取
extern volatile uint32_t uwTick;

//只读现有1MHz TIM6，形成可跨WFI的有界墙钟快照
static uint32_t UsbIsoTiming_ReadWall(uint32_t *flags);

//准备周期计数器与诊断头
void UsbIsoTiming_Init(void)
{
    uint32_t before;

    //初始化只在USB准备阶段执行，记录内容按count判定有效，不清扫整个数组
    memset((void *)&g_usb_iso_timing, 0, sizeof(g_usb_iso_timing));
    g_usb_iso_timing.magic = 0x5554494DUL;
    g_usb_iso_timing.version = 2U;
    g_usb_iso_timing.capacity = USB_ISO_TIMING_CAPACITY;
    g_usb_iso_timing.record_bytes = sizeof(UsbIsoTimingRecord_t);
    g_usb_iso_timing.buffer_address = (uint32_t)s_usb_iso_records;
    g_usb_iso_timing.buffer_bytes = sizeof(s_usb_iso_records);
    g_usb_iso_timing.cpu_hz = SystemCoreClock;

    //SCCB也使用DWT，只使能、不清零，不改变其他模块延时基准
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    before = DWT->CYCCNT;
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) != 0U || DWT->CYCCNT == before || SystemCoreClock == 0U)
    {
        g_usb_iso_timing.state = USB_ISO_TRACE_ERROR;
        return;
    }
    g_usb_iso_timing.enabled = 1U;
}

//处理手动采样与冻结收尾
void UsbIsoTiming_Process(void)
{
    uint32_t mask, request;

    //Cache维护只在任务上下文执行，不在USB ISR内扫描记录
    if (__get_IPSR() != 0U || g_usb_iso_timing.enabled == 0U)
        return;

    //未触发且无需收尾时直接返回，不在每次UsbTask唤醒时进入临界区
    if (g_usb_iso_timing.request == 0U && g_usb_iso_timing.state != USB_ISO_TRACE_FROZEN)
        return;
    mask = __get_PRIMASK();
    __disable_irq();
    request = g_usb_iso_timing.request;
    g_usb_iso_timing.request = 0U;
    if (request == 1U)
    {

        //运行中不重置索引，避免旧写入污染新一轮
        if (g_usb_iso_timing.state == USB_ISO_TRACE_ACTIVE || g_usb_iso_timing.writers != 0U)
            g_usb_iso_timing.rejected_requests++;
        else
        {
            g_usb_iso_timing.count = 0U;
            g_usb_iso_timing.stop_reason = 0U;
            g_usb_iso_timing.start_cycles = DWT->CYCCNT;
            g_usb_iso_timing.end_cycles = 0U;
            __DMB();
            g_usb_iso_timing.state = USB_ISO_TRACE_ACTIVE;
        }
    }
    else if (request == 2U)
    {
        if (g_usb_iso_timing.state == USB_ISO_TRACE_ACTIVE)
        {
            g_usb_iso_timing.state = USB_ISO_TRACE_FROZEN;
            g_usb_iso_timing.end_cycles = DWT->CYCCNT;
            g_usb_iso_timing.stop_reason = 2U;
        }
    }
    else if (request != 0U)
        g_usb_iso_timing.rejected_requests++;
    __set_PRIMASK(mask);

    //所有任务与IRQ写入结束后再发布READY，不交付半条记录
    if (g_usb_iso_timing.state == USB_ISO_TRACE_FROZEN && g_usb_iso_timing.writers == 0U)
    {
        __DMB();
        if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
            SCB_CleanDCache_by_Addr((uint32_t *)s_usb_iso_records, sizeof(s_usb_iso_records));
        __DSB();
        g_usb_iso_timing.state = USB_ISO_TRACE_READY;
    }
}

//记录一次时序与EP1硬件状态
void UsbIsoTiming_Record(uint32_t event, uint32_t sequence, uint32_t detail)
{
    uint32_t mask, index, cycles;
    volatile UsbIsoTimingRecord_t *record;
    USB_OTG_DeviceTypeDef *device;
    USB_OTG_INEndpointTypeDef *ep;

    //未手动触发时立即返回，异常事件编号不进入记录
    if (g_usb_iso_timing.state != USB_ISO_TRACE_ACTIVE || event == 0U || event > USB_ISO_TIME_STACK_END)
        return;
    cycles = DWT->CYCCNT;
    mask = __get_PRIMASK();
    __disable_irq();
    if (g_usb_iso_timing.state != USB_ISO_TRACE_ACTIVE ||
        g_usb_iso_timing.count >= USB_ISO_TIMING_CAPACITY)
    {
        __set_PRIMASK(mask);
        return;
    }

    //短临界区只预留独占槽位，寄存器读取和记录写入放在外面
    index = g_usb_iso_timing.count++;
    g_usb_iso_timing.writers++;
    if (g_usb_iso_timing.count == USB_ISO_TIMING_CAPACITY)
    {
        g_usb_iso_timing.state = USB_ISO_TRACE_FROZEN;
        g_usb_iso_timing.end_cycles = cycles;
        g_usb_iso_timing.stop_reason = 1U;
    }
    __set_PRIMASK(mask);

    //快照只读USB寄存器，不应答中断、不改变端点或FIFO
    device = (USB_OTG_DeviceTypeDef *)(USB2_OTG_FS_PERIPH_BASE + USB_OTG_DEVICE_BASE);
    ep = (USB_OTG_INEndpointTypeDef *)(USB2_OTG_FS_PERIPH_BASE + USB_OTG_IN_ENDPOINT_BASE + USB_OTG_EP_REG_SIZE);
    record = &s_usb_iso_records[index];
    record->event = 0U;
    record->cycles = cycles;
    record->frame = (device->DSTS & USB_OTG_DSTS_FNSOF_Msk) >> USB_OTG_DSTS_FNSOF_Pos;
    record->sequence = sequence;
    record->detail = detail;
    record->diepctl = ep->DIEPCTL;
    record->dieptsiz = ep->DIEPTSIZ;
    record->fifo_words = ep->DTXFSTS;
    record->systick_val = SysTick->VAL;
    record->ipsr = __get_IPSR();
    {
        uint32_t flags;
        record->wall_us = UsbIsoTiming_ReadWall(&flags);
        record->time_flags = flags;
    }
    __DMB();
    record->event = event;

    //最后完成标记与写者计数提交后，任务侧才允许导出
    mask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    g_usb_iso_timing.writers--;
    __set_PRIMASK(mask);
}
//读取已有HAL时间基准，不配置或停止定时器
static uint32_t UsbIsoTiming_ReadWall(uint32_t *flags)
{
    uint32_t first, last, pending_first, pending_last, count, attempt;

    //TIM6必须保留原1ms更新和Sleep时钟，否则不伪造有效时间
    if ((TIM6->CR1 & TIM_CR1_CEN) == 0U || (TIM6->DIER & TIM_DIER_UIE) == 0U ||
        TIM6->ARR != 999U || (RCC->APB1LLPENR & RCC_APB1LLPENR_TIM6LPEN) == 0U)
    {
        *flags = 8U;
        return 0U;
    }

    //最多读取两次，不在IRQ内等待边界稳定
    for (attempt = 0U; attempt < 2U; attempt++)
    {
        first = uwTick;
        pending_first = TIM6->SR & TIM_SR_UIF;
        count = TIM6->CNT;
        pending_last = TIM6->SR & TIM_SR_UIF;
        last = uwTick;
        if (first == last && pending_first == pending_last && count <= 999U)
        {
            *flags = 1U | (pending_last != 0U ? 2U : 0U);
            return (first + (pending_last != 0U ? 1U : 0U)) * 1000U + count;
        }
    }
    *flags = 4U;
    return 0U;
}

#endif
