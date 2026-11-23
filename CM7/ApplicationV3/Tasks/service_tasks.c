#include "../Core/app_services.h"
#include "app_tasks.h"
#include "app_events.h"
#include "platform_sync.h"
#include "service_tasks.h"
#include "supervisor.h"
#include "record_board.h"
#if APP_V3_RECORD_BOARD_TEST
#include "platform_time.h"
#endif
#include "../Services/Camera/camera_service.h"
#include "../Services/USB/usb_service.h"
#include "../Services/Storage/storage_service.h"
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

//由原ControlTask串行调度实际Service；表只描述当前已接入的依赖。
#define SUPERVISOR_TIMEOUT_MS 10000U

typedef enum
{
    SUPERVISOR_START,
    SUPERVISOR_STOP,
    SUPERVISOR_RECOVER,
    SUPERVISOR_QUERY
} SupervisorOperation_t;

typedef struct
{
    uint32_t bit;
    uint32_t provider;
    RuntimeEventTarget_t service;
    uint8_t operations[4];
} SupervisorService_t;

static const SupervisorService_t s_supervisor_services[] =
{
    {
        APP_V3_PIPE_CAMERA,
        0U,
        RUNTIME_TARGET_CAMERA,
        {CAMERA_SERVICE_START, CAMERA_SERVICE_STOP, CAMERA_SERVICE_RECOVER, CAMERA_SERVICE_HARDWARE_QUERY}
    },

    {
        APP_V3_PIPE_USB,
        APP_V3_PIPE_CAMERA,
        RUNTIME_TARGET_USB,
        {USB_SERVICE_START, USB_SERVICE_STOP, USB_SERVICE_RECOVER, USB_SERVICE_HARDWARE_QUERY}
    },

    {
        APP_V3_PIPE_RECORD,
        APP_V3_PIPE_CAMERA,
        RUNTIME_TARGET_STORAGE,
        {STORAGE_SERVICE_RECORD_START, STORAGE_SERVICE_RECORD_STOP, STORAGE_SERVICE_RECOVER, STORAGE_SERVICE_RECORD_QUERY}
    }
};

#define SUPERVISOR_SERVICES (sizeof(s_supervisor_services) / sizeof(s_supervisor_services[0]))

static struct
{
    AppV3SupervisorStatus_t status;
    const SupervisorService_t *pending;
    SupervisorOperation_t operation;
    uint32_t desired;
    uint32_t file_number;
    uint32_t subscription;
    TickType_t started;
    bool submitted;
} s_supervisor;
static uint32_t s_supervisor_id = 0x79000000U;

//保留本轮首错。已入队的事务继续等待原身份，不重发硬件命令。
static void Supervisor_Fail(SysResult_t result)
{
    if (s_supervisor.status.first_fault == SYS_OK)
        s_supervisor.status.first_fault = result;
    s_supervisor.status.planning = false;
}

static void Supervisor_ClearPending(void)
{
    s_supervisor.pending = NULL;
    s_supervisor.status.pending_id = 0U;
    s_supervisor.status.pending_component = 0U;
    s_supervisor.submitted = false;
}

static void Supervisor_Store(const AppV3ServiceEvent_t *event)
{
    switch (event->service)
    {
        case RUNTIME_TARGET_CAMERA:
            s_supervisor.status.camera = *event;
            break;
        case RUNTIME_TARGET_USB:
            s_supervisor.status.usb = *event;
            break;
        case RUNTIME_TARGET_STORAGE:
            s_supervisor.status.storage = *event;
            break;
        default:
            break;
    }
}

