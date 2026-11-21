#include "../Core/app_services.h"
#include "app_tasks.h"
#include "app_events.h"
#include "platform_sync.h"
#include "service_tasks.h"
#include <stddef.h>

//沿用四个任务的原栈容量和优先级。IPC 仍由原文件创建。
#define TASK_WORDS(stack) (sizeof(stack) / sizeof((stack)[0]))

static StackType_t s_camera_stack[1536U] __attribute__((aligned(8)));
static StackType_t s_usb_stack[2048U] __attribute__((aligned(8)));
static StackType_t s_storage_stack[2048U] __attribute__((aligned(8)));
static StackType_t s_control_stack[768U] __attribute__((aligned(8)));
static StaticTask_t s_task_controls[RUNTIME_TARGET_COUNT - 1U];
static bool s_task_attempted[RUNTIME_TARGET_COUNT - 1U];

static void AppV3_ServiceRun(void *argument)
{
    const RuntimeEventTarget_t *service_owner = argument;
    SysResult_t initialization = SYS_ERR_INVALID_STATE;
    uint32_t events;
    bool report_pending = false;
    bool boot_seen = false;

    for (;;)
    {
        //Storage 没有旧控制消费唤醒，待发期间定时检查队列空间。
        SysResult_t result = App_EventsWait(*service_owner,
            report_pending ? 100U : APP_EVENTS_WAIT_FOREVER, &events);
        if (result != SYS_OK && result != SYS_ERR_TIMEOUT)
        {
            configASSERT(result == SYS_OK);
            vTaskDelay(1U);
            continue;
        }

        //任何普通通知都不能触发重复初始化。
        if (!boot_seen && result == SYS_OK && (events & RUNTIME_EVENT_BOOT_READY) != 0U)
        {
            boot_seen = true;
            initialization = AppV3_InitService(*service_owner, AppV3_GetContext());
            report_pending = true;
        }

        //队列满时保留原结果。入队成功后只由 Control 消费。
        if (report_pending)
            report_pending = AppV3_ReportInitialization(*service_owner, initialization) != SYS_OK;
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

static SysResult_t AppV3_CreateTask(RuntimeEventTarget_t owner, const char *name,
    StackType_t *stack, uint32_t stack_words, UBaseType_t priority)
{
    const RuntimeEventTarget_t *service_owner = AppV3_FindService(owner);
    TaskHandle_t handle;
    bool control = owner == RUNTIME_TARGET_CONTROL;
    uint32_t slot = owner == RUNTIME_TARGET_STORAGE ? RUNTIME_TARGET_IPC : owner;

    //只在调度前创建，不重用已经尝试过的静态控制块。
    if (Platform_SyncGetException() != 0U || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;
    if (!control && service_owner == NULL)
        return SYS_ERR_INVALID_ARG;
    if (s_task_attempted[slot])
        return SYS_ERR_ALREADY_EXISTS;

    s_task_attempted[slot] = true;
    handle = xTaskCreateStatic(control ? AppV3_ControlRun : AppV3_ServiceRun,
        name, stack_words, (void *)service_owner, priority, stack, &s_task_controls[slot]);
    if (handle == NULL)
        return SYS_ERR_NO_RESOURCE;

    return App_EventsAttach(owner, handle);
}

//V3 Target 用这四个入口替换旧任务文件，保持原创建顺序。
SysResult_t CameraTask_Create(void)
{
    return AppV3_CreateTask(RUNTIME_TARGET_CAMERA, "Camera", s_camera_stack, TASK_WORDS(s_camera_stack), 3U);
}

SysResult_t UsbTask_Create(void)
{
    return AppV3_CreateTask(RUNTIME_TARGET_USB, "Usb", s_usb_stack, TASK_WORDS(s_usb_stack), 4U);
}

SysResult_t StorageTask_Create(void)
{
    return AppV3_CreateTask(RUNTIME_TARGET_STORAGE, "Storage", s_storage_stack, TASK_WORDS(s_storage_stack), 1U);
}

SysResult_t ControlTask_Create(void)
{
    return AppV3_CreateTask(RUNTIME_TARGET_CONTROL, "Control", s_control_stack, TASK_WORDS(s_control_stack), 2U);
}
