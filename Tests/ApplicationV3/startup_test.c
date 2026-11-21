//使用真实 FreeRTOS Queue/List。测试只模拟调度、身份和设备状态。
#include "app_services.h"
#include "app_events.h"
#include "app_main.h"
#include "app_tasks.h"
#include "app_status.h"
#include "service_tasks.h"
#include "queue.h"
#include "queue_pool.h"
#include "service_record.h"
#include "service_event_loop.h"
#include "service_events.h"
#include "../../CM7/ApplicationV3/Services/Camera/camera_service.h"
#include "../../CM7/ApplicationV3/Services/USB/usb_service.h"
#include "../../CM7/ApplicationV3/Services/Storage/storage_service.h"
#include <setjmp.h>

volatile uint32_t g_failure;
static RuntimeEventTarget_t owner = RUNTIME_TARGET_CONTROL;
static BaseType_t scheduler = taskSCHEDULER_NOT_STARTED;
static uint32_t critical_depth;
static bool notify_fails, event_hint, count_event_hints;
static uint32_t exception, primask;
static uint32_t irq_priority = configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY;
static TickType_t test_tick;
static bool blocking_probe;
static AppV3Queue_t pinned_queue;
static SysResult_t during_wait;
static bool record_probe;
static const AppV3Context_t *record_context;
static SysResult_t record_probe_expected;
static bool reject_next_queue_send;
static uint32_t rejected_queue_sends;

BaseType_t Test_RealQueueSend(QueueHandle_t queue, const void *item, TickType_t timeout,
    BaseType_t position);

//只拦截专项点上的一次提交，其余收发使用真实FreeRTOS队列
BaseType_t xQueueGenericSend(QueueHandle_t queue, const void *item, TickType_t timeout,
    BaseType_t position)
{
    if (reject_next_queue_send)
    {
        reject_next_queue_send = false;
        ++rejected_queue_sends;
        return errQUEUE_FULL;
    }
    return Test_RealQueueSend(queue, item, timeout, position);
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return (TaskHandle_t)(uintptr_t)(owner + 1U);
}

#define CHECK(condition) do { if (!(condition)) { g_failure = __LINE__; return __LINE__; } } while (0)

uint32_t Platform_SyncGetException(void)
{
    return exception;
}

uint32_t Platform_SyncGetPrimask(void)
{
    return primask;
}

uint32_t Platform_SyncGetBasepri(void)
{
    return 0U;
}

uint32_t Platform_SyncGetIrqPriority(uint32_t irq_number)
{
    (void)irq_number;
    return irq_priority;
}

TickType_t xTaskGetTickCount(void)
{
    return test_tick;
}

void Platform_SyncDisableIrq(void)
{
    primask = 1U;
}

void Platform_SyncRestoreIrq(uint32_t saved)
{
    primask = saved;
}

void Platform_SyncMemoryBarrier(void)
{
}

void vPortFree(void *pointer)
{
    (void)pointer;
    g_failure = __LINE__;
}

SysResult_t RuntimeEvent_PostFromIsr(RuntimeEventTarget_t target, uint32_t events)
{
    return RuntimeEvent_Post(target, events);
}

bool App_EventsIsOwner(RuntimeEventTarget_t target)
{
    return owner == target;
}

BaseType_t xTaskGetSchedulerState(void)
{
    return scheduler;
}

SysResult_t RuntimeEvent_Post(RuntimeEventTarget_t target, uint32_t events)
{
    (void)target;
    if (count_event_hints && !notify_fails && (events & RUNTIME_EVENT_WAKE) != 0U)
        event_hint = true;
    return notify_fails ? SYS_ERR_IO : SYS_OK;
}

void Platform_RtosAssert(const char *file, uint32_t line)
{
    (void)file;
    g_failure = line;
}

void vPortEnterCritical(void)
{
    ++critical_depth;
}

void vPortExitCritical(void)
{
    if (critical_depth == 0U)
        g_failure = __LINE__;
    else
        --critical_depth;
}

//软测只执行非阻塞队列操作，意外的任务阻塞视为失败。
void vTaskSuspendAll(void)
{
    if (record_probe)
    {
        RuntimeEventTarget_t saved_owner = owner;
        owner = RUNTIME_TARGET_CAMERA;
        if (AppV3_InitService(owner, record_context) != record_probe_expected ||
            AppV3Record_Destroy(owner) != SYS_ERR_BUSY)
            g_failure = __LINE__;
        owner = saved_owner;
    }
    else if (blocking_probe)
        during_wait = AppV3Queue_Free(pinned_queue);
    else
        g_failure = __LINE__;
}

BaseType_t xTaskResumeAll(void)
{
    if (!blocking_probe && !record_probe)
        g_failure = __LINE__;
    return pdTRUE;
}

BaseType_t xTaskRemoveFromEventList(const List_t *list)
{
    (void)list;
    g_failure = __LINE__;
    return pdFALSE;
}

void vTaskPlaceOnEventList(List_t *list, TickType_t ticks)
{
    (void)list;
    (void)ticks;
    g_failure = __LINE__;
}

void vTaskInternalSetTimeOutState(TimeOut_t *timeout)
{
    timeout->xOverflowCount = 0;
    timeout->xTimeOnEntering = 0U;
}

BaseType_t xTaskCheckForTimeOut(TimeOut_t *timeout, TickType_t *remaining_ticks)
{
    (void)timeout;
    (void)remaining_ticks;
    return pdTRUE;
}

void vTaskMissedYield(void)
{
    g_failure = __LINE__;
}

UBaseType_t uxTaskGetNumberOfTasks(void)
{
    return 0U;
}

void vPortValidateInterruptPriority(void)
{
}

static VideoConfig_t fixture_video;
static AppStorageConfig_t fixture_storage;
static bool fixture_missing_config;
static SysResult_t fixture_camera_result;
static AppControlStatus_t published;
static unsigned int published_count, waited;
static unsigned int scheduled_priority, created_threads, fail_thread;
static uint32_t scheduled_stack_words;
static bool event_test;
static unsigned int event_count;
static uint32_t event_delays, event_wait_ms, event_callback_calls;
static AppV3EventLoop_t *fixture_loops[RUNTIME_TARGET_COUNT];
static bool reject_event_subscribe;
static AppV3ServiceEvent_t business_events[20U];
static uint32_t business_event_count;
static bool run_control_business, run_storage_business;
static SysResult_t Test_StoreEvent(const AppV3ServiceEvent_t *event, void *context);
static uint32_t Test_StartBusiness(const AppV3Context_t *context);
static SysResult_t Test_ServiceStep(RuntimeEventTarget_t service);


SysResult_t Test_RealEventSubscribe(AppV3EventLoop_t *loop, AppV3Queue_t queue,
    AppV3QueueReadable_t callback, void *context);

//只在软测记录实际订阅入口；可注入订阅失败，其他测试走真实Event Loop
SysResult_t AppV3EventLoop_Subscribe(AppV3EventLoop_t *loop, AppV3Queue_t queue,
    AppV3QueueReadable_t callback, void *context)
{
    SysResult_t result;
    if (reject_event_subscribe)
        return SYS_ERR_IO;
    result = Test_RealEventSubscribe(loop, queue, callback, context);
    if (result == SYS_OK)
        fixture_loops[owner] = loop;
    return result;
}

static bool check_boot_order, framework_ready;
static TaskFunction_t scheduled_entry;
static void *scheduled_argument;
static jmp_buf task_exit;

const VideoConfig_t *VideoConfig_GetDefault(void)
{
    return fixture_missing_config ? NULL : &fixture_video;
}

const AppStorageConfig_t *VideoConfig_GetStorage(void)
{
    return &fixture_storage;
}

SysResult_t CameraCapture_ValidateStreamConfig(const CameraConfig_t *config)
{
    return config == &fixture_video.camera ? fixture_camera_result : SYS_ERR_INVALID_ARG;
}

SysResult_t App_StatusPublishControl(const AppControlStatus_t *status)
{
    if (owner != RUNTIME_TARGET_CONTROL)
        return SYS_ERR_INVALID_STATE;
    published = *status;
    ++published_count;
    return SYS_OK;
}

TaskHandle_t xTaskCreateStatic(TaskFunction_t entry, const char *const name,
    const uint32_t stack_words, void *const argument, UBaseType_t priority,
    StackType_t *const stack, StaticTask_t *const task)
{
    (void)name;
    (void)stack;
    if (++created_threads == fail_thread)
        return NULL;
    scheduled_entry = entry;
    scheduled_argument = argument;
    scheduled_priority = priority;
    scheduled_stack_words = stack_words;
    return (TaskHandle_t)task;
}

SysResult_t App_EventsAttach(RuntimeEventTarget_t target, TaskHandle_t handle)
{
    return target != RUNTIME_TARGET_IPC && handle != NULL
        ? SYS_OK : SYS_ERR_INVALID_ARG;
}

SysResult_t App_EventsInit(void)
{
    return SYS_OK;
}

