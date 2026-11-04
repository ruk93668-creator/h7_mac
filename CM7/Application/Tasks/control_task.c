#include "control_task.h"
#include "app_tasks.h"
#include "app_events.h"
#include <stddef.h>
#include "stm32h7xx.h"

#define CONTROL_TASK_STACK_WORDS 768U
#define CONTROL_TASK_PRIORITY    2U
#define CONTROL_TASK_WAIT_MS     1000U

static StaticTask_t s_control_tcb;
static StackType_t s_control_stack[CONTROL_TASK_STACK_WORDS] __attribute__((aligned(8)));
static TaskHandle_t s_control_task;
volatile ControlTaskDebug_t g_control_task_debug;

//main准备成功后发布屏障，之后阻塞等待控制事件
static void ControlTask_Run(void *argument);

SysResult_t ControlTask_Create(void)
{
    if (__get_IPSR() != 0U || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;
    if (s_control_task != NULL)
        return SYS_ERR_ALREADY_EXISTS;

    s_control_task = xTaskCreateStatic(ControlTask_Run, "Control", CONTROL_TASK_STACK_WORDS,
        NULL, CONTROL_TASK_PRIORITY, s_control_stack, &s_control_tcb);
    if (s_control_task == NULL)
        return SYS_ERR_NO_RESOURCE;

    return App_EventsAttach(RUNTIME_TARGET_CONTROL, s_control_task);
}

//内部功能函数实现
static void ControlTask_Run(void *argument)
{
    uint32_t events;
    SysResult_t result;
    (void)argument;

    result = App_TasksReleaseBoot();
    g_control_task_debug.result = result;
    g_control_task_debug.boot_seen = result == SYS_OK;

    for (;;)
    {
        result = App_EventsWait(RUNTIME_TARGET_CONTROL, CONTROL_TASK_WAIT_MS, &events);
        if (result == SYS_ERR_TIMEOUT)
            g_control_task_debug.timeout_count++;
        else if (result == SYS_OK)
        {
            g_control_task_debug.wake_count++;
            if ((events & RUNTIME_EVENT_FAULT) != 0U)
                g_control_task_debug.result = SYS_ERR_IO;
        }
        else
        {
            g_control_task_debug.result = result;
            configASSERT(result == SYS_OK);
        }

        g_control_task_debug.stack_free_words = uxTaskGetStackHighWaterMark(NULL);
    }
}
