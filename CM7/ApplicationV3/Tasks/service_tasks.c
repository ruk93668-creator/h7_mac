#include "../Core/app_services.h"
#include "app_tasks.h"
#include "app_events.h"
#include "platform_sync.h"
#include "service_tasks.h"
#include "supervisor.h"
#include "../Services/Camera/camera_service.h"
#include "../Services/USB/usb_service.h"
#include "../Services/Storage/storage_service.h"
#include "../Runtime/service_event_loop.h"
#include "../Runtime/service_events.h"
#include <stddef.h>

//服务启动结果等待上限，单位毫秒
#define STARTUP_DEADLINE_MS 1000U
//Control检查队列进度与启动超时的周期，单位毫秒
#define STARTUP_POLL_MS 100U
//启动阶段同轮允许处理的结果上限
#define STARTUP_MAX_ATTEMPTS (1U + STARTUP_DEADLINE_MS / STARTUP_POLL_MS)

//保留原Control静态栈及各目标任务的创建状态
static StackType_t s_control_stack[768U] __attribute__((aligned(8)));
//各任务的静态内核控制块，创建失败也不复用
static StaticTask_t s_task_controls[RUNTIME_TARGET_COUNT];
//记录已尝试创建的任务身份位，防止重复创建
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


//由原ControlTask串行调度实际Service；表只描述当前已接入的依赖。
#define SUPERVISOR_TIMEOUT_MS 10000U

//监督器对单组件发出的生命周期动作
typedef enum
{
    SUPERVISOR_START,
    SUPERVISOR_STOP,
    SUPERVISOR_RECOVER,
    SUPERVISOR_QUERY
} SupervisorOperation_t;

//固定的组件依赖及业务命令登记
typedef struct
{
    //本组件在监督器活动掩码中的位
    uint32_t bit;
    //启动前必须确认可用的依赖组件
    uint32_t provider;
    //服务身份，用于筛选结果或发送生命周期命令
    RuntimeEventTarget_t service;
    //索引本组件所支持的命令编号
    uint8_t operations[4];
} SupervisorService_t;

//监督器固定服务表，声明依赖与命令映射
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

//固定服务表实际组件数量
#define SUPERVISOR_SERVICES (sizeof(s_supervisor_services) / sizeof(s_supervisor_services[0]))

//由Control维护的协调计划和在途命令
static struct
{
    //监督器发布的状态及最近业务终结副本
    AppV3SupervisorStatus_t status;
    //存在待处理的请求或队列背压下暂存的终结结果
    const SupervisorService_t *pending;
    //服务或协调器正在处理的操作编号
    SupervisorOperation_t operation;
    //调用者申请达到的组件活动集合
    uint32_t desired;
    //创建新录像文件使用的唯一编号，由发起者提供
    uint32_t file_number;
    //当前结果订阅凭据，推进完成后统一撤销
    uint32_t subscription;
    //当前命令的起始内核节拍，用来判定终结等待超时
    TickType_t started;
    //命令已成功入队，等待同一身份结果
    bool submitted;
} s_supervisor;
//跨事务递增的命令身份水位
static uint32_t s_supervisor_id = 0x79000000U;

//保留当前事务的首个错误。已入队的事务继续等待原身份，不重发硬件命令。
static void Supervisor_Fail(SysResult_t result)
{
    if (s_supervisor.status.first_fault == SYS_OK)
        s_supervisor.status.first_fault = result;
    s_supervisor.status.planning = false;
}

//清除已终结事务的等待信息
static void Supervisor_ClearPending(void)
{
    s_supervisor.pending = NULL;
    s_supervisor.status.pending_id = 0U;
    s_supervisor.status.pending_component = 0U;
    s_supervisor.submitted = false;
}

//按服务和命令身份接收终结并更新活动事实
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

//检查Control身份并准备一项协调事务
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

//为计划中的下一条命令分配编号并设置等待状态
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

//汇总仍依赖该组件的活动或不确定路径
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

//由Control提交目标活动集合，随后查询实际完成状态
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

//提交单组件查询或恢复并沿用既有协调约束
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

//由Control请求现有的显式恢复契约
SysResult_t AppV3Supervisor_Recover(uint32_t component)
{
    return Supervisor_Single(component, SUPERVISOR_RECOVER);
}

//由Control请求刷新指定组件的真实状态
SysResult_t AppV3Supervisor_Query(uint32_t component)
{
    return Supervisor_Single(component, SUPERVISOR_QUERY);
}

//复制监督器当前只读状态，供协调调用者查询
SysResult_t AppV3Supervisor_Read(AppV3SupervisorStatus_t *status)
{
    if (status == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!App_MessagesIsOwner(RUNTIME_TARGET_CONTROL))
        return SYS_ERR_INVALID_STATE;
    *status = s_supervisor.status;
    return SYS_OK;
}

//将计划步骤交给目标服务的命令邮箱
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

//由Control按依赖顺序推进业务计划和等待结果
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