SysResult_t IpcTask_Create(void)
{
    ++created_threads;
    return SYS_OK;
}

SysResult_t App_EventsRecordInitialization(SysResult_t result)
{
    framework_ready = result == SYS_OK;
    return result;
}

SysResult_t App_EventsReleaseBoot(void)
{
    if (check_boot_order && (!framework_ready || scheduler != taskSCHEDULER_RUNNING))
        return SYS_ERR_INVALID_STATE;
    return owner == RUNTIME_TARGET_CONTROL ? SYS_OK : SYS_ERR_INVALID_STATE;
}

void vTaskDelay(TickType_t ticks)
{
    if (event_test && ticks == 1U)
    {
        ++event_delays;
        return;
    }
    g_failure = __LINE__;
    longjmp(task_exit, 1);
}

SysResult_t App_EventsWait(RuntimeEventTarget_t target, uint32_t timeout_ms, uint32_t *events)
{
    if (run_storage_business)
    {
        uint32_t consumed;
        static uint32_t subscription;
        if (target != RUNTIME_TARGET_STORAGE)
            return SYS_ERR_INVALID_STATE;
        ++waited;
        if (waited == 1U && timeout_ms == APP_EVENTS_WAIT_FOREVER)
        {
            *events = RUNTIME_EVENT_BOOT_READY;
            return SYS_OK;
        }

        //让Control在真实Storage线程两次等待之间提交命令，模拟调度但不调用硬件
        owner = RUNTIME_TARGET_CONTROL;
        if (waited == 2U && timeout_ms == 100U)
        {
            if (AppV3_DrainInitialization() != SYS_OK || !AppV3_BusinessReady() ||
                AppV3Business_Subscribe(RUNTIME_TARGET_STORAGE, Test_StoreEvent, NULL, &subscription) != SYS_OK ||
                StorageService_Request(1U, STORAGE_SERVICE_MOCK_START, 0U) != SYS_OK)
                g_failure = __LINE__;
        }
        else if (waited == 3U && timeout_ms == 100U)
        {
            if (AppV3Business_Drain(&consumed) != SYS_OK || consumed != 1U ||
                StorageService_Request(2U, STORAGE_SERVICE_MOCK_STOP, 0U) != SYS_OK)
                g_failure = __LINE__;
        }
        else
        {
            if (waited != 4U || timeout_ms != 100U ||
                AppV3Business_Drain(&consumed) != SYS_OK || consumed != 1U ||
                AppV3Business_Unsubscribe(subscription) != SYS_OK)
                g_failure = __LINE__;
            longjmp(task_exit, 1);
        }
        owner = RUNTIME_TARGET_STORAGE;
        return SYS_ERR_TIMEOUT;
    }
    if (run_control_business)
    {
        static uint32_t ticket;
        if (target != RUNTIME_TARGET_CONTROL)
            return SYS_ERR_TIMEOUT;
        ++waited;
        if (waited == 1U && timeout_ms == 100U && !AppV3_BusinessReady())
        {
            if (Test_StartBusiness(AppV3_GetContext()) != 0U ||
                AppV3Business_Subscribe(RUNTIME_TARGET_STORAGE, Test_StoreEvent, NULL, &ticket) != SYS_OK)
                g_failure = __LINE__;
            return SYS_ERR_TIMEOUT;
        }
        if (waited == 2U && timeout_ms == 100U)
        {
            if (StorageService_Request(1U, STORAGE_SERVICE_MOCK_START, 0U) != SYS_OK ||
                StorageService_Request(2U, STORAGE_SERVICE_QUERY, 0U) != SYS_OK ||
                Test_ServiceStep(RUNTIME_TARGET_STORAGE) != SYS_OK)
                g_failure = __LINE__;
            return SYS_ERR_TIMEOUT;
        }
        if (waited != 3U || timeout_ms != 0U || business_event_count != 2U ||
            AppV3Business_Unsubscribe(ticket) != SYS_OK)
            g_failure = __LINE__;
        longjmp(task_exit, 1);
    }

    if (event_test)
    {
        ++waited;
        event_wait_ms += timeout_ms;
        if (event_hint)
        {
            event_hint = false;
            *events = RUNTIME_EVENT_WAKE;
            return SYS_OK;
        }
        return SYS_ERR_TIMEOUT;
    }

    if (target == RUNTIME_TARGET_CONTROL)
    {
        if (timeout_ms == 0U)
            return SYS_ERR_TIMEOUT;
        if (timeout_ms == 100U && waited++ == 0U)
        {
            test_tick += pdMS_TO_TICKS(1000U);
            return SYS_ERR_TIMEOUT;
        }
        if (timeout_ms != 100U || waited != 2U)
            g_failure = __LINE__;
        longjmp(task_exit, 1);
    }
    if (target != RUNTIME_TARGET_STORAGE)
    {
        g_failure = __LINE__;
        longjmp(task_exit, 1);
    }

    //先送普通提示，再给 BOOT。队列填满时只有待发结果触发定时重试。
    if (waited++ == 0U && timeout_ms == APP_EVENTS_WAIT_FOREVER)
    {
        *events = RUNTIME_EVENT_WAKE;
        return SYS_OK;
    }
    if (waited == 2U && timeout_ms == APP_EVENTS_WAIT_FOREVER)
    {
        *events = RUNTIME_EVENT_BOOT_READY;
        return SYS_OK;
    }
    if (waited == 3U && timeout_ms == 100U)
    {
        owner = RUNTIME_TARGET_CONTROL;
        if (AppV3_DrainInitialization() != SYS_OK || published_count != 0U)
            g_failure = __LINE__;
        owner = RUNTIME_TARGET_STORAGE;
        *events = RUNTIME_EVENT_BOOT_READY;
        return SYS_OK;
    }

    if (waited != 4U || timeout_ms != 100U)
        g_failure = __LINE__;
    longjmp(task_exit, 1);
}

static SysResult_t Test_QueueReadable(AppV3Queue_t queue, void *context)
{
    AppMessage_t message;
    (void)context;
    if (AppV3Queue_Get(queue, &message, 0U) != SYS_OK || message.command_id != event_count + 1U)
        return SYS_ERR_IO;
    ++event_count;
    return SYS_OK;
}

//保留原业务结果通道核验，并验证四条历史通道明确拒绝
static uint32_t Test_ExistingMessageChannels(void)
{
    static const struct
    {
        AppChannel_t channel;
        RuntimeEventTarget_t sender;
        RuntimeEventTarget_t receiver;
    } routes[] =
    {
        {APP_CHANNEL_CAMERA_COMMAND, RUNTIME_TARGET_CONTROL, RUNTIME_TARGET_CAMERA},
        {APP_CHANNEL_USB_COMMAND, RUNTIME_TARGET_CONTROL, RUNTIME_TARGET_USB},
        {APP_CHANNEL_TO_CAMERA, RUNTIME_TARGET_USB, RUNTIME_TARGET_CAMERA},
        {APP_CHANNEL_TO_USB, RUNTIME_TARGET_CAMERA, RUNTIME_TARGET_USB}
    };
    AppMessage_t message = {.type = APP_MESSAGE_START};
    uint32_t route_index;

    for (route_index = 0U; route_index < sizeof(routes) / sizeof(routes[0]); ++route_index)
    {
        owner = routes[route_index].sender;
        message.source = owner;
        message.command_id = route_index + 1U;
        CHECK(App_MessagesSend(routes[route_index].channel, &message) == SYS_ERR_NOT_SUPPORTED);
        owner = routes[route_index].receiver;
        CHECK(!App_MessagesReceive(routes[route_index].channel, &message));
    }
    owner = RUNTIME_TARGET_CAMERA;
    message.source = owner;
    message.command_id = 7U;
    CHECK(App_MessagesSend(APP_CHANNEL_CONTROL, &message) == SYS_OK);
    owner = RUNTIME_TARGET_CONTROL;
    CHECK(App_MessagesReceive(APP_CHANNEL_CONTROL, &message) &&
        message.source == RUNTIME_TARGET_CAMERA && message.command_id == 7U);
    return 0U;
}

