#include "../Core/app_services.h"
#include "app_tasks.h"
#include "app_events.h"
#include "platform_sync.h"
#include "service_tasks.h"
#include "../Runtime/service_event_loop.h"
#include <stddef.h>

static StackType_t s_control_stack[768U] __attribute__((aligned(8)));
static StaticTask_t s_task_controls[RUNTIME_TARGET_COUNT];
static uint32_t s_task_attempted;

void AppV3_ServiceRun(void *argument)
{
    const AppV3ThreadSpec_t *thread = argument;
    SysResult_t initialization = SYS_ERR_INVALID_STATE;
    uint32_t events;
    bool report_pending = false;
    bool boot_seen = false;
    AppV3EventLoop_t loop = {0};
    SysResult_t loop_status = AppV3EventLoop_Init(&loop, thread->owner);

    //服务在自己线程内建立循环，初始化失败时不进入任何业务分发。
    configASSERT(loop_status == SYS_OK);
    while (loop_status != SYS_OK)
        vTaskDelay(portMAX_DELAY);

    for (;;)
    {
        //Storage 没有旧控制消费唤醒，待发期间定时检查队列空间。
        SysResult_t result = AppV3EventLoop_Step(&loop,
            report_pending ? 100U : APP_EVENTS_WAIT_FOREVER, &events);
        if (result != SYS_OK && result != SYS_ERR_TIMEOUT)
        {
            configASSERT(result == SYS_OK);
            vTaskDelay(1U);
            continue;
        }

        //任何普通通知都不能触发重复初始化。
        if (!boot_seen && result == SYS_OK && (events & thread->boot_events) == thread->boot_events)
        {
            boot_seen = true;
            initialization = AppV3_InitService(thread->owner, AppV3_GetContext());
            report_pending = true;
        }

        //队列满时保留原结果。入队成功后只由 Control 消费。
        if (report_pending)
            report_pending = AppV3_ReportInitialization(thread->owner, initialization) != SYS_OK;
    }
}

static void AppV3_ControlRun(void *argument)
{
    SysResult_t result = App_TasksReleaseBoot();
    uint32_t events;
    (void)argument;

    //原 BOOT 屏障失败时进入停驻，不执行服务调度。
    configASSERT(result == SYS_OK);
    while (result != SYS_OK)
        vTaskDelay(portMAX_DELAY);

    for (;;)
    {
        //保留原 Control 的 100 ms 周期。即使通知失败，已入队的报告仍会被取出。
        result = App_EventsWait(RUNTIME_TARGET_CONTROL, 100U, &events);
        if (result == SYS_OK || result == SYS_ERR_TIMEOUT)
        {
            result = AppV3_DrainInitialization();
            configASSERT(result == SYS_OK);
        }
        else
        {
            //异常等待不能空转占住其他服务的 CPU 时间。
            vTaskDelay(1U);
        }
    }
}

SysResult_t AppV3_CreateThread(const AppV3ThreadSpec_t *thread)
{
    TaskHandle_t handle;
    uint32_t owner_bit;

    if (thread == NULL || (uint32_t)thread->owner >= RUNTIME_TARGET_COUNT)
        return SYS_ERR_INVALID_ARG;

    //在调度前独占初始化；失败后保留已尝试事实，避免再次使用 TCB。
    if (Platform_SyncGetException() != 0U || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;
    owner_bit = 1UL << thread->owner;
    if ((s_task_attempted & owner_bit) != 0U)
        return SYS_ERR_ALREADY_EXISTS;

    s_task_attempted |= owner_bit;
    handle = xTaskCreateStatic(thread->entry, thread->name, thread->stack_words,
        (void *)thread, thread->priority, thread->stack, &s_task_controls[thread->owner]);
    if (handle == NULL)
        return SYS_ERR_NO_RESOURCE;

    return App_EventsAttach(thread->owner, handle);
}

SysResult_t ControlTask_Create(void)
{
    static const AppV3ThreadSpec_t control =
    {
        RUNTIME_TARGET_CONTROL, "Control", AppV3_ControlRun, s_control_stack,
        sizeof(s_control_stack) / sizeof(s_control_stack[0]), 2U, 0U
    };
    return AppV3_CreateThread(&control);
}