//业务回调只接纳匹配命令的终结，后续依赖调用留给Control下一轮。
static SysResult_t Supervisor_Result(const AppV3ServiceEvent_t *event, void *context)
{
    const SupervisorService_t *pending = s_supervisor.pending;
    AppV3HardwareState_t expected;
    uint32_t component;

    (void)context;
    if (!s_supervisor.submitted || pending == NULL || !event->real_hardware ||
        event->requester != RUNTIME_TARGET_CONTROL || event->service != pending->service ||
        event->command_id != s_supervisor.status.pending_id)
        return SYS_OK;

    //只有服务自己给出的匹配终结才能转移已确认状态。
    component = pending->bit;
    Supervisor_Store(event);
    s_supervisor.status.active &= ~component;
    s_supervisor.status.uncertain &= ~component;
    if (event->hardware.state == APP_V3_HW_ACTIVE)
        s_supervisor.status.active |= component;
    else if (event->hardware.state != APP_V3_HW_READY)
        s_supervisor.status.uncertain |= component;

    expected = s_supervisor.operation == SUPERVISOR_START ? APP_V3_HW_ACTIVE : APP_V3_HW_READY;
    if (event->result != SYS_OK || (s_supervisor.operation != SUPERVISOR_QUERY && event->hardware.state != expected))
        Supervisor_Fail(event->result == SYS_OK ? SYS_ERR_PROTOCOL : event->result);
    else if (s_supervisor.operation == SUPERVISOR_QUERY && event->hardware.first_fault != SYS_OK)
        Supervisor_Fail(event->hardware.first_fault);
    Supervisor_ClearPending();
    return SYS_OK;
}

static SysResult_t Supervisor_Begin(void)
{
    SysResult_t result = SYS_OK;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_CONTROL) || !AppV3_BusinessReady())
        return SYS_ERR_INVALID_STATE;
    if (s_supervisor.pending != NULL || s_supervisor.status.planning)
        return SYS_ERR_BUSY;
    //静态常驻，只有首个实际计划才占用一个既有订阅槽。
    if (s_supervisor.subscription == 0U)
        result = AppV3Business_Subscribe(RUNTIME_TARGET_COUNT, Supervisor_Result, NULL, &s_supervisor.subscription);
    return result;
}

static void Supervisor_NewStep(const SupervisorService_t *service, SupervisorOperation_t operation)
{
    if (s_supervisor_id == UINT32_MAX)
    {
        Supervisor_Fail(SYS_ERR_NO_RESOURCE);
        return;
    }
    s_supervisor.pending = service;
    s_supervisor.operation = operation;
    s_supervisor.status.pending_component = service->bit;
    s_supervisor.status.pending_id = ++s_supervisor_id;
    s_supervisor.started = xTaskGetTickCount();
}

static uint32_t Supervisor_Dependents(uint32_t provider)
{
    uint32_t i, mask = 0U;

    for (i = 0U; i < SUPERVISOR_SERVICES; ++i)
        if ((s_supervisor_services[i].provider & provider) != 0U)
            mask |= s_supervisor_services[i].bit;
    return mask;
}

//先逆序关闭不再需要的消费者，再正序启动新增依赖。
static void Supervisor_Plan(void)
{
    uint32_t i, current = s_supervisor.status.active | s_supervisor.status.uncertain;
    uint32_t stop = current & ~s_supervisor.desired;
    uint32_t start = s_supervisor.desired & ~s_supervisor.status.active;

    for (i = SUPERVISOR_SERVICES; i != 0U; --i)
    {
        const SupervisorService_t *service = &s_supervisor_services[i - 1U];
        if ((stop & service->bit) != 0U && (current & Supervisor_Dependents(service->bit)) == 0U)
        {
            Supervisor_NewStep(service, SUPERVISOR_STOP);
            return;
        }
    }
    for (i = 0U; i < SUPERVISOR_SERVICES; ++i)
    {
        const SupervisorService_t *service = &s_supervisor_services[i];
        if ((start & service->bit) != 0U && (s_supervisor.status.active & service->provider) == service->provider)
        {
            Supervisor_NewStep(service, SUPERVISOR_START);
            return;
        }
    }
    if (stop != 0U || start != 0U)
        Supervisor_Fail(SYS_ERR_INVALID_STATE);
    else
        s_supervisor.status.planning = false;
}