//完成原任务组织、BOOT和三个实际Service初始化后审计两个静态池
static uint32_t Test_PrivateQueuesAfterStartup(void)
{
    static const RuntimeEventTarget_t service_owners[] =
    {
        RUNTIME_TARGET_CAMERA, RUNTIME_TARGET_USB, RUNTIME_TARGET_STORAGE
    };
    AppV3Queue_t service_queues[3U], excess, startup;
    AppV3RecordRef_t references[3U] = {{0}};
    UBaseType_t capacity, remaining;
    uint32_t service_index;

    check_boot_order = true;
    CHECK(App_TasksCreate() == SYS_OK && g_app_tasks_debug.created_tasks == 5U);
    CHECK(App_Init() == SYS_OK);
    CHECK(App_TasksRecordInitialization(SYS_OK) == SYS_OK);
    scheduler = taskSCHEDULER_RUNNING;
    CHECK(App_TasksReleaseBoot() == SYS_OK && g_app_tasks_debug.boot_released == 1U);
    for (service_index = 0U; service_index < 3U; ++service_index)
    {
        owner = service_owners[service_index];
        CHECK(AppV3_InitService(owner, AppV3_GetContext()) == SYS_OK);
        CHECK(AppV3_ReportInitialization(owner, SYS_OK) == SYS_OK);
    }
    owner = RUNTIME_TARGET_CONTROL;
    CHECK(AppV3_DrainInitialization() == SYS_OK && published_count == 1U && published.result == SYS_OK);
    CHECK(AppV3_GetStartupQueue(&startup) == SYS_OK);
    CHECK(AppV3Queue_Query(startup, &capacity, &remaining) == SYS_OK && capacity == 4U && remaining == 4U);
    CHECK(AppV3Queue_Alloc(sizeof(uint32_t), 1U, &excess) == SYS_ERR_NO_RESOURCE);

    //V3.1B由各Service初始化时真实占用R1预留槽，测试不再重复申请
    for (service_index = 0U; service_index < 3U; ++service_index)
    {
        const AppV3EventLoop_t *loop = fixture_loops[service_owners[service_index]];
        CHECK(loop != NULL && loop->owner == service_owners[service_index]);
        service_queues[service_index] = loop->queue;
        CHECK(AppV3Queue_Query(loop->queue, &capacity, &remaining) == SYS_OK &&
            capacity == 4U && remaining == 4U);
        CHECK(AppV3Record_Open(service_owners[service_index], 0U, &references[service_index]) == SYS_OK);
    }
    CHECK(service_queues[0].index != service_queues[1].index &&
        service_queues[0].index != service_queues[2].index &&
        service_queues[1].index != service_queues[2].index);
    CHECK(AppV3Queue_Alloc(sizeof(AppMessage_t), 4U, &excess) == SYS_ERR_NO_RESOURCE);
    CHECK(Test_ExistingMessageChannels() == 0U);
    for (service_index = 0U; service_index < 3U; ++service_index)
        CHECK(AppV3Record_Close(&references[service_index]) == SYS_OK);
    return 0U;
}

//模式一在每次消费后补入新消息，模式二故意不消费
static SysResult_t Test_R1Readable(AppV3Queue_t queue, void *context)
{
    uint32_t mode = *(const uint32_t *)context;
    AppMessage_t message;
    SysResult_t result;

    ++event_callback_calls;
    if (mode == 2U)
        return SYS_OK;
    result = Test_QueueReadable(queue, NULL);
    if (result != SYS_OK || mode == 0U)
        return result;

    message = (AppMessage_t){.type = APP_MESSAGE_START, .command_id = event_count + 4U};
    return AppV3Queue_Put(queue, &message, 0U);
}

static uint32_t Test_EventLoopR1(uint32_t mode)
{
    AppV3EventLoop_t loop = {.initialized = false};
    AppV3Queue_t queue;
    AppMessage_t message = {.type = APP_MESSAGE_START};
    UBaseType_t capacity, remaining;
    uint32_t events, message_index;

    event_test = true;
    count_event_hints = true;
    notify_fails = mode != 0U;
    CHECK(AppV3Queue_Alloc(sizeof(message), 4U, &queue) == SYS_OK);
    CHECK(AppV3EventLoop_Init(&loop, owner) == SYS_OK);
    CHECK(AppV3EventLoop_Subscribe(&loop, queue, Test_R1Readable, &mode) == SYS_OK);
    for (message_index = 1U; message_index <= 4U; ++message_index)
    {
        message.command_id = message_index;
        CHECK(AppV3Queue_Put(queue, &message, 0U) == SYS_OK);
    }
    CHECK(AppV3EventLoop_Step(&loop, 100U, &events) == SYS_OK);
    CHECK(event_callback_calls == 4U && event_wait_ms == 0U && event_delays == 1U && waited == 1U);
    CHECK(events == (mode == 0U ? RUNTIME_EVENT_WAKE : 0U));
    CHECK(AppV3Queue_Query(queue, &capacity, &remaining) == SYS_OK && capacity == 4U);
    if (mode == 0U)
    {
        CHECK(event_count == 4U && remaining == 4U);
        CHECK(AppV3EventLoop_Step(&loop, 100U, &events) == SYS_ERR_TIMEOUT && event_wait_ms == 100U);
        CHECK(event_callback_calls == 4U && event_delays == 1U);
        return 0U;
    }

    //持续来件和不消费都必须受相同的回调预算约束
    CHECK(remaining == 0U);
    CHECK(AppV3EventLoop_Step(&loop, 100U, &events) == SYS_OK);
    CHECK(event_callback_calls == 8U && event_wait_ms == 0U && event_delays == 2U && waited == 2U);
    CHECK(event_count == (mode == 1U ? 8U : 0U));
    CHECK(AppV3Queue_Query(queue, &capacity, &remaining) == SYS_OK && remaining == 0U);
    return 0U;
}

//在Open已登记等待者时拒绝实际信号提交，检查失败、超时及槽位撤回
static uint32_t Test_RecordSignalFailure(bool prior_failure, const AppV3Context_t *context)
{
    AppV3RecordRef_t reference = {0};
    AppV3ServiceInstance_t instance = {RUNTIME_TARGET_CAMERA, context};
    SysResult_t published_result;

    record_probe = true;
    record_context = context;
    reject_next_queue_send = true;
    fixture_camera_result = prior_failure ? SYS_ERR_IO : SYS_OK;
    record_probe_expected = prior_failure ? SYS_ERR_IO : SYS_ERR_BUSY;
    CHECK(AppV3Record_Open(RUNTIME_TARGET_CAMERA, 1U, &reference) == record_probe_expected);
    CHECK(record_probe && rejected_queue_sends == 1U && reference.ticket == 0U && reference.instance == NULL);
    record_probe = false;

    //后来调用者仍观察原错误，成功的假报告无法越过Record核验
    CHECK(AppV3Record_Query(RUNTIME_TARGET_CAMERA, &published_result) == SYS_OK &&
        published_result == record_probe_expected);
    CHECK(AppV3Record_Open(RUNTIME_TARGET_CAMERA, 0U, &reference) == record_probe_expected);
    owner = RUNTIME_TARGET_CAMERA;
    CHECK(AppV3_ReportInitialization(owner, SYS_OK) == SYS_ERR_INVALID_STATE);
    CHECK(AppV3_ReportInitialization(owner, record_probe_expected) == SYS_OK);
    owner = RUNTIME_TARGET_CONTROL;
    CHECK(AppV3_DrainInitialization() == SYS_OK && published_count == 1U &&
        published.last_fault == record_probe_expected);
    owner = RUNTIME_TARGET_CAMERA;
    CHECK(AppV3Record_Destroy(owner) == SYS_OK);
    CHECK(AppV3Record_Create(owner, &instance) == SYS_OK);
    CHECK(AppV3Record_Open(owner, 0U, &reference) == SYS_OK && reference.instance == &instance);
    CHECK(AppV3Record_Close(&reference) == SYS_OK && AppV3Record_Destroy(owner) == SYS_OK);
    CHECK(g_failure == 0U && critical_depth == 0U);
    return 0U;
}

//按真实服务身份推进初始化，Control继续使用原启动通道
static uint32_t Test_StartBusiness(const AppV3Context_t *context)
{
    static const RuntimeEventTarget_t services[] =
    {
        RUNTIME_TARGET_CAMERA, RUNTIME_TARGET_USB, RUNTIME_TARGET_STORAGE
    };
    uint32_t service_index;

    scheduler = taskSCHEDULER_RUNNING;
    for (service_index = 0U; service_index < sizeof(services) / sizeof(services[0]); ++service_index)
    {
        owner = services[service_index];
        CHECK(AppV3_InitService(owner, context) == SYS_OK);
        CHECK(AppV3_ReportInitialization(owner, SYS_OK) == SYS_OK);
    }
    owner = RUNTIME_TARGET_CONTROL;
    CHECK(AppV3_DrainInitialization() == SYS_OK && published.result == SYS_OK);
    CHECK(AppV3_BusinessReady());
    event_test = true;
    return 0U;
}

//通过真实订阅回调、队列和Event Loop处理一个Service批次
static SysResult_t Test_ServiceStep(RuntimeEventTarget_t service)
{
    uint32_t events;
    SysResult_t result;
    owner = service;
    result = AppV3EventLoop_Step(fixture_loops[service], 0U, &events);
    owner = RUNTIME_TARGET_CONTROL;
    return result;
}

static SysResult_t Test_StoreEvent(const AppV3ServiceEvent_t *event, void *context)
{
    if (business_event_count >= sizeof(business_events) / sizeof(business_events[0]) || context != NULL)
        return SYS_ERR_NO_RESOURCE;
    business_events[business_event_count++] = *event;
    return SYS_OK;
}

