#include "camera_task.h"
#include "app_events.h"
#include <stddef.h>
#include "stm32h7xx.h"

#define CAMERA_TASK_STACK_WORDS 1024U
#define CAMERA_TASK_PRIORITY    3U

static StaticTask_t s_camera_tcb;
static StackType_t s_camera_stack[CAMERA_TASK_STACK_WORDS] __attribute__((aligned(8)));
static TaskHandle_t s_camera_task;
volatile CameraTaskDebug_t g_camera_task_debug;

//阶段01只等待准备与事件，不启动相机或跨任务借帧
static void CameraTask_Run(void *argument);

SysResult_t CameraTask_Create(void)
{
    if (__get_IPSR() != 0U || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;
    if (s_camera_task != NULL)
        return SYS_ERR_ALREADY_EXISTS;

    s_camera_task = xTaskCreateStatic(CameraTask_Run, "Camera", CAMERA_TASK_STACK_WORDS,
        NULL, CAMERA_TASK_PRIORITY, s_camera_stack, &s_camera_tcb);
    if (s_camera_task == NULL)
        return SYS_ERR_NO_RESOURCE;

    return App_EventsAttach(RUNTIME_TARGET_CAMERA, s_camera_task);
}

//内部功能函数实现
static void CameraTask_Run(void *argument)
{
    uint32_t events;
    SysResult_t result;
    (void)argument;

    for (;;)
    {
        result = App_EventsWait(RUNTIME_TARGET_CAMERA, APP_EVENTS_WAIT_FOREVER, &events);
        g_camera_task_debug.result = result;
        if (result != SYS_OK)
        {
            configASSERT(result == SYS_OK);
            continue;
        }

        if ((events & RUNTIME_EVENT_BOOT_READY) != 0U)
            g_camera_task_debug.boot_seen = 1U;
        if (g_camera_task_debug.boot_seen == 0U)
            continue;

        g_camera_task_debug.wake_count++;
        g_camera_task_debug.stack_free_words = uxTaskGetStackHighWaterMark(NULL);
    }
}
