#include "camera_task.h"
#include "app_events.h"
#include <stddef.h>
#include "stm32h7xx.h"

#define CAMERA_TASK_STACK_WORDS 1024U
#define CAMERA_TASK_PRIORITY    3U

//保存Camera任务的控制信息
static StaticTask_t s_camera_tcb;

//保存Camera任务的运行栈
static StackType_t s_camera_stack[CAMERA_TASK_STACK_WORDS] __attribute__((aligned(8)));

//保存Camera任务的任务句柄
static TaskHandle_t s_camera_task;

//记录Camera任务的启动、唤醒和栈余量
volatile CameraTaskDebug_t g_camera_task_debug;

//等待启动屏障和相机事件
static void CameraTask_Run(void *argument);

//创建并登记唯一的静态Camera任务
SysResult_t CameraTask_Create(void)
{

    //只允许在调度器启动前的非中断上下文创建任务
    if (__get_IPSR() != 0U || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;

    //已存在任务时拒绝重复创建，避免复用同一份TCB和栈
    if (s_camera_task != NULL)
        return SYS_ERR_ALREADY_EXISTS;

    //使用本文件的静态TCB和栈创建任务，不申请内核堆
    s_camera_task = xTaskCreateStatic(CameraTask_Run, "Camera", CAMERA_TASK_STACK_WORDS,
        NULL, CAMERA_TASK_PRIORITY, s_camera_stack, &s_camera_tcb);

    //创建失败立即返回，不登记无效句柄
    if (s_camera_task == NULL)
        return SYS_ERR_NO_RESOURCE;

    //登记该任务的事件接收端，供启动屏障和通知定位
    return App_EventsAttach(RUNTIME_TARGET_CAMERA, s_camera_task);
}

//等待启动屏障和相机事件
static void CameraTask_Run(void *argument)
{
    uint32_t events;
    SysResult_t result;
    (void)argument;

    for (;;)
    {

        //阻塞等待自己的事件，没有事件时不占用CPU
        result = App_EventsWait(RUNTIME_TARGET_CAMERA, APP_EVENTS_WAIT_FOREVER, &events);
        g_camera_task_debug.result = result;

        //等待环境异常时记录并断言，避免错误条件下持续空转
        if (result != SYS_OK)
        {
            configASSERT(result == SYS_OK);
            continue;
        }

        //收到屏障后才允许处理运行事件；本阶段尚不启动采集
        if ((events & RUNTIME_EVENT_BOOT_READY) != 0U)
            g_camera_task_debug.boot_seen = 1U;
        if (g_camera_task_debug.boot_seen == 0U)
            continue;

        //记录本轮处理情况和历史最小剩余栈空间
        g_camera_task_debug.wake_count++;
        g_camera_task_debug.stack_free_words = uxTaskGetStackHighWaterMark(NULL);
    }
}