static uint32_t Test_BusinessIsolation(const AppV3Context_t *context)
{
    uint32_t ticket, consumed, events;
    CHECK(Test_StartBusiness(context) == 0U);
    CHECK(AppV3Business_Subscribe(RUNTIME_TARGET_COUNT, Test_StoreEvent, NULL, &ticket) == SYS_OK);
    CHECK(CameraService_Request(1U, CAMERA_SERVICE_MOCK_START, 0U) == SYS_OK);
    CHECK(UsbService_Request(1U, USB_SERVICE_SET_PARAMETER, 720U) == SYS_OK);
    CHECK(StorageService_Request(1U, STORAGE_SERVICE_QUERY, 0U) == SYS_OK);
    CHECK(business_event_count == 0U);
    owner = RUNTIME_TARGET_USB;
    CHECK(AppV3EventLoop_Step(fixture_loops[RUNTIME_TARGET_CAMERA], 0U, &events) == SYS_ERR_INVALID_STATE);
    CHECK(UsbService_Request(2U, USB_SERVICE_QUERY, 0U) == SYS_ERR_INVALID_STATE);
    CHECK(AppV3Business_Drain(&consumed) == SYS_ERR_INVALID_STATE);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_USB) == SYS_OK);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_CAMERA) == SYS_OK);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_STORAGE) == SYS_OK);
    CHECK(AppV3Business_Drain(&consumed) == SYS_OK && consumed == 3U && business_event_count == 3U);
    CHECK(business_events[0].service == RUNTIME_TARGET_USB && business_events[0].parameter == 720U &&
        !business_events[0].simulated_running);
    CHECK(business_events[1].service == RUNTIME_TARGET_CAMERA && business_events[1].simulated_running &&
        business_events[1].requester == RUNTIME_TARGET_CONTROL && business_events[1].sequence == 1U);
    CHECK(business_events[2].service == RUNTIME_TARGET_STORAGE && !business_events[2].simulated_running);
    CHECK(AppV3Business_Unsubscribe(ticket) == SYS_OK && published_count == 1U);
    return 0U;
}

static uint32_t Test_BusinessBackpressure(const AppV3Context_t *context)
{
    uint32_t ticket, consumed, command_index;
    CHECK(Test_StartBusiness(context) == 0U);
    notify_fails = true;
    CHECK(AppV3Business_Subscribe(RUNTIME_TARGET_COUNT, Test_StoreEvent, NULL, &ticket) == SYS_OK);

    //先占满Control结果队列；Camera完成首项后必须保留结果并停止取后续命令
    for (command_index = 1U; command_index <= 4U; ++command_index)
        CHECK(UsbService_Request(command_index, USB_SERVICE_QUERY, 0U) == SYS_OK);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_USB) == SYS_OK);
    for (command_index = 1U; command_index <= 4U; ++command_index)
        CHECK(CameraService_Request(command_index, CAMERA_SERVICE_QUERY, 0U) == SYS_OK);
    CHECK(CameraService_Request(5U, CAMERA_SERVICE_QUERY, 0U) == SYS_ERR_BUSY);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_CAMERA) == SYS_ERR_BUSY);
    CHECK(AppV3Business_Drain(&consumed) == SYS_OK && consumed == 4U);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_CAMERA) == SYS_OK);
    CHECK(AppV3Business_Drain(&consumed) == SYS_OK && consumed == 4U && business_event_count == 8U);
    for (command_index = 0U; command_index < 4U; ++command_index)
    {
        const AppV3ServiceEvent_t *event = &business_events[4U + command_index];
        CHECK(event->service == RUNTIME_TARGET_CAMERA && event->command_id == command_index + 1U &&
            event->sequence == command_index + 1U);
    }
    CHECK(CameraService_Request(5U, CAMERA_SERVICE_QUERY, 0U) == SYS_OK);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_CAMERA) == SYS_OK);
    CHECK(AppV3Business_Drain(&consumed) == SYS_OK && consumed == 1U && business_event_count == 9U);
    CHECK(AppV3Business_Unsubscribe(ticket) == SYS_OK);
    return 0U;
}

static uint32_t Test_BusinessCommandFaults(const AppV3Context_t *context)
{
    uint32_t ticket, consumed;
    CHECK(Test_StartBusiness(context) == 0U);
    CHECK(AppV3Business_Subscribe(RUNTIME_TARGET_STORAGE, Test_StoreEvent, NULL, &ticket) == SYS_OK);
    CHECK(StorageService_Request(0U, STORAGE_SERVICE_QUERY, 0U) == SYS_ERR_INVALID_ARG);
    CHECK(StorageService_Request(3U, STORAGE_SERVICE_MOCK_START, 0U) == SYS_OK);
    CHECK(StorageService_Request(3U, STORAGE_SERVICE_MOCK_STOP, 0U) == SYS_OK);
    CHECK(StorageService_Request(1U, STORAGE_SERVICE_MOCK_STOP, 0U) == SYS_OK);
    CHECK(StorageService_Request(4U, (StorageServiceOperation_t)99U, 0U) == SYS_OK);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_STORAGE) == SYS_OK);
    CHECK(AppV3Business_Drain(&consumed) == SYS_OK && consumed == 4U);
    CHECK(business_events[0].result == SYS_OK && business_events[0].simulated_running);
    CHECK(business_events[1].result == SYS_ERR_ALREADY_EXISTS && business_events[1].simulated_running);
    CHECK(business_events[2].result == SYS_ERR_PROTOCOL && business_events[2].simulated_running);
    CHECK(business_events[3].result == SYS_ERR_NOT_SUPPORTED && business_events[3].simulated_running);

    CHECK(StorageService_Request(5U, STORAGE_SERVICE_MOCK_FAIL, 0U) == SYS_OK);
    CHECK(StorageService_Request(6U, STORAGE_SERVICE_SET_PARAMETER, 65U) == SYS_OK);
    CHECK(StorageService_Request(7U, STORAGE_SERVICE_MOCK_STOP, 0U) == SYS_OK);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_STORAGE) == SYS_OK);
    CHECK(AppV3Business_Drain(&consumed) == SYS_OK && consumed == 3U);
    CHECK(business_events[4].result == SYS_ERR_IO && business_events[4].simulated_running);
    CHECK(business_events[5].result == SYS_ERR_INVALID_ARG && business_events[5].parameter == 0U);
    CHECK(business_events[6].result == SYS_OK && !business_events[6].simulated_running);
    CHECK(AppV3Business_Unsubscribe(ticket) == SYS_OK);
    return 0U;
}

typedef struct
{
    uint32_t removed;
    uint32_t added;
} TestSubscriptionChange_t;

//当前订阅可以撤销下一项；后来注册的回调从下一事件开始生效
static SysResult_t Test_ChangeSubscription(const AppV3ServiceEvent_t *event, void *context)
{
    TestSubscriptionChange_t *change = context;
    uint32_t recursive_count;
    (void)event;
    if (AppV3Business_Drain(&recursive_count) != SYS_ERR_BUSY)
        return SYS_ERR_PROTOCOL;
    if (change->added != 0U)
        return SYS_OK;
    if (AppV3Business_Unsubscribe(change->removed) != SYS_OK)
        return SYS_ERR_PROTOCOL;
    if (AppV3Business_Subscribe(RUNTIME_TARGET_CAMERA, Test_StoreEvent, NULL, &change->added) != SYS_OK)
        return SYS_ERR_PROTOCOL;
    return SYS_ERR_IO;
}

