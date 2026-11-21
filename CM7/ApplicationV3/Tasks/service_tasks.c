#include "../Core/app_services.h"
#include "app_tasks.h"
#include "app_events.h"
#include "platform_sync.h"
#include "service_tasks.h"
#include "board_smoke.h"
#include "../Runtime/service_event_loop.h"
#include "../Runtime/service_events.h"
#include <stddef.h>

#define STARTUP_DEADLINE_MS 1000U
#define STARTUP_POLL_MS 100U
#define STARTUP_MAX_ATTEMPTS (1U + STARTUP_DEADLINE_MS / STARTUP_POLL_MS)

//保留原Control静态栈及各目标任务的创建状态
static StackType_t s_control_stack[768U] __attribute__((aligned(8)));
static StaticTask_t s_task_controls[RUNTIME_TARGET_COUNT];
static uint32_t s_task_attempted;

//在服务所属任务内等待BOOT、准备实例并提交结果
void AppV3_ServiceRun(void *argument, AppV3ServiceMailbox_t *mailbox, uint32_t (*advance)(void))
{
    const AppV3ThreadSpec_t *thread = argument;
    SysResult_t initialization = SYS_ERR_INVALID_STATE;
    uint32_t events;
    uint32_t report_attempts = 0U;
    bool report_pending = false;
    bool boot_seen = false;


    for (;;)
    {
        uint32_t wait_ms = report_pending ? STARTUP_POLL_MS : APP_EVENTS_WAIT_FOREVER;
        SysResult_t result;

        //仅在本服务发布成功后推进其私有队列，未发布时只等待BOOT或重试报告
        if (initialization == SYS_OK && !report_pending)
        {
            //硬件推进发生在Service任务内，结果队列背压不会阻止必要的停止推进
            if (AppV3_BusinessReady())
                wait_ms = advance();
            result = AppV3Mailbox_Flush(mailbox);
            if (result == SYS_OK)
                result = AppV3EventLoop_Step(&mailbox->loop, wait_ms, &events);
            if (result != SYS_OK && result != SYS_ERR_TIMEOUT)
                vTaskDelay(1U);
            continue;
        }

        result = App_EventsWait(thread->owner, wait_ms, &events);
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

#if APP_V3_BOARD_SMOKE != APP_V3_BOARD_SMOKE_OFF
#if APP_V3_BOARD_SMOKE == APP_V3_BOARD_SMOKE_CAMERA
#include "../Services/Camera/camera_service.h"
#define BOARD_SMOKE_SERVICE RUNTIME_TARGET_CAMERA
#elif APP_V3_BOARD_SMOKE == APP_V3_BOARD_SMOKE_USB
#include "../Services/USB/usb_service.h"
#define BOARD_SMOKE_SERVICE RUNTIME_TARGET_USB
#else
#include "../Services/Storage/storage_service.h"
#define BOARD_SMOKE_SERVICE RUNTIME_TARGET_STORAGE
#endif

volatile AppV3BoardSmokeReport_t g_app_v3_board_smoke;
static uint32_t s_smoke_ticket;
static TickType_t s_smoke_started;

//用无符号Tick差值限制每步的提交与等待总时间
static bool BoardSmoke_Expired(void)
{
    return (TickType_t)(xTaskGetTickCount() - s_smoke_started) >=
        pdMS_TO_TICKS(APP_V3_BOARD_SMOKE_TIMEOUT_MS);
}

//只保存当前步骤的真实终结；同步回调不提交命令
static SysResult_t BoardSmoke_Record(const AppV3ServiceEvent_t *event, void *context)
{
    uint32_t step = g_app_v3_board_smoke.step;
    (void)context;

    //同一Service的模拟结果、其他请求者及迟到结果均不推进本测试
    if (g_app_v3_board_smoke.state == APP_V3_SMOKE_WAIT &&
        g_app_v3_board_smoke.received == step &&
        event->service == BOARD_SMOKE_SERVICE && event->requester == RUNTIME_TARGET_CONTROL &&
        event->real_hardware && event->command_id == APP_V3_BOARD_SMOKE_FIRST_ID + step &&
        !BoardSmoke_Expired())
    {
        g_app_v3_board_smoke.events[step] = *event;
#if APP_V3_BOARD_STRESS != APP_V3_BOARD_STRESS_OFF
        g_app_v3_board_smoke.latency_ticks[step] = (TickType_t)(xTaskGetTickCount() - s_smoke_started);
#endif
        g_app_v3_board_smoke.received = step + 1U;
    }
    return SYS_OK;
}

//断连后先查询FAULT，复接后经原RECOVER确认退出，再验证同一服务可重启
#if APP_V3_BOARD_STRESS == APP_V3_BOARD_STRESS_RECOVERY
typedef enum
{
    BOARD_SMOKE_START = 0,
    BOARD_SMOKE_QUERY,
    BOARD_SMOKE_STOP,
    BOARD_SMOKE_QUERY_STOPPED,
    BOARD_SMOKE_RECOVER
} BoardSmokeOperationIndex_t;

static const BoardSmokeOperationIndex_t s_recovery_operations[APP_V3_BOARD_SMOKE_STEPS] =
{
    BOARD_SMOKE_START, BOARD_SMOKE_QUERY, BOARD_SMOKE_QUERY, BOARD_SMOKE_RECOVER,
    BOARD_SMOKE_START, BOARD_SMOKE_QUERY, BOARD_SMOKE_STOP, BOARD_SMOKE_QUERY_STOPPED
};
#endif

//只在当前构建中引用一个服务；Storage使用原只读START
static SysResult_t BoardSmoke_Request(uint32_t step)
{
    uint32_t command_id = APP_V3_BOARD_SMOKE_FIRST_ID + step;
#if APP_V3_BOARD_STRESS == APP_V3_BOARD_STRESS_RECOVERY
    uint32_t operation_index = s_recovery_operations[step];
#else
    uint32_t operation_index = step % APP_V3_BOARD_SMOKE_CYCLE_STEPS;
#endif
#if APP_V3_BOARD_SMOKE == APP_V3_BOARD_SMOKE_CAMERA
    static const CameraServiceOperation_t operations[] =
    {
        CAMERA_SERVICE_START, CAMERA_SERVICE_HARDWARE_QUERY,
        CAMERA_SERVICE_STOP, CAMERA_SERVICE_HARDWARE_QUERY
    };
    return CameraService_Request(command_id, operations[operation_index], 0U);
#elif APP_V3_BOARD_SMOKE == APP_V3_BOARD_SMOKE_USB
    static const UsbServiceOperation_t operations[] =
    {
        USB_SERVICE_START, USB_SERVICE_HARDWARE_QUERY,
        USB_SERVICE_STOP, USB_SERVICE_HARDWARE_QUERY, USB_SERVICE_RECOVER
    };
    return UsbService_Request(command_id, operations[operation_index], 0U);
#else
    static const StorageServiceOperation_t operations[] =
    {
        STORAGE_SERVICE_START, STORAGE_SERVICE_HARDWARE_QUERY,
        STORAGE_SERVICE_STOP, STORAGE_SERVICE_HARDWARE_QUERY, STORAGE_SERVICE_RECOVER
    };
    return StorageService_Request(command_id, operations[operation_index], 0U);
#endif
}

//完成状态与首错要来自本步骤的真实结果，不靠等待时长推断资源释放
static SysResult_t BoardSmoke_CheckResult(uint32_t step)
{
    const volatile AppV3ServiceEvent_t *event = &g_app_v3_board_smoke.events[step];
    AppV3HardwareState_t expected;

#if APP_V3_BOARD_STRESS == APP_V3_BOARD_STRESS_RECOVERY
    if (step == 2U)
    {
        //USB查询返回快照；Storage查询仍可能直接报告卡不存在
        if (event->hardware.state != APP_V3_HW_FAULT ||
            (event->result != SYS_OK && event->result != SYS_ERR_NOT_FOUND) ||
            event->hardware.first_fault == SYS_OK)
            return SYS_ERR_PROTOCOL;
        return SYS_OK;
    }
    if (step == 3U && event->result == SYS_OK &&
        event->hardware.first_fault != g_app_v3_board_smoke.events[2].hardware.first_fault)
        return SYS_ERR_PROTOCOL;
    if (step >= 4U && event->result == SYS_OK && event->hardware.first_fault != SYS_OK)
        return event->hardware.first_fault;
    expected = (step < 2U || step == 4U || step == 5U) ? APP_V3_HW_ACTIVE : APP_V3_HW_READY;
#else
#if APP_V3_BOARD_STRESS == APP_V3_BOARD_STRESS_REPEAT
    //观察期间的异步FAULT即使后来STOP成功，也不能算无故障运行
    if (event->result == SYS_OK && event->hardware.first_fault != SYS_OK)
        return event->hardware.first_fault;
#endif
    expected = step % APP_V3_BOARD_SMOKE_CYCLE_STEPS < 2U ? APP_V3_HW_ACTIVE : APP_V3_HW_READY;
#endif
    if (event->result != SYS_OK)
        return event->result;
    return event->hardware.state == expected ? SYS_OK : SYS_ERR_PROTOCOL;
}

#if APP_V3_BOARD_SMOKE == APP_V3_BOARD_SMOKE_USB || APP_V3_BOARD_STRESS != APP_V3_BOARD_STRESS_OFF
//观察窗口只在收到上一条成功终结后开启，期间不提交下一条命令
static uint32_t BoardSmoke_ObserveMs(uint32_t next_step)
{
#if APP_V3_BOARD_STRESS == APP_V3_BOARD_STRESS_RECOVERY
    if (next_step == 2U || next_step == 3U)
        return APP_V3_BOARD_STRESS_OPERATOR_MS;
#if APP_V3_BOARD_SMOKE == APP_V3_BOARD_SMOKE_USB
    if (next_step == 6U)
        return APP_V3_BOARD_SMOKE_USB_OBSERVE_MS;
#endif
#else
    if (next_step % APP_V3_BOARD_SMOKE_CYCLE_STEPS == 2U)
    {
#if APP_V3_BOARD_STRESS == APP_V3_BOARD_STRESS_REPEAT
        if (next_step + 2U == APP_V3_BOARD_SMOKE_STEPS)
            return APP_V3_BOARD_STRESS_SOAK_MS;
#endif
#if APP_V3_BOARD_SMOKE == APP_V3_BOARD_SMOKE_USB
        return APP_V3_BOARD_SMOKE_USB_OBSERVE_MS;
#endif
    }
#endif
    return 0U;
}
#endif

//计算测试结果后撤销订阅；不清理或推断任何硬件所有权
static void BoardSmoke_Finish(AppV3BoardSmokeState_t state, SysResult_t result)
{
#if APP_V3_BOARD_STRESS != APP_V3_BOARD_STRESS_OFF
    UBaseType_t remaining_words[RUNTIME_TARGET_COUNT];
    uint32_t target_index;

    //终局只采样一次；读取失败单独保留，不能覆盖真实业务首错
    if (App_EventsReadStackWatermarks(remaining_words))
    {
        for (target_index = 0U; target_index < RUNTIME_TARGET_COUNT; ++target_index)
            g_app_v3_board_smoke.stack_remaining_words[target_index] = remaining_words[target_index];
        g_app_v3_board_smoke.stack_sampled = true;
    }
#endif
    g_app_v3_board_smoke.result = result;
    g_app_v3_board_smoke.state = state;
    if (s_smoke_ticket != 0U)
    {
        SysResult_t unsubscribe = AppV3Business_Unsubscribe(s_smoke_ticket);
        //本函数在Drain之外由订阅者Control调用，票据从未交给其他代码
        configASSERT(unsubscribe == SYS_OK);
        (void)unsubscribe;
        s_smoke_ticket = 0U;
    }
}

//只在Control完成本轮Drain后推进；每轮最多提交一个已有命令
static void BoardSmoke_Poll(void)
{
    SysResult_t result;
    uint32_t step = g_app_v3_board_smoke.step;

    if (g_app_v3_board_smoke.state == APP_V3_SMOKE_IDLE)
    {
        g_app_v3_board_smoke.service = BOARD_SMOKE_SERVICE;
        result = AppV3Business_Subscribe(BOARD_SMOKE_SERVICE, BoardSmoke_Record, NULL, &s_smoke_ticket);
        if (result != SYS_OK)
        {
            BoardSmoke_Finish(APP_V3_SMOKE_FAILED, result);
            return;
        }
        s_smoke_started = xTaskGetTickCount();
        g_app_v3_board_smoke.state = APP_V3_SMOKE_SUBMIT;
    }

    //推进下一步前同时核对原操作结果与服务报告的运行状态
    if (g_app_v3_board_smoke.state == APP_V3_SMOKE_WAIT && g_app_v3_board_smoke.received > step)
    {
        result = BoardSmoke_CheckResult(step);
        if (result != SYS_OK)
        {
            BoardSmoke_Finish(APP_V3_SMOKE_FAILED, result);
            return;
        }
        g_app_v3_board_smoke.step = ++step;
        if (step == APP_V3_BOARD_SMOKE_STEPS)
        {
            BoardSmoke_Finish(APP_V3_SMOKE_COMPLETE, SYS_OK);
            return;
        }
        s_smoke_started = xTaskGetTickCount();
#if APP_V3_BOARD_SMOKE == APP_V3_BOARD_SMOKE_USB || APP_V3_BOARD_STRESS != APP_V3_BOARD_STRESS_OFF
        if (BoardSmoke_ObserveMs(step) != 0U)
        {
            g_app_v3_board_smoke.state = APP_V3_SMOKE_OBSERVE;
            return;
        }
#endif
        g_app_v3_board_smoke.state = APP_V3_SMOKE_SUBMIT;
    }

#if APP_V3_BOARD_SMOKE == APP_V3_BOARD_SMOKE_USB || APP_V3_BOARD_STRESS != APP_V3_BOARD_STRESS_OFF
    if (g_app_v3_board_smoke.state == APP_V3_SMOKE_OBSERVE)
    {
        //沿用原Control轮询，观察期间不阻塞任务，也不提前提交STOP
        TickType_t now = xTaskGetTickCount();
        if ((TickType_t)(now - s_smoke_started) < pdMS_TO_TICKS(BoardSmoke_ObserveMs(step)))
            return;

        //窗口结束后才开始计算STOP自身的提交与完成期限
        s_smoke_started = now;
        g_app_v3_board_smoke.state = APP_V3_SMOKE_SUBMIT;
    }
#endif

    //仅未成功入队的BUSY可以重试，重试沿用原命令编号
    if (g_app_v3_board_smoke.state != APP_V3_SMOKE_SUBMIT &&
        g_app_v3_board_smoke.state != APP_V3_SMOKE_WAIT)
        return;
    if (BoardSmoke_Expired())
    {
        BoardSmoke_Finish(APP_V3_SMOKE_TIMED_OUT, SYS_ERR_TIMEOUT);
        return;
    }
    if (g_app_v3_board_smoke.state != APP_V3_SMOKE_SUBMIT)
        return;
    result = BoardSmoke_Request(step);
    if (result == SYS_OK)
        g_app_v3_board_smoke.state = APP_V3_SMOKE_WAIT;
    else if (result != SYS_ERR_BUSY)
        BoardSmoke_Finish(APP_V3_SMOKE_FAILED, result);
}
#endif

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
        uint32_t business_consumed = 0U;

        //启动成功后单独推进业务结果。非法消息与回调错误保留在业务诊断中
        if (AppV3_BusinessReady())
        {
            (void)AppV3Business_Drain(&business_consumed);
#if APP_V3_BOARD_SMOKE != APP_V3_BOARD_SMOKE_OFF
            BoardSmoke_Poll();
#endif
        }
        if (business_consumed >= 4U)
            vTaskDelay(1U);

        //漏通知时按原周期回查；业务仍有积压时先取通知再继续消费
        result = AppV3EventLoop_Step(&loop, business_consumed != 0U ? 0U : STARTUP_POLL_MS, &events);
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
