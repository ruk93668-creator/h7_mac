#include "app_events.h"
#include "usb_device.h"
#include "stm32h7xx.h"
#include <stddef.h>

static TaskHandle_t s_tasks[RUNTIME_TARGET_COUNT];
static uint32_t s_pending[RUNTIME_TARGET_COUNT];
static bool s_initialized;
static bool s_prepared;
static bool s_failed;
static volatile bool s_boot_ready;

//普通线程通知，启动前仅保存提示
static SysResult_t AppEvents_Post(void *context, RuntimeEventTarget_t target, uint32_t events);

//合规外设中断通知，必要时请求任务切换
static SysResult_t AppEvents_PostFromIsr(void *context, RuntimeEventTarget_t target, uint32_t events);

//USB通知只唤醒UsbTask，不处理协议或图像
static void AppEvents_UsbNotify(uint32_t events, void *context);

//在短临界区内检查状态并锁存启动前提示
static SysResult_t AppEvents_PreparePost(RuntimeEventTarget_t target, uint32_t events, bool *deferred);

static const RuntimeEventPort_t s_event_port =
{
    .post = AppEvents_Post,
    .post_from_isr = AppEvents_PostFromIsr,
    .context = NULL
};

SysResult_t App_EventsInit(void)
{
    SysResult_t result;

    if (__get_IPSR() != 0U || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;
    if (s_initialized)
        return SYS_ERR_ALREADY_EXISTS;

    result = USB_Device_SetEventCallback(AppEvents_UsbNotify, NULL);
    if (result != SYS_OK)
        return result;

    result = RuntimeEvent_Bind(&s_event_port);
    if (result != SYS_OK)
        return result;

    s_initialized = true;
    return SYS_OK;
}

SysResult_t App_EventsAttach(RuntimeEventTarget_t target, TaskHandle_t task)
{
    if ((uint32_t)target >= RUNTIME_TARGET_COUNT || task == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!s_initialized || s_failed || __get_IPSR() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;
    if (s_tasks[target] != NULL)
        return SYS_ERR_ALREADY_EXISTS;

    s_tasks[target] = task;
    return SYS_OK;
}

SysResult_t App_EventsRecordInitialization(SysResult_t result)
{
    uint32_t i;

    if (!s_initialized || s_prepared || s_failed || __get_IPSR() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;
    if (result != SYS_OK)
    {
        s_failed = true;
        return result;
    }

    for (i = 0U; i < RUNTIME_TARGET_COUNT; i++)
    {
        if (s_tasks[i] == NULL)
            return SYS_ERR_INVALID_STATE;
    }

    s_prepared = true;
    return SYS_OK;
}

SysResult_t App_EventsReleaseBoot(void)
{
    uint32_t i;

    if (__get_IPSR() != 0U || !s_prepared || s_failed ||
        xTaskGetSchedulerState() != taskSCHEDULER_RUNNING ||
        xTaskGetCurrentTaskHandle() != s_tasks[RUNTIME_TARGET_CONTROL])
        return SYS_ERR_INVALID_STATE;
    if (s_boot_ready)
        return SYS_ERR_ALREADY_EXISTS;

    //先完整提交屏障，再让高优先级任务执行
    taskENTER_CRITICAL();
    s_boot_ready = true;
    for (i = 0U; i < RUNTIME_TARGET_COUNT; i++)
    {
        (void)xTaskNotify(s_tasks[i], RUNTIME_EVENT_BOOT_READY | s_pending[i], eSetBits);
        s_pending[i] = 0U;
    }
    taskEXIT_CRITICAL();
    return SYS_OK;
}

SysResult_t App_EventsWait(RuntimeEventTarget_t target, uint32_t timeout_ms, uint32_t *events)
{
    TickType_t ticks;

    if ((uint32_t)target >= RUNTIME_TARGET_COUNT || events == NULL)
        return SYS_ERR_INVALID_ARG;
    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U || __get_BASEPRI() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_RUNNING ||
        xTaskGetCurrentTaskHandle() != s_tasks[target])
        return SYS_ERR_INVALID_STATE;

    ticks = timeout_ms == APP_EVENTS_WAIT_FOREVER ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (timeout_ms != 0U && ticks == 0U)
        ticks = 1U;

    *events = 0U;
    //不清入口通知位，保留等待前已经到达的事件
    return xTaskNotifyWait(0U, UINT32_MAX, events, ticks) == pdTRUE
        ? SYS_OK : SYS_ERR_TIMEOUT;
}

//内部功能函数实现
static SysResult_t AppEvents_PreparePost(RuntimeEventTarget_t target, uint32_t events, bool *deferred)
{
    uint32_t primask = __get_PRIMASK();
    SysResult_t result = SYS_OK;

    __disable_irq();
    if (!s_initialized || s_failed)
        result = SYS_ERR_INVALID_STATE;
    else if (!s_boot_ready)
    {
        s_pending[target] |= events;
        *deferred = true;
    }
    __set_PRIMASK(primask);
    return result;
}

static SysResult_t AppEvents_Post(void *context, RuntimeEventTarget_t target, uint32_t events)
{
    bool deferred = false;
    SysResult_t result;
    (void)context;

    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U ||
        (s_boot_ready && __get_BASEPRI() != 0U))
        return SYS_ERR_INVALID_STATE;

    result = AppEvents_PreparePost(target, events, &deferred);
    if (result != SYS_OK || deferred)
        return result;

    return xTaskNotify(s_tasks[target], events, eSetBits) == pdPASS ? SYS_OK : SYS_ERR_IO;
}

static SysResult_t AppEvents_PostFromIsr(void *context, RuntimeEventTarget_t target, uint32_t events)
{
    uint32_t ipsr = __get_IPSR();
    BaseType_t woken = pdFALSE;
    bool deferred = false;
    SysResult_t result;
    (void)context;

    //仅外设IRQ，优先级数值必须不小于内核API阈值
    if (ipsr < 16U || NVIC_GetPriority((IRQn_Type)(ipsr - 16U)) <
        configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY)
        return SYS_ERR_INVALID_STATE;

    result = AppEvents_PreparePost(target, events, &deferred);
    if (result != SYS_OK || deferred)
        return result;

    if (xTaskNotifyFromISR(s_tasks[target], events, eSetBits, &woken) != pdPASS)
        return SYS_ERR_IO;
    portYIELD_FROM_ISR(woken);
    return SYS_OK;
}

static void AppEvents_UsbNotify(uint32_t events, void *context)
{
    (void)events;
    (void)context;

    if (__get_IPSR() != 0U)
        (void)RuntimeEvent_PostFromIsr(RUNTIME_TARGET_USB, RUNTIME_EVENT_USB_PROGRESS);
    else
        (void)RuntimeEvent_Post(RUNTIME_TARGET_USB, RUNTIME_EVENT_USB_PROGRESS);
}