static uint32_t Test_BusinessSubscribers(const AppV3Context_t *context)
{
    TestSubscriptionChange_t change = {0};
    AppV3ServiceInstance_t replacement = {RUNTIME_TARGET_CAMERA, context};
    AppV3BusinessDiagnostics_t diagnostics;
    AppMessage_t unknown = {.type = APP_MESSAGE_START, .source = RUNTIME_TARGET_CAMERA, .command_id = 99U};
    uint32_t first, duplicate, consumed;

    CHECK(Test_StartBusiness(context) == 0U);
    CHECK(AppV3Business_Subscribe(RUNTIME_TARGET_CAMERA, Test_ChangeSubscription, &change, &first) == SYS_OK);
    CHECK(AppV3Business_Subscribe(RUNTIME_TARGET_CAMERA, Test_StoreEvent, NULL, &change.removed) == SYS_OK);
    CHECK(AppV3Business_Subscribe(RUNTIME_TARGET_CAMERA, Test_StoreEvent, NULL, &duplicate) == SYS_ERR_ALREADY_EXISTS);
    owner = RUNTIME_TARGET_CAMERA;
    CHECK(AppV3Business_Unsubscribe(first) == SYS_ERR_INVALID_STATE);
    CHECK(App_MessagesSend(APP_CHANNEL_CONTROL, &unknown) == SYS_OK);
    owner = RUNTIME_TARGET_CONTROL;
    CHECK(AppV3Business_Drain(&consumed) == SYS_ERR_PROTOCOL && consumed == 1U);
    CHECK(CameraService_Request(1U, CAMERA_SERVICE_QUERY, 0U) == SYS_OK);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_CAMERA) == SYS_OK);
    CHECK(AppV3Business_Drain(&consumed) == SYS_ERR_IO && consumed == 1U && business_event_count == 0U);
    CHECK(change.added != 0U && change.added != change.removed);
    CHECK(AppV3Business_Unsubscribe(change.removed) == SYS_ERR_NOT_FOUND);
    CHECK(CameraService_Request(2U, CAMERA_SERVICE_QUERY, 0U) == SYS_OK);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_CAMERA) == SYS_OK);
    CHECK(AppV3Business_Drain(&consumed) == SYS_OK && consumed == 1U && business_event_count == 1U);
    CHECK(business_events[0].result == SYS_OK && business_events[0].command_id == 2U);
    CHECK(AppV3Business_GetDiagnostics(&diagnostics) == SYS_OK && diagnostics.accepted == 2U &&
        diagnostics.rejected == 1U && diagnostics.callback_failures == 1U &&
        diagnostics.rejected_command_id == 99U && diagnostics.rejected_type == APP_MESSAGE_START && diagnostics.first_error == SYS_ERR_PROTOCOL);
    CHECK(AppV3Business_Unsubscribe(first) == SYS_OK && AppV3Business_Unsubscribe(change.added) == SYS_OK);
    owner = RUNTIME_TARGET_CAMERA;
    CHECK(AppV3Record_Destroy(owner) == SYS_OK);
    owner = RUNTIME_TARGET_CONTROL;
    CHECK(CameraService_Request(3U, CAMERA_SERVICE_QUERY, 0U) == SYS_ERR_BUSY);
    owner = RUNTIME_TARGET_CAMERA;
    CHECK(AppV3Record_Create(owner, &replacement) == SYS_OK);
    owner = RUNTIME_TARGET_CONTROL;
    CHECK(CameraService_Request(3U, CAMERA_SERVICE_QUERY, 0U) == SYS_ERR_INVALID_STATE);
    owner = RUNTIME_TARGET_CAMERA;
    CHECK(AppV3Record_Destroy(owner) == SYS_OK);
    AppV3_CameraDiscard();
    return 0U;
}

static uint32_t Test_BusinessStartupFailure(const AppV3Context_t *context, bool record_failure)
{
    AppV3Queue_t reclaimed;
    AppV3RecordRef_t reference = {0};

    scheduler = taskSCHEDULER_RUNNING;
    CHECK(CameraService_Request(1U, CAMERA_SERVICE_QUERY, 0U) == SYS_ERR_BUSY);
    owner = RUNTIME_TARGET_USB;
    CHECK(AppV3_InitService(owner, context) == SYS_OK);
    CHECK(AppV3_ReportInitialization(owner, SYS_OK) == SYS_OK);
    owner = RUNTIME_TARGET_CONTROL;
    CHECK(UsbService_Request(1U, USB_SERVICE_QUERY, 0U) == SYS_ERR_BUSY);

    owner = RUNTIME_TARGET_CAMERA;
    reject_event_subscribe = !record_failure;
    reject_next_queue_send = record_failure;
    CHECK(AppV3_InitService(owner, context) == (record_failure ? SYS_ERR_BUSY : SYS_ERR_IO));
    reject_event_subscribe = false;
    CHECK(AppV3_ReportInitialization(owner, record_failure ? SYS_ERR_BUSY : SYS_ERR_IO) == SYS_OK);
    owner = RUNTIME_TARGET_CONTROL;
    CHECK(AppV3_DrainInitialization() == SYS_OK && !AppV3_BusinessReady());
    CHECK(AppV3Record_Open(RUNTIME_TARGET_CAMERA, 0U, &reference) == (record_failure ? SYS_ERR_BUSY : SYS_ERR_IO));
    CHECK(reference.ticket == 0U);
    CHECK(UsbService_Request(2U, USB_SERVICE_QUERY, 0U) == SYS_ERR_BUSY);
    CHECK(AppV3Queue_Alloc(sizeof(AppMessage_t), 4U, &reclaimed) == SYS_OK);
    CHECK(AppV3Queue_Free(reclaimed) == SYS_OK);
    CHECK(g_failure == 0U);
    return 0U;
}

//IPC在没有订阅者且通知失败时，仍能取得自己的ACK与FAULT
static uint32_t Test_IpcResult(const AppV3Context_t *context)
{
    AppV3ServiceEvent_t event;
    uint32_t consumed;

    CHECK(Test_StartBusiness(context) == 0U);
    notify_fails = true;
    owner = RUNTIME_TARGET_IPC;
    CHECK(CameraService_Request(1U, CAMERA_SERVICE_MOCK_START, 0U) == SYS_OK);
    CHECK(AppV3Business_TakeResult(RUNTIME_TARGET_CAMERA, 1U, &event) == SYS_ERR_BUSY);
    CHECK(AppV3Business_TakeResult(RUNTIME_TARGET_CAMERA, 9U, &event) == SYS_ERR_NOT_FOUND);
    CHECK(StorageService_Request(1U, STORAGE_SERVICE_QUERY, 0U) == SYS_ERR_BUSY);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_CAMERA) == SYS_OK);
    CHECK(AppV3Business_Drain(&consumed) == SYS_OK && consumed == 1U);
    owner = RUNTIME_TARGET_IPC;
    CHECK(CameraService_Request(2U, CAMERA_SERVICE_MOCK_FAIL, 0U) == SYS_ERR_BUSY);
    CHECK(AppV3Business_TakeResult(RUNTIME_TARGET_CAMERA, 1U, &event) == SYS_OK &&
        event.requester == RUNTIME_TARGET_IPC && event.command_id == 1U &&
        event.result == SYS_OK && event.simulated_running);
    CHECK(AppV3Business_TakeResult(RUNTIME_TARGET_CAMERA, 1U, &event) == SYS_ERR_NOT_FOUND);

    CHECK(CameraService_Request(2U, CAMERA_SERVICE_MOCK_FAIL, 0U) == SYS_OK);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_CAMERA) == SYS_OK);
    CHECK(AppV3Business_Drain(&consumed) == SYS_OK && consumed == 1U);
    owner = RUNTIME_TARGET_IPC;
    CHECK(AppV3Business_TakeResult(RUNTIME_TARGET_CAMERA, 2U, &event) == SYS_OK &&
        event.result == SYS_ERR_IO && event.command_id == 2U);
    return 0U;
}

//同一个Service收到两个任务的相同编号，仅对应任务能取出各自结果
static uint32_t Test_SharedCommandId(const AppV3Context_t *context)
{
    AppV3ServiceEvent_t event;
    uint32_t consumed;

    CHECK(Test_StartBusiness(context) == 0U);
    owner = RUNTIME_TARGET_USB;
    CHECK(CameraService_Request(5U, CAMERA_SERVICE_MOCK_FAIL, 0U) == SYS_OK);
    owner = RUNTIME_TARGET_IPC;
    CHECK(CameraService_Request(5U, CAMERA_SERVICE_QUERY, 0U) == SYS_OK);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_CAMERA) == SYS_OK);
    CHECK(AppV3Business_Drain(&consumed) == SYS_OK && consumed == 2U);
    owner = RUNTIME_TARGET_STORAGE;
    CHECK(AppV3Business_TakeResult(RUNTIME_TARGET_CAMERA, 5U, &event) == SYS_ERR_NOT_FOUND);
    owner = RUNTIME_TARGET_IPC;
    CHECK(AppV3Business_TakeResult(RUNTIME_TARGET_CAMERA, 5U, &event) == SYS_OK &&
        event.requester == RUNTIME_TARGET_IPC && event.result == SYS_OK && event.command_id == 5U);
    owner = RUNTIME_TARGET_USB;
    CHECK(AppV3Business_TakeResult(RUNTIME_TARGET_CAMERA, 5U, &event) == SYS_OK &&
        event.requester == RUNTIME_TARGET_USB && event.result == SYS_ERR_IO && event.command_id == 5U);
    return 0U;
}

typedef struct
{
    uint32_t ticket;
    uint32_t calls;
    bool context_live;
} TestActiveSubscription_t;

static SysResult_t Test_ActiveCallback(const AppV3ServiceEvent_t *event, void *context)
{
    TestActiveSubscription_t *active = context;
    SysResult_t cross_task_result;
    (void)event;

    //模拟Control回调执行期间被IPC抢占，外任务不能撤销Control的Context
    owner = RUNTIME_TARGET_IPC;
    cross_task_result = AppV3Business_Unsubscribe(active->ticket);
    owner = RUNTIME_TARGET_CONTROL;
    if (cross_task_result != SYS_ERR_INVALID_STATE ||
        AppV3Business_Unsubscribe(active->ticket) != SYS_ERR_BUSY || !active->context_live)
        return SYS_ERR_PROTOCOL;
    ++active->calls;
    return SYS_OK;
}

