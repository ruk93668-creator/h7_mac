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

    if (waited != 4U || timeout_ms != APP_EVENTS_WAIT_FOREVER)
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

//私有槽全部占用后，逐条验证原业务通道的收发身份和消息
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
        {APP_CHANNEL_CONTROL, RUNTIME_TARGET_CAMERA, RUNTIME_TARGET_CONTROL},
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
        CHECK(App_MessagesSend(routes[route_index].channel, &message) == SYS_OK);
        owner = routes[route_index].receiver;
        CHECK(App_MessagesReceive(routes[route_index].channel, &message) &&
            message.source == routes[route_index].sender && message.command_id == route_index + 1U);
    }
    owner = RUNTIME_TARGET_CONTROL;
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
    AppMessage_t message = {.type = APP_MESSAGE_START};
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

    //从各自真实任务身份申请队列，同时保留三个已发布Record
    for (service_index = 0U; service_index < 3U; ++service_index)
    {
        CHECK(AppV3Record_Open(service_owners[service_index], 0U, &references[service_index]) == SYS_OK);
        owner = service_owners[service_index];
        CHECK(AppV3Queue_Alloc(sizeof(message), 4U, &service_queues[service_index]) == SYS_OK);
        message.command_id = service_index + 1U;
        CHECK(AppV3Queue_Put(service_queues[service_index], &message, 0U) == SYS_OK);
        owner = RUNTIME_TARGET_CONTROL;
    }
    CHECK(AppV3Queue_Alloc(sizeof(message), 4U, &excess) == SYS_ERR_NO_RESOURCE);
    CHECK(Test_ExistingMessageChannels() == 0U);
    for (service_index = 0U; service_index < 3U; ++service_index)
    {
        CHECK(AppV3Queue_Get(service_queues[service_index], &message, 0U) == SYS_OK &&
            message.command_id == service_index + 1U);
        CHECK(AppV3Record_Close(&references[service_index]) == SYS_OK);
    }
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

uint32_t AppV3_TestRun(uint32_t scenario)
{
    const AppV3Context_t *context;
    AppMessage_t startup_message = {.type = APP_MESSAGE_START};
    uint32_t filler_index;

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
