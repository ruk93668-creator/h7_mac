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
static bool notify_fails;
static uint32_t exception, primask;
static uint32_t irq_priority = configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY;
static TickType_t test_tick;
static bool blocking_probe;
static AppV3Queue_t pinned_queue;
static SysResult_t during_wait;
static bool record_probe;
static const AppV3Context_t *record_context;

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
    (void)events;
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
        if (AppV3_InitService(owner, record_context) != SYS_OK ||
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

SysResult_t App_TasksReleaseBoot(void)
{
    return owner == RUNTIME_TARGET_CONTROL ? SYS_OK : SYS_ERR_INVALID_STATE;
}

void vTaskDelay(TickType_t ticks)
{
    (void)ticks;
    g_failure = __LINE__;
    longjmp(task_exit, 1);
}

SysResult_t App_EventsWait(RuntimeEventTarget_t target, uint32_t timeout_ms, uint32_t *events)
{
    if (event_test)
    {
        ++waited;
        return SYS_ERR_TIMEOUT;
    }

    if (target == RUNTIME_TARGET_CONTROL)
    {
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

uint32_t AppV3_TestRun(uint32_t scenario)
{
    const AppV3Context_t *context;
    AppMessage_t startup_message = {0};
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

    CHECK(App_Init() == SYS_OK);
    context = AppV3_GetContext();
    CHECK(context->video == &fixture_video && context->storage == &fixture_storage);
    CHECK(App_MessagesInit() == SYS_OK);

    if (scenario == 8U || scenario == 9U)
    {
        AppV3Queue_t queue, replacement;
        UBaseType_t capacity, space;
        AppMessage_t item = {0};
        scheduler = taskSCHEDULER_RUNNING;
        CHECK(AppV3_GetStartupQueue(&queue) == SYS_OK);
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
        AppV3EventLoop_t loop = {0};
        AppV3Queue_t queue;
        AppMessage_t item = {0};
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