static uint32_t Test_UnsubscribeDuringCallback(const AppV3Context_t *context)
{
    TestActiveSubscription_t active = {.context_live = true};
    uint32_t consumed;

    CHECK(Test_StartBusiness(context) == 0U);
    CHECK(AppV3Business_Subscribe(RUNTIME_TARGET_CAMERA, Test_ActiveCallback, &active, &active.ticket) == SYS_OK);
    CHECK(CameraService_Request(1U, CAMERA_SERVICE_QUERY, 0U) == SYS_OK);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_CAMERA) == SYS_OK);
    CHECK(AppV3Business_Drain(&consumed) == SYS_OK && consumed == 1U && active.calls == 1U);
    CHECK(AppV3Business_Unsubscribe(active.ticket) == SYS_OK);
    active.context_live = false;
    CHECK(CameraService_Request(2U, CAMERA_SERVICE_QUERY, 0U) == SYS_OK);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_CAMERA) == SYS_OK);
    CHECK(AppV3Business_Drain(&consumed) == SYS_OK && consumed == 1U && active.calls == 1U);
    return 0U;
}

//Control业务队列满时，Service保留原FAULT，结果槽一直处于待完成
static uint32_t Test_ResultBackpressure(const AppV3Context_t *context)
{
    AppV3ServiceEvent_t event;
    uint32_t consumed, command_id;

    CHECK(Test_StartBusiness(context) == 0U);
    notify_fails = true;
    for (command_id = 1U; command_id <= 4U; ++command_id)
        CHECK(UsbService_Request(command_id, USB_SERVICE_QUERY, 0U) == SYS_OK);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_USB) == SYS_OK);
    owner = RUNTIME_TARGET_IPC;
    CHECK(CameraService_Request(1U, CAMERA_SERVICE_MOCK_FAIL, 0U) == SYS_OK);
    owner = RUNTIME_TARGET_CONTROL;
    CHECK(CameraService_Request(1U, CAMERA_SERVICE_QUERY, 0U) == SYS_OK);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_CAMERA) == SYS_ERR_BUSY);
    owner = RUNTIME_TARGET_IPC;
    CHECK(AppV3Business_TakeResult(RUNTIME_TARGET_CAMERA, 1U, &event) == SYS_ERR_BUSY);
    owner = RUNTIME_TARGET_CONTROL;
    CHECK(AppV3Business_Drain(&consumed) == SYS_OK && consumed == 4U);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_CAMERA) == SYS_OK);
    CHECK(AppV3Business_Drain(&consumed) == SYS_OK && consumed == 2U);
    owner = RUNTIME_TARGET_IPC;
    CHECK(AppV3Business_TakeResult(RUNTIME_TARGET_CAMERA, 1U, &event) == SYS_OK &&
        event.requester == RUNTIME_TARGET_IPC && event.sequence == 1U && event.result == SYS_ERR_IO);
    CHECK(AppV3Business_TakeResult(RUNTIME_TARGET_CAMERA, 1U, &event) == SYS_ERR_NOT_FOUND);
    return 0U;
}

//请求未入私有队列时撤销预留，调用任务可原样重试编号
static uint32_t Test_RequestReservationRollback(const AppV3Context_t *context)
{
    AppV3ServiceEvent_t event;
    uint32_t consumed, command_id;

    CHECK(Test_StartBusiness(context) == 0U);
    for (command_id = 1U; command_id <= 4U; ++command_id)
        CHECK(StorageService_Request(command_id, STORAGE_SERVICE_QUERY, 0U) == SYS_OK);
    owner = RUNTIME_TARGET_IPC;
    CHECK(StorageService_Request(1U, STORAGE_SERVICE_QUERY, 0U) == SYS_ERR_BUSY);
    CHECK(AppV3Business_TakeResult(RUNTIME_TARGET_STORAGE, 1U, &event) == SYS_ERR_NOT_FOUND);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_STORAGE) == SYS_OK);
    CHECK(AppV3Business_Drain(&consumed) == SYS_OK && consumed == 4U);
    owner = RUNTIME_TARGET_IPC;
    CHECK(StorageService_Request(1U, STORAGE_SERVICE_QUERY, 0U) == SYS_OK);
    CHECK(Test_ServiceStep(RUNTIME_TARGET_STORAGE) == SYS_OK);
    CHECK(AppV3Business_Drain(&consumed) == SYS_OK && consumed == 1U);
    owner = RUNTIME_TARGET_IPC;
    CHECK(AppV3Business_TakeResult(RUNTIME_TARGET_STORAGE, 1U, &event) == SYS_OK &&
        event.result == SYS_OK && event.sequence == 5U);
    return 0U;
}

