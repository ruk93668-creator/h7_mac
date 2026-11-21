#include "../Core/app_services.h"
#include "app_tasks.h"
#include "app_events.h"
#include "platform_sync.h"
#include "service_tasks.h"
#include "../Runtime/service_event_loop.h"
#include <stddef.h>

#define STARTUP_DEADLINE_MS 1000U
#define STARTUP_POLL_MS 100U
#define STARTUP_MAX_ATTEMPTS (1U + STARTUP_DEADLINE_MS / STARTUP_POLL_MS)

//保留原Control静态栈及各目标任务的创建状态
static StackType_t s_control_stack[768U] __attribute__((aligned(8)));
static StaticTask_t s_task_controls[RUNTIME_TARGET_COUNT];
static uint32_t s_task_attempted;

//在服务所属任务内等待BOOT、准备实例并提交结果
void AppV3_ServiceRun(void *argument)
{
    const AppV3ThreadSpec_t *thread = argument;
    SysResult_t initialization = SYS_ERR_INVALID_STATE;
    uint32_t events;
    uint32_t report_attempts = 0U;
    bool report_pending = false;
    bool boot_seen = false;
    AppV3EventLoop_t loop = {.initialized = false};
    SysResult_t loop_status = AppV3EventLoop_Init(&loop, thread->owner);

    //服务在自己线程内建立循环，初始化失败时不进入任何业务分发。
    configASSERT(loop_status == SYS_OK);
    while (loop_status != SYS_OK)
        vTaskDelay(portMAX_DELAY);

    for (;;)
    {
        //Storage 没有旧控制消费唤醒，待发期间定时检查队列空间。
        SysResult_t result = AppV3EventLoop_Step(&loop,
            report_pending ? STARTUP_POLL_MS : APP_EVENTS_WAIT_FOREVER, &events);
        //等待异常时让出CPU，服务保持原启动结果
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
        {
            SysResult_t report = AppV3_ReportInitialization(thread->owner, initialization);
            ++report_attempts;
            report_pending = report == SYS_ERR_NO_RESOURCE && report_attempts < STARTUP_MAX_ATTEMPTS;
        }
    }
}

//用Tick差值判定启动期限，适配计数器回绕
static SysResult_t AppV3_CheckStartupDeadline(const TickType_t *started)
{
    if ((TickType_t)(xTaskGetTickCount() - *started) < pdMS_TO_TICKS(STARTUP_DEADLINE_MS))
        return SYS_OK;
    return AppV3_ExpireInitialization();
}

//Control收到队列可读提示后推进启动报告
static SysResult_t AppV3_StartupReadable(AppV3Queue_t queue, void *context)
{
    SysResult_t result;
    (void)queue;

    //先检查截止时间，再处理队列，避免晚到报告把超时改写为成功。
    result = AppV3_CheckStartupDeadline(context);
    if (result != SYS_OK)
        return result;
    return AppV3_DrainInitialization();
}

//协调BOOT屏障、启动结果消费和超时处理
static void AppV3_ControlRun(void *argument)
{
    uint32_t events;
    TickType_t started = xTaskGetTickCount();
    AppV3Queue_t startup_queue;
    AppV3EventLoop_t loop = {.initialized = false};
    SysResult_t result = AppV3EventLoop_Init(&loop, RUNTIME_TARGET_CONTROL);
    (void)argument;

    //控制入口先能接收结果，再释放原 BOOT 屏障。
    if (result == SYS_OK)
        result = AppV3_GetStartupQueue(&startup_queue);
    if (result == SYS_OK)
        result = AppV3EventLoop_Subscribe(&loop, startup_queue, AppV3_StartupReadable, &started);
    if (result == SYS_OK)
        result = App_TasksReleaseBoot();
    //任何准备错误都禁止该Control入口继续运行
    configASSERT(result == SYS_OK);
    while (result != SYS_OK)
        vTaskDelay(portMAX_DELAY);

    for (;;)
    {
        //保留原 Control 的 100 ms 周期。即使通知失败，已入队的报告仍会被取出。
        result = AppV3EventLoop_Step(&loop, STARTUP_POLL_MS, &events);
        if (result == SYS_OK || result == SYS_ERR_TIMEOUT)
        {
            result = AppV3_CheckStartupDeadline(&started);
            configASSERT(result == SYS_OK);
        }
        else
        {
            //异常等待不能空转占住其他服务的 CPU 时间。
            vTaskDelay(1U);
        }
    }
}

//使用静态栈与TCB创建唯一任务并登记通知入口
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

    //调用内核前冻结创建尝试，部分失败也不会重复使用TCB
    s_task_attempted |= owner_bit;
    handle = xTaskCreateStatic(thread->entry, thread->name, thread->stack_words,
        (void *)thread, thread->priority, thread->stack, &s_task_controls[thread->owner]);
    if (handle == NULL)
        return SYS_ERR_NO_RESOURCE;

    //只在任务实际创建成功后登记通知接收端
    return App_EventsAttach(thread->owner, handle);
}

//按原栈和优先级创建Control专用任务
SysResult_t ControlTask_Create(void)
{
    static const AppV3ThreadSpec_t control =
    {
        RUNTIME_TARGET_CONTROL, "Control", AppV3_ControlRun, s_control_stack,
        sizeof(s_control_stack) / sizeof(s_control_stack[0]), 2U, 0U
    };
    return AppV3_CreateThread(&control);
}