SysResult_t AppV3Supervisor_Set(uint32_t components, uint32_t file_number)
{
    uint32_t i, supported = 0U, desired = components;
    SysResult_t result;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_CONTROL))
        return SYS_ERR_INVALID_STATE;

    //逐项展开当前固定依赖，未接入的设备位始终拒绝。
    for (i = 0U; i < SUPERVISOR_SERVICES; ++i)
    {
        supported |= s_supervisor_services[i].bit;
        if ((components & s_supervisor_services[i].bit) != 0U)
            desired |= s_supervisor_services[i].provider;
    }
    if ((components & ~supported) != 0U)
        return SYS_ERR_NOT_SUPPORTED;
    if ((desired & s_supervisor.status.uncertain) != 0U)
        return SYS_ERR_INVALID_STATE;
    if ((desired & APP_V3_PIPE_RECORD & ~s_supervisor.status.active) != 0U && file_number == 0U)
        return SYS_ERR_INVALID_ARG;
    result = Supervisor_Begin();
    if (result != SYS_OK)
        return result;

    s_supervisor.desired = desired;
    s_supervisor.file_number = file_number;
    //若仍有未证明退出的服务，清理计划保留旧首错。
    if (s_supervisor.status.uncertain == 0U)
        s_supervisor.status.first_fault = SYS_OK;
    s_supervisor.status.timed_out = false;
    s_supervisor.status.planning = true;
    return SYS_OK;
}

static SysResult_t Supervisor_Single(uint32_t component, SupervisorOperation_t operation)
{
    uint32_t i;
    SysResult_t result;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_CONTROL))
        return SYS_ERR_INVALID_STATE;
    if (operation == SUPERVISOR_RECOVER &&
        ((s_supervisor.status.uncertain & component) == 0U ||
         ((s_supervisor.status.active | s_supervisor.status.uncertain) & Supervisor_Dependents(component)) != 0U))
        return SYS_ERR_INVALID_STATE;
    for (i = 0U; i < SUPERVISOR_SERVICES; ++i)
    {
        if (s_supervisor_services[i].bit != component)
            continue;
        result = Supervisor_Begin();
        if (result != SYS_OK)
            return result;
        if (s_supervisor_id == UINT32_MAX)
            return SYS_ERR_NO_RESOURCE;
        Supervisor_NewStep(&s_supervisor_services[i], operation);
        return SYS_OK;
    }
    return SYS_ERR_NOT_SUPPORTED;
}

SysResult_t AppV3Supervisor_Recover(uint32_t component)
{
    return Supervisor_Single(component, SUPERVISOR_RECOVER);
}

SysResult_t AppV3Supervisor_Query(uint32_t component)
{
    return Supervisor_Single(component, SUPERVISOR_QUERY);
}

SysResult_t AppV3Supervisor_Read(AppV3SupervisorStatus_t *status)
{
    if (status == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!App_MessagesIsOwner(RUNTIME_TARGET_CONTROL))
        return SYS_ERR_INVALID_STATE;
    *status = s_supervisor.status;
    return SYS_OK;
}

static SysResult_t Supervisor_Submit(void)
{
    const SupervisorService_t *service = s_supervisor.pending;
    uint32_t id = s_supervisor.status.pending_id;
    uint32_t operation = service->operations[s_supervisor.operation];

    switch (service->service)
    {
        case RUNTIME_TARGET_CAMERA:
            return CameraService_Request(id, (CameraServiceOperation_t)operation, 0U);
        case RUNTIME_TARGET_USB:
            return UsbService_Request(id, (UsbServiceOperation_t)operation, 0U);
        case RUNTIME_TARGET_STORAGE:
            return StorageService_Request(id, (StorageServiceOperation_t)operation, s_supervisor.file_number);
        default:
            return SYS_ERR_NOT_SUPPORTED;
    }
}

void AppV3Supervisor_Poll(void)
{
    SysResult_t result;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_CONTROL))
        return;
    if (s_supervisor.pending == NULL && s_supervisor.status.planning)
        Supervisor_Plan();
    if (s_supervisor.pending == NULL)
        return;

    //超时保留已到达Service队列的命令，停止后续调度并等待匹配的迟到结果。
    if (xTaskGetTickCount() - s_supervisor.started >= pdMS_TO_TICKS(SUPERVISOR_TIMEOUT_MS))
    {
        s_supervisor.status.timed_out = true;
        Supervisor_Fail(SYS_ERR_TIMEOUT);
        if (!s_supervisor.submitted)
            Supervisor_ClearPending();
        return;
    }
    if (s_supervisor.submitted)
        return;
    result = Supervisor_Submit();
    if (result == SYS_OK)
        s_supervisor.submitted = true;
    else if (result != SYS_ERR_BUSY && result != SYS_ERR_NO_RESOURCE)
    {
        Supervisor_Fail(result);
        Supervisor_ClearPending();
    }
}

