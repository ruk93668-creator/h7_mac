#include "app_tasks.h"
#include "app_events.h"
#include "Tasks/camera_task.h"
#include "Tasks/usb_task.h"
#include "Tasks/control_task.h"
#include "stm32h7xx.h"

//记录任务创建数量、准备状态和启动屏障结果
volatile AppTasksDebug_t g_app_tasks_debug;

//锁定本次创建尝试，部分失败后也不能复用静态任务资源
static bool s_create_attempted;

//统一创建三个业务任务和事件适配
SysResult_t App_TasksCreate(void)
{
    SysResult_t result;

    //只允许main在调度器启动前、全局中断未关闭时创建一次
    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED || s_create_attempted)
        return SYS_ERR_INVALID_STATE;

    //部分创建失败也禁止重试，避免重复TCB及任务实例
    s_create_attempted = true;

    //先准备普通C事件后端，再创建接收通知的任务
    result = App_EventsInit();
    if (result == SYS_OK)
    {

        //按固定顺序创建Camera任务，成功后更新数量
        result = CameraTask_Create();
        if (result == SYS_OK)
            g_app_tasks_debug.created_tasks++;
    }
    if (result == SYS_OK)
    {

        //只有前一步成功才创建Usb任务，失败立即停止后续步骤
        result = UsbTask_Create();
        if (result == SYS_OK)
            g_app_tasks_debug.created_tasks++;
    }
    if (result == SYS_OK)
    {

        //最后创建Control任务，调度器运行后由它发布BOOT
        result = ControlTask_Create();
        if (result == SYS_OK)
            g_app_tasks_debug.created_tasks++;
    }

    //保留首个失败结果；main据此决定是否允许启动调度器
    g_app_tasks_debug.result = result;
    return result;
}

//记录Framework初始化结果并准备启动屏障
SysResult_t App_TasksRecordInitialization(SysResult_t result)
{

    //三个任务必须全部就绪，初始化结果不能重复提交
    if (g_app_tasks_debug.created_tasks != 3U || g_app_tasks_debug.prepared != 0U)
        return SYS_ERR_INVALID_STATE;

    //把main中的初始化结果交给屏障层，失败时保持关闭
    result = App_EventsRecordInitialization(result);
    g_app_tasks_debug.result = result;

    //只有成功记录准备结果，才能标记业务启动条件成立
    if (result == SYS_OK)
        g_app_tasks_debug.prepared = 1U;
    return result;
}

//发布业务任务启动屏障
SysResult_t App_TasksReleaseBoot(void)
{

    //由事件层校验当前调用者和准备状态，再广播BOOT
    SysResult_t result = App_EventsReleaseBoot();

    //发布成功才记录已放行，否则保存真实失败原因
    if (result == SYS_OK)
        g_app_tasks_debug.boot_released = 1U;
    else
        g_app_tasks_debug.result = result;
    return result;
}
