#include "control_task.h"
#include "app_tasks.h"
#include "app_events.h"
#include <stddef.h>
#include "stm32h7xx.h"

#define CONTROL_TASK_STACK_WORDS 768U
#define CONTROL_TASK_PRIORITY    2U
#define CONTROL_TASK_WAIT_MS     1000U

//保存Control任务的控制信息
static StaticTask_t s_control_tcb;

//保存Control任务的运行栈
static StackType_t s_control_stack[CONTROL_TASK_STACK_WORDS] __attribute__((aligned(8)));

//保存Control任务的任务句柄
static TaskHandle_t s_control_task;

//记录Control任务的启动、唤醒和栈余量
volatile ControlTaskDebug_t g_control_task_debug;

//发布启动屏障并接收故障提示
static void ControlTask_Run(void *argument);

//创建并登记唯一的静态Control任务
SysResult_t ControlTask_Create(void)
{

    //只允许在调度器启动前的非中断上下文创建任务
    if (__get_IPSR() != 0U || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;

    //已存在任务时拒绝重复创建，避免复用同一份TCB和栈
    if (s_control_task != NULL)
        return SYS_ERR_ALREADY_EXISTS;

    //使用本文件的静态TCB和栈创建任务，不申请内核堆
    s_control_task = xTaskCreateStatic(ControlTask_Run, "Control", CONTROL_TASK_STACK_WORDS,
        NULL, CONTROL_TASK_PRIORITY, s_control_stack, &s_control_tcb);

    //创建失败立即返回，不登记无效句柄
    if (s_control_task == NULL)
        return SYS_ERR_NO_RESOURCE;

    //登记该任务的事件接收端，供启动屏障和通知定位
    return App_EventsAttach(RUNTIME_TARGET_CONTROL, s_control_task);
}

//发布启动屏障并接收故障提示
static void ControlTask_Run(void *argument)
{
    uint32_t events;
    SysResult_t result;
    (void)argument;

    //main已经记录初始化结果，由Control统一放行其他任务
    result = App_TasksReleaseBoot();
    g_control_task_debug.result = result;
    g_control_task_debug.boot_seen = result == SYS_OK;

    for (;;)
    {

        //阻塞等待故障提示，最长1000ms唤醒一次记录运行情况
        result = App_EventsWait(RUNTIME_TARGET_CONTROL, CONTROL_TASK_WAIT_MS, &events);

        //区分正常超时、收到提示和调用环境异常
        if (result == SYS_ERR_TIMEOUT)
            g_control_task_debug.timeout_count++;
        else if (result == SYS_OK)
        {
            g_control_task_debug.wake_count++;

            //这里只记录存在故障，具体原因保留在故障来源模块
            if ((events & RUNTIME_EVENT_FAULT) != 0U)
                g_control_task_debug.result = SYS_ERR_IO;
        }
        else
        {
            g_control_task_debug.result = result;
            configASSERT(result == SYS_OK);
        }

        //记录栈余量，不在Control中操作相机或USB资源
        g_control_task_debug.stack_free_words = uxTaskGetStackHighWaterMark(NULL);
    }
}