uint32_t AppV3_TestRun(uint32_t scenario)
{
    const AppV3Context_t *context;
    AppMessage_t startup_message = {.type = APP_MESSAGE_START};
    uint32_t filler_index;

    CHECK(sizeof(AppV3ServiceCommand_t) == 16U);
    CHECK(sizeof(AppMessage_t) == 52U);
    CHECK(offsetof(AppMessage_t, frame) == 28U);
    CHECK(sizeof(IpcApplicationStatus_t) == 80U);
    fixture_video.output = VIDEO_OUTPUT_USB_UVC;
    fixture_storage.record_fps = 25U;

    if (scenario == 0U)
    {
        fixture_missing_config = true;
        CHECK(App_Init() == SYS_ERR_INVALID_STATE);
        return 0U;
    }

    if (scenario >= 15U && scenario <= 17U)
    {
        check_boot_order = true;
        fail_thread = scenario == 16U ? 2U : 0U;
        if (scenario == 16U)
        {
            CHECK(App_TasksCreate() == SYS_ERR_NO_RESOURCE);
            CHECK(App_TasksRecordInitialization(SYS_OK) == SYS_ERR_INVALID_STATE);
            CHECK(App_TasksReleaseBoot() == SYS_ERR_INVALID_STATE);
            CHECK(g_app_tasks_debug.created_tasks == 1U && g_app_tasks_debug.boot_released == 0U);
            return 0U;
        }

        CHECK(App_TasksCreate() == SYS_OK && g_app_tasks_debug.created_tasks == 5U);
        CHECK(App_TasksCreate() == SYS_ERR_INVALID_STATE && created_threads == 5U);
        CHECK(App_TasksReleaseBoot() == SYS_ERR_INVALID_STATE);
        if (scenario == 17U)
        {
            CHECK(App_TasksRecordInitialization(SYS_ERR_IO) == SYS_ERR_IO);
            scheduler = taskSCHEDULER_RUNNING;
            CHECK(App_TasksReleaseBoot() == SYS_ERR_INVALID_STATE);
            CHECK(g_app_tasks_debug.prepared == 0U && g_app_tasks_debug.boot_released == 0U);
            return 0U;
        }

        CHECK(App_Init() == SYS_OK);
        CHECK(App_TasksRecordInitialization(SYS_OK) == SYS_OK && g_app_tasks_debug.prepared == 1U);
        CHECK(App_TasksReleaseBoot() == SYS_ERR_INVALID_STATE);
        scheduler = taskSCHEDULER_RUNNING;
        CHECK(App_TasksReleaseBoot() == SYS_OK && g_app_tasks_debug.boot_released == 1U);
        return 0U;
    }

    if (scenario == 18U)
        return Test_PrivateQueuesAfterStartup();

    CHECK(App_Init() == SYS_OK);
    context = AppV3_GetContext();
    CHECK(context->video == &fixture_video && context->storage == &fixture_storage);
    CHECK(App_MessagesInit() == SYS_OK);

    switch (scenario)
    {
        case 24U: return Test_BusinessIsolation(context);
        case 25U: return Test_BusinessBackpressure(context);
        case 26U: return Test_BusinessCommandFaults(context);
        case 27U: return Test_BusinessSubscribers(context);
        case 28U:
        case 29U: return Test_BusinessStartupFailure(context, scenario == 29U);
        case 30U:
            CHECK(ControlTask_Create() == SYS_OK);
            scheduler = taskSCHEDULER_RUNNING;
            run_control_business = true;
            notify_fails = true;
            if (setjmp(task_exit) == 0)
                scheduled_entry(scheduled_argument);
            CHECK(waited == 3U && business_event_count == 2U && published_count == 1U);
            CHECK(business_events[0].command_id == 1U && business_events[0].simulated_running);
            CHECK(business_events[1].command_id == 2U && business_events[1].simulated_running);
            CHECK(g_failure == 0U);
            return 0U;
        case 31U:
        {
            uint32_t created;
            CHECK(AppV3_StartServices(&created) == SYS_OK && created == 3U);
            CHECK(scheduled_entry == AppV3_StorageRun);
            scheduler = taskSCHEDULER_RUNNING;
            CHECK(App_TasksReleaseBoot() == SYS_OK);
            owner = RUNTIME_TARGET_CAMERA;
            CHECK(AppV3_InitService(owner, context) == SYS_OK && AppV3_ReportInitialization(owner, SYS_OK) == SYS_OK);
            owner = RUNTIME_TARGET_USB;
            CHECK(AppV3_InitService(owner, context) == SYS_OK && AppV3_ReportInitialization(owner, SYS_OK) == SYS_OK);
            owner = RUNTIME_TARGET_STORAGE;
            run_storage_business = true;
            notify_fails = true;
            if (setjmp(task_exit) == 0)
                scheduled_entry(scheduled_argument);
            CHECK(waited == 4U && business_event_count == 2U && g_failure == 0U);
            CHECK(business_events[0].simulated_running && business_events[0].command_id == 1U);
            CHECK(!business_events[1].simulated_running && business_events[1].command_id == 2U);
            return 0U;
        }
        case 32U: return Test_IpcResult(context);
        case 33U: return Test_SharedCommandId(context);
        case 34U: return Test_UnsubscribeDuringCallback(context);
        case 35U:
            scheduler = taskSCHEDULER_RUNNING;
            return Test_ExistingMessageChannels();
        case 36U: return Test_ResultBackpressure(context);
        case 37U: return Test_RequestReservationRollback(context);
        default: break;
    }
    if (scenario >= 19U)
    {
        scheduler = taskSCHEDULER_RUNNING;
        if (scenario <= 21U)
            return Test_EventLoopR1(scenario - 19U);
        return Test_RecordSignalFailure(scenario == 23U, context);
    }

    if (scenario == 8U || scenario == 9U)
    {
        AppV3Queue_t queue, replacement;
        UBaseType_t capacity, space;
        AppMessage_t item = {.type = APP_MESSAGE_START};
        scheduler = taskSCHEDULER_RUNNING;
        AppV3Queue_t reserved[3U];
        CHECK(AppV3_GetStartupQueue(&queue) == SYS_OK);
        for (uint32_t private_index = 0U; private_index < 3U; ++private_index)
            CHECK(AppV3Queue_Alloc(sizeof(item), 4U, &reserved[private_index]) == SYS_OK);
        CHECK(AppV3Queue_Alloc(sizeof(item), 1U, &replacement) == SYS_ERR_NO_RESOURCE);
        CHECK(AppV3Queue_Query(queue, &capacity, &space) == SYS_OK && capacity == 4U && space == 4U);
        CHECK(AppV3Queue_Subscribe(queue, owner) == SYS_OK);
        CHECK(AppV3Queue_Free(queue) == SYS_ERR_BUSY);
        notify_fails = true;
        item.command_id = 11U;
        CHECK(AppV3Queue_Put(queue, &item, 0U) == SYS_OK);
        item.command_id = 12U;
        CHECK(AppV3Queue_Put(queue, &item, 0U) == SYS_OK);
        CHECK(AppV3Queue_Put(queue, &item, 0U) == SYS_OK);
        CHECK(AppV3Queue_Put(queue, &item, 0U) == SYS_OK);
        CHECK(AppV3Queue_Put(queue, &item, 0U) == SYS_ERR_BUSY);
        CHECK(AppV3Queue_Get(queue, &item, 0U) == SYS_OK && item.command_id == 11U);
        if (scenario == 9U)
        {
            CHECK(AppV3Queue_GetFromIsr(queue, &item) == SYS_ERR_INVALID_STATE);
            exception = 3U;
            CHECK(AppV3Queue_GetFromIsr(queue, &item) == SYS_ERR_INVALID_STATE);
            exception = 16U;
            --irq_priority;
            CHECK(AppV3Queue_GetFromIsr(queue, &item) == SYS_ERR_INVALID_STATE);
            ++irq_priority;
            CHECK(AppV3Queue_Free(queue) == SYS_ERR_INVALID_STATE);
            CHECK(AppV3Queue_GetFromIsr(queue, &item) == SYS_OK && item.command_id == 12U);
            CHECK(AppV3Queue_PutFromIsr(queue, &item) == SYS_OK);
            exception = 0U;
        }
        CHECK(AppV3Queue_Unsubscribe(queue, owner) == SYS_OK);
        CHECK(AppV3Queue_Free(queue) == SYS_ERR_BUSY);
        CHECK(AppV3Queue_Get(queue, &item, 0U) == SYS_OK && item.command_id == 12U);
        CHECK(AppV3Queue_Get(queue, &item, 0U) == SYS_OK && item.command_id == 12U);
        CHECK(AppV3Queue_Get(queue, &item, 0U) == SYS_OK && item.command_id == 12U);
        blocking_probe = true;
        pinned_queue = queue;
        CHECK(AppV3Queue_Get(queue, &item, 1U) == SYS_ERR_TIMEOUT && during_wait == SYS_ERR_BUSY);
        blocking_probe = false;
        CHECK(AppV3Queue_Free(queue) == SYS_OK);
        CHECK(AppV3Queue_Free(queue) == SYS_ERR_NOT_FOUND);
        CHECK(AppV3Queue_Alloc(sizeof(item), 2U, &replacement) == SYS_OK);
        CHECK(AppV3Queue_Get(queue, &item, 0U) == SYS_ERR_NOT_FOUND);
        CHECK(g_failure == 0U);
        return 0U;
    }

    if (scenario == 10U || scenario == 11U)
    {
        AppV3RecordRef_t references[5] = {{0}}, stale;
        RuntimeEventTarget_t camera = RUNTIME_TARGET_CAMERA;
        uint32_t holder_index;
        scheduler = taskSCHEDULER_RUNNING;
        CHECK(AppV3Record_Open(camera, 0U, &references[0]) == SYS_ERR_BUSY);
        blocking_probe = true;
        CHECK(AppV3Record_Open(camera, 1U, &references[0]) == SYS_ERR_TIMEOUT);
        blocking_probe = false;
        if (scenario == 11U)
        {
            owner = camera;
            fixture_camera_result = SYS_ERR_IO;
            CHECK(AppV3_InitService(owner, context) == SYS_ERR_IO);
            owner = RUNTIME_TARGET_CONTROL;
            CHECK(AppV3Record_Open(camera, 1U, &references[0]) == SYS_ERR_IO);
            CHECK(AppV3Record_Destroy(camera) == SYS_ERR_INVALID_STATE);
            CHECK(g_failure == 0U);
            return 0U;
        }
        record_probe = true;
        record_context = context;
        CHECK(AppV3Record_Open(camera, 1U, &references[0]) == SYS_OK);
        record_probe = false;
        CHECK(references[0].instance->config == context && references[0].instance->owner == camera);
        for (holder_index = 1U; holder_index < 4U; ++holder_index)
            CHECK(AppV3Record_Open(camera, 0U, &references[holder_index]) == SYS_OK);
        CHECK(AppV3Record_Open(camera, 0U, &references[4]) == SYS_ERR_NO_RESOURCE);
        owner = camera;
        CHECK(AppV3Record_Close(&references[0]) == SYS_ERR_NOT_FOUND);
        CHECK(AppV3_InitService(owner, context) == SYS_ERR_ALREADY_EXISTS);
        CHECK(AppV3Record_Destroy(camera) == SYS_ERR_BUSY);
        owner = RUNTIME_TARGET_CONTROL;
        stale = references[0];
        for (holder_index = 0U; holder_index < 4U; ++holder_index)
            CHECK(AppV3Record_Close(&references[holder_index]) == SYS_OK);
        CHECK(AppV3Record_Close(&stale) == SYS_ERR_NOT_FOUND);
        owner = camera;
        CHECK(AppV3Record_Destroy(camera) == SYS_OK);
        CHECK(AppV3Record_Open(camera, 0U, &references[0]) == SYS_ERR_BUSY);
        CHECK(g_failure == 0U);
        return 0U;
    }

    if (scenario == 12U)
    {
        AppV3EventLoop_t loop = {.initialized = false};
        AppV3Queue_t queue;
        AppMessage_t item = {.type = APP_MESSAGE_START};
        uint32_t events;
        scheduler = taskSCHEDULER_RUNNING;
        event_test = true;
        notify_fails = true;
        CHECK(AppV3_GetStartupQueue(&queue) == SYS_OK);
        item.command_id = 1U;
        CHECK(AppV3Queue_Put(queue, &item, 0U) == SYS_OK);
        CHECK(AppV3EventLoop_Init(&loop, owner) == SYS_OK);
        CHECK(AppV3EventLoop_Subscribe(&loop, queue, Test_QueueReadable, NULL) == SYS_OK);
        item.command_id = 2U;
        CHECK(AppV3Queue_Put(queue, &item, 0U) == SYS_OK);
        CHECK(AppV3EventLoop_Step(&loop, 20U, &events) == SYS_OK && event_count == 2U && waited == 1U);
        CHECK(AppV3EventLoop_Step(&loop, 20U, &events) == SYS_ERR_TIMEOUT && waited == 2U);
        exception = 16U;
        CHECK(AppV3EventLoop_Step(&loop, 20U, &events) == SYS_ERR_INVALID_STATE && waited == 2U);
        exception = 0U;
        owner = RUNTIME_TARGET_CAMERA;
        CHECK(AppV3EventLoop_Unsubscribe(&loop) == SYS_ERR_INVALID_STATE);
        owner = RUNTIME_TARGET_CONTROL;
        CHECK(AppV3EventLoop_Unsubscribe(&loop) == SYS_OK);
        CHECK(AppV3Queue_Free(queue) == SYS_OK);
        return 0U;
    }

    if (scenario == 1U)
    {
        CHECK(AppV3_FindService(RUNTIME_TARGET_CONTROL) == NULL);
        CHECK(AppV3_FindService(RUNTIME_TARGET_IPC) == NULL);
        CHECK(AppV3_FindService(RUNTIME_TARGET_COUNT) == NULL);
        CHECK(*AppV3_FindService(RUNTIME_TARGET_CAMERA) == RUNTIME_TARGET_CAMERA);
        CHECK(*AppV3_FindService(RUNTIME_TARGET_USB) == RUNTIME_TARGET_USB);
        CHECK(*AppV3_FindService(RUNTIME_TARGET_STORAGE) == RUNTIME_TARGET_STORAGE);
        CHECK(AppV3_InitService(RUNTIME_TARGET_USB, context) == SYS_ERR_INVALID_STATE);
        CHECK(AppV3_ReportInitialization(RUNTIME_TARGET_USB, SYS_OK) == SYS_ERR_INVALID_STATE);
        return 0U;
    }

    if (scenario == 7U)
    {
        uint32_t created;
        fail_thread = 2U;
        CHECK(AppV3_StartServices(&created) == SYS_ERR_NO_RESOURCE);
        CHECK(created == 1U && created_threads == 2U);
        CHECK(AppV3_StartServices(&created) == SYS_ERR_ALREADY_EXISTS && created == 0U);
        return 0U;
    }
    if (scenario == 5U)
    {
        uint32_t created;
        CHECK(AppV3_StartServices(&created) == SYS_OK && created == 3U);
        CHECK(AppV3_StartServices(&created) == SYS_ERR_ALREADY_EXISTS && created == 0U);
        CHECK(scheduled_stack_words == 2048U && scheduled_priority == 1U);
    }

    if (scenario == 6U || scenario == 14U)
    {
        CHECK(ControlTask_Create() == SYS_OK);
        CHECK(scheduled_stack_words == 768U && scheduled_priority == 2U);
        scheduler = taskSCHEDULER_RUNNING;
        owner = RUNTIME_TARGET_CAMERA;
        notify_fails = true;
        if (scenario == 6U)
        {
            fixture_camera_result = SYS_ERR_IO;
            CHECK(AppV3_InitService(owner, context) == SYS_ERR_IO);
            CHECK(AppV3_ReportInitialization(owner, SYS_ERR_IO) == SYS_OK);
        }
        owner = RUNTIME_TARGET_CONTROL;
        if (setjmp(task_exit) == 0)
            scheduled_entry(scheduled_argument);
        CHECK(waited == 2U && published_count == 1U);
        CHECK(published.last_fault == (scenario == 6U ? SYS_ERR_IO : SYS_ERR_TIMEOUT));
        CHECK(g_failure == 0U);
        return 0U;
    }

    scheduler = taskSCHEDULER_RUNNING;
    if (scenario == 2U)
    {
        owner = RUNTIME_TARGET_CAMERA;
        CHECK(AppV3_InitService(owner, context) == SYS_OK);
        CHECK(AppV3_InitService(owner, context) == SYS_ERR_ALREADY_EXISTS);
        CHECK(AppV3_ReportInitialization(owner, SYS_OK) == SYS_OK);
        owner = RUNTIME_TARGET_USB;
        CHECK(AppV3_InitService(owner, context) == SYS_OK);
        CHECK(AppV3_ReportInitialization(owner, SYS_OK) == SYS_OK);
        owner = RUNTIME_TARGET_STORAGE;
        CHECK(AppV3_InitService(owner, context) == SYS_OK);
        CHECK(AppV3_ReportInitialization(owner, SYS_OK) == SYS_OK);
        owner = RUNTIME_TARGET_CONTROL;
        CHECK(AppV3_DrainInitialization() == SYS_OK);
        CHECK(published_count == 1U && published.result == SYS_OK);
        return 0U;
    }

    if (scenario == 3U)
    {
        fixture_camera_result = SYS_ERR_IO;
        owner = RUNTIME_TARGET_CAMERA;
        CHECK(AppV3_InitService(owner, context) == SYS_ERR_IO);
        CHECK(AppV3_ReportInitialization(owner, SYS_ERR_IO) == SYS_OK);
        owner = RUNTIME_TARGET_CONTROL;
        CHECK(AppV3_DrainInitialization() == SYS_OK && published.result == SYS_ERR_IO);
        fixture_storage.record_fps = 0U;
        owner = RUNTIME_TARGET_STORAGE;
        CHECK(AppV3_InitService(owner, context) == SYS_ERR_INVALID_ARG);
        CHECK(AppV3_ReportInitialization(owner, SYS_ERR_INVALID_ARG) == SYS_OK);
        owner = RUNTIME_TARGET_USB;
        CHECK(AppV3_InitService(owner, context) == SYS_OK);
        CHECK(AppV3_ReportInitialization(owner, SYS_OK) == SYS_OK);
        owner = RUNTIME_TARGET_CONTROL;
        CHECK(AppV3_DrainInitialization() == SYS_OK && published.last_fault == SYS_ERR_IO);
        return 0U;
    }

    if (scenario == 4U)
    {
        owner = RUNTIME_TARGET_USB;
        notify_fails = true;
        CHECK(AppV3_ReportInitialization(owner, SYS_OK) == SYS_ERR_INVALID_STATE);
        CHECK(AppV3_InitService(owner, context) == SYS_OK);
        CHECK(AppV3_ReportInitialization(owner, SYS_OK) == SYS_OK);
        owner = RUNTIME_TARGET_CONTROL;
        CHECK(AppV3_DrainInitialization() == SYS_OK && published_count == 0U);
        CHECK(AppV3_DrainInitialization() == SYS_OK && published_count == 0U);
        return 0U;
    }

    if (scenario == 13U)
    {
        AppV3Queue_t queue;
        CHECK(AppV3_GetStartupQueue(&queue) == SYS_OK);
        startup_message.type = APP_MESSAGE_ACK;
        startup_message.source = RUNTIME_TARGET_STORAGE;
        CHECK(AppV3Queue_Put(queue, &startup_message, 0U) == SYS_OK);
        startup_message.type = APP_MESSAGE_START;
        startup_message.source = owner;
        CHECK(App_MessagesSend(APP_CHANNEL_CONTROL, &startup_message) == SYS_OK);
        owner = RUNTIME_TARGET_USB;
        CHECK(AppV3_InitService(owner, context) == SYS_OK);
        CHECK(AppV3_ReportInitialization(owner, SYS_OK) == SYS_OK);
        CHECK(AppV3_ReportInitialization(owner, SYS_OK) == SYS_OK);
        owner = RUNTIME_TARGET_CONTROL;
        CHECK(AppV3_DrainInitialization() == SYS_OK && published_count == 0U);
        CHECK(App_MessagesReceive(APP_CHANNEL_CONTROL, &startup_message));
        CHECK(startup_message.type == APP_MESSAGE_START);
        CHECK(AppV3_ExpireInitialization() == SYS_OK && published.result == SYS_ERR_TIMEOUT);
        owner = RUNTIME_TARGET_CAMERA;
        CHECK(AppV3_InitService(owner, context) == SYS_OK);
        CHECK(AppV3_ReportInitialization(owner, SYS_OK) == SYS_OK);
        owner = RUNTIME_TARGET_STORAGE;
        CHECK(AppV3_InitService(owner, context) == SYS_OK);
        CHECK(AppV3_ReportInitialization(owner, SYS_OK) == SYS_OK);
        owner = RUNTIME_TARGET_CONTROL;
        CHECK(AppV3_DrainInitialization() == SYS_OK && published_count == 1U);
        CHECK(published.result == SYS_ERR_TIMEOUT);
        return 0U;
    }

    CHECK(scenario == 5U);
    startup_message.source = RUNTIME_TARGET_CONTROL;
    startup_message.type = APP_MESSAGE_START;
    AppV3Queue_t queue;
    CHECK(AppV3_GetStartupQueue(&queue) == SYS_OK);
    for (filler_index = 0U; filler_index < 4U; ++filler_index)
        CHECK(AppV3Queue_Put(queue, &startup_message, 0U) == SYS_OK);

    //执行真实任务入口，模拟等待函数在已观察到重试后返回测试。
    owner = RUNTIME_TARGET_STORAGE;
    if (setjmp(task_exit) == 0)
        scheduled_entry(scheduled_argument);
    CHECK(waited == 4U && g_failure == 0U);
    owner = RUNTIME_TARGET_CONTROL;
    CHECK(App_MessagesReceive(APP_CHANNEL_STARTUP, &startup_message));
    CHECK(startup_message.type == APP_MESSAGE_ACK && startup_message.source == RUNTIME_TARGET_STORAGE);
    CHECK(!App_MessagesReceive(APP_CHANNEL_STARTUP, &startup_message));
    return 0U;
}