#if APP_V3_RECORD_BOARD_TEST
volatile AppV3RecordBoardCommand_t g_app_v3_record_command;
volatile AppV3RecordBoardStatus_t g_app_v3_record_board;
static uint32_t s_record_board_started;

static SysResult_t RecordBoard_Dispatch(uint32_t command, uint32_t partition, uint32_t file, uint32_t active)
{
    switch (command)
    {
        case 1U: return AppV3Supervisor_Set(APP_V3_PIPE_CAMERA, 0U);
        case 2U:
            //只接受用户已核对的显式分区。文件号必须由本次操作提供。
            if (partition != APP_STORAGE_PARTITION || file == 0U)
                return SYS_ERR_INVALID_ARG;
            if ((active & APP_V3_PIPE_RECORD) != 0U)
                return SYS_ERR_ALREADY_EXISTS;
            return AppV3Supervisor_Set(APP_V3_PIPE_RECORD, file);
        case 3U: return AppV3Supervisor_Set(APP_V3_PIPE_CAMERA, 0U);
        case 4U: return AppV3Supervisor_Set(0U, 0U);
        case 5U: return AppV3Supervisor_Query(APP_V3_PIPE_CAMERA);
        case 6U: return AppV3Supervisor_Query(APP_V3_PIPE_RECORD);
        case 7U: return AppV3Supervisor_Recover(APP_V3_PIPE_RECORD);
        case 8U:
        {
            UBaseType_t words[RUNTIME_TARGET_COUNT];
            uint32_t index;

            //按原任务句柄读取历史栈低水位，单位是StackType_t。
            g_app_v3_record_board.stack_valid = App_EventsReadStackWatermarks(words);
            if (!g_app_v3_record_board.stack_valid)
                return SYS_ERR_INVALID_STATE;
            for (index = 0U; index < RUNTIME_TARGET_COUNT; ++index)
                g_app_v3_record_board.stack_remaining_words[index] = words[index];
            return SYS_OK;
        }
        default: return SYS_ERR_NOT_SUPPORTED;
    }
}

void AppV3_RecordBoardPoll(void)
{
    AppV3SupervisorStatus_t snapshot;
    uint32_t command;

    //只在Control任务读取并清除邮箱。命令完成仍由原Business结果决定。
    if (AppV3Supervisor_Read(&snapshot) != SYS_OK)
        return;
    g_app_v3_record_board.configured_partition = APP_STORAGE_PARTITION;
    if (g_app_v3_record_board.command_pending && !snapshot.planning && snapshot.pending_id == 0U)
    {
        g_app_v3_record_board.elapsed_ms = Platform_TimeGetMs() - s_record_board_started;
        g_app_v3_record_board.command_pending = false;
    }
    command = g_app_v3_record_command.command;
    if (command != 0U && !g_app_v3_record_board.command_pending)
    {
        uint32_t partition = g_app_v3_record_command.approved_partition;
        uint32_t file = g_app_v3_record_command.file_number;

        g_app_v3_record_command.command = 0U;
        //每次录像都要求写入本次已批准的分区和未使用文件号。
        g_app_v3_record_command.approved_partition = 0U;
        g_app_v3_record_command.file_number = 0U;
        g_app_v3_record_board.last_command = command;
        ++g_app_v3_record_board.issued;
        g_app_v3_record_board.elapsed_ms = 0U;
        s_record_board_started = Platform_TimeGetMs();
        g_app_v3_record_board.dispatch_result = RecordBoard_Dispatch(command, partition, file, snapshot.active);
        g_app_v3_record_board.command_pending = g_app_v3_record_board.dispatch_result == SYS_OK && command != 8U;
        (void)AppV3Supervisor_Read(&snapshot);
    }
    g_app_v3_record_board.supervisor = snapshot;
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
            AppV3Supervisor_Poll();
#if APP_V3_RECORD_BOARD_TEST
            AppV3_RecordBoardPoll();
#endif
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
