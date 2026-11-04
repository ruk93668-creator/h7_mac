#include "app_tasks.h"
#include "app_events.h"
#include "Tasks/camera_task.h"
#include "Tasks/usb_task.h"
#include "Tasks/control_task.h"
#include "stm32h7xx.h"

volatile AppTasksDebug_t g_app_tasks_debug;
static bool s_create_attempted;

SysResult_t App_TasksCreate(void)
{
    SysResult_t result;

    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED || s_create_attempted)
        return SYS_ERR_INVALID_STATE;

    //部分创建失败也禁止重试，避免重复TCB及任务实例
    s_create_attempted = true;
    result = App_EventsInit();
    if (result == SYS_OK)
    {
        result = CameraTask_Create();
        if (result == SYS_OK)
            g_app_tasks_debug.created_tasks++;
    }
    if (result == SYS_OK)
    {
        result = UsbTask_Create();
        if (result == SYS_OK)
            g_app_tasks_debug.created_tasks++;
    }
    if (result == SYS_OK)
    {
        result = ControlTask_Create();
        if (result == SYS_OK)
            g_app_tasks_debug.created_tasks++;
    }

    g_app_tasks_debug.result = result;
    return result;
}

SysResult_t App_TasksRecordInitialization(SysResult_t result)
{
    if (g_app_tasks_debug.created_tasks != 3U || g_app_tasks_debug.prepared != 0U)
        return SYS_ERR_INVALID_STATE;

    result = App_EventsRecordInitialization(result);
    g_app_tasks_debug.result = result;
    if (result == SYS_OK)
        g_app_tasks_debug.prepared = 1U;
    return result;
}

SysResult_t App_TasksReleaseBoot(void)
{
    SysResult_t result = App_EventsReleaseBoot();

    if (result == SYS_OK)
        g_app_tasks_debug.boot_released = 1U;
    else
        g_app_tasks_debug.result = result;
    return result;
}
