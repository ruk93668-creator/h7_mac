//复用历史夹具的真实 FreeRTOS Queue/List，调度与设备验证可注入。
#include "../CameraLifecycleC1/transport_test.c"
#include "../../CM7/ApplicationV3/Core/app_services.h"
#include "app_main.h"
#include "app_tasks.h"
#include "app_status.h"
#include "Tasks/storage_task.h"
#include "Tasks/control_task.h"
#include <setjmp.h>

static VideoConfig_t fixture_video;
static AppStorageConfig_t fixture_storage;
static bool fixture_missing_config;
static SysResult_t fixture_camera_result;
static AppControlStatus_t published;
static unsigned int published_count, waited;
static unsigned int scheduled_priority;
static uint32_t scheduled_stack_words;
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
    scheduled_entry = entry;
    scheduled_argument = argument;
    scheduled_priority = priority;
    scheduled_stack_words = stack_words;
    return (TaskHandle_t)task;
}

SysResult_t App_EventsAttach(RuntimeEventTarget_t target, TaskHandle_t handle)
{
    return (target == RUNTIME_TARGET_STORAGE || target == RUNTIME_TARGET_CONTROL) && handle != NULL
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
    if (target == RUNTIME_TARGET_CONTROL)
    {
        if (timeout_ms == 100U && waited++ == 0U)
            return SYS_ERR_TIMEOUT;
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

uint32_t AppV3_TestRun(uint32_t scenario)
{
    const AppV3Context_t *context;
    AppMessage_t startup_message = {0};
    uint32_t filler_index;

    CHECK(sizeof(AppMessage_t) == 52U);
    CHECK(sizeof(AppLifecycleMessage_t) == 56U);
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

    if (scenario == 5U)
    {
        CHECK(StorageTask_Create() == SYS_OK);
        CHECK(StorageTask_Create() == SYS_ERR_ALREADY_EXISTS);
        CHECK(scheduled_stack_words == 2048U && scheduled_priority == 1U);
    }

    if (scenario == 6U)
    {
        CHECK(ControlTask_Create() == SYS_OK);
        CHECK(scheduled_stack_words == 768U && scheduled_priority == 2U);
        scheduler = taskSCHEDULER_RUNNING;
        owner = RUNTIME_TARGET_CAMERA;
        notify_fails = true;
        CHECK(AppV3_ReportInitialization(owner, SYS_ERR_IO) == SYS_OK);
        owner = RUNTIME_TARGET_CONTROL;
        if (setjmp(task_exit) == 0)
            scheduled_entry(scheduled_argument);
        CHECK(waited == 2U && published_count == 1U && published.last_fault == SYS_ERR_IO);
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
        CHECK(AppV3_ReportInitialization(owner, SYS_OK) == SYS_OK);
        owner = RUNTIME_TARGET_CONTROL;
        CHECK(AppV3_DrainInitialization() == SYS_OK && published_count == 1U);
        CHECK(AppV3_DrainInitialization() == SYS_OK && published_count == 1U);
        return 0U;
    }

    CHECK(scenario == 5U);
    startup_message.source = RUNTIME_TARGET_CONTROL;
    startup_message.type = APP_MESSAGE_START;
    for (filler_index = 0U; filler_index < 4U; ++filler_index)
        CHECK(App_MessagesSend(APP_CHANNEL_CONTROL, &startup_message) == SYS_OK);

    //执行真实任务入口，模拟等待函数在已观察到重试后返回测试。
    owner = RUNTIME_TARGET_STORAGE;
    if (setjmp(task_exit) == 0)
        scheduled_entry(scheduled_argument);
    CHECK(waited == 4U && g_failure == 0U);
    owner = RUNTIME_TARGET_CONTROL;
    CHECK(App_MessagesReceive(APP_CHANNEL_CONTROL, &startup_message));
    CHECK(startup_message.type == APP_MESSAGE_ACK && startup_message.source == RUNTIME_TARGET_STORAGE);
    CHECK(!App_MessagesReceive(APP_CHANNEL_CONTROL, &startup_message));
    return 0U;
}
