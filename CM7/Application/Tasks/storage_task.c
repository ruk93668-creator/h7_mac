#include "storage_task.h"
#include "app_storage.h"
#include "app_events.h"
#include "video_config.h"
#include "platform_time.h"
#include "stm32h7xx.h"
#include <stddef.h>

#define STORAGE_STACK_WORDS 2048U
#define STORAGE_PRIORITY 1U

//任务和栈都是静态资源，文件操作不借用USB或Camera任务栈
static StaticTask_t s_tcb;
static StackType_t s_stack[STORAGE_STACK_WORDS] __attribute__((aligned(8)));
static TaskHandle_t s_task;

//等待BOOT，再处理有界保存命令与副本
static void StorageTask_Run(void *argument);

//静态创建并登记BOOT屏障，失败不使用无效句柄
SysResult_t StorageTask_Create(void)
{
    if (__get_IPSR() != 0U || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED || s_task != NULL)
        return SYS_ERR_INVALID_STATE;
    s_task = xTaskCreateStatic(StorageTask_Run, "Storage", STORAGE_STACK_WORDS,
        NULL, STORAGE_PRIORITY, s_stack, &s_tcb);
    return s_task == NULL ? SYS_ERR_NO_RESOURCE : App_EventsAttach(RUNTIME_TARGET_STORAGE, s_task);
}

//文件系统由本任务唯一使用，空闲轮次不识卡或写入
static void StorageTask_Run(void *argument)
{
    uint32_t events, stack_tick = 0U, now, request;
    bool boot = false;
    SysResult_t result;
    (void)argument;
    for (;;)
    {
        result = App_EventsWait(RUNTIME_TARGET_STORAGE, boot ? 100U : APP_EVENTS_WAIT_FOREVER, &events);
        if (result != SYS_OK && result != SYS_ERR_TIMEOUT)
        {
            g_storage_debug.result = result;
            continue;
        }
        if (!boot && (events & RUNTIME_EVENT_BOOT_READY) != 0U)
        {
            boot = true;
            result = App_StoragePrepare(VideoConfig_GetStorage());
            g_storage_debug.result = result;
        }
        if (!boot || g_storage_debug.ready == 0U)
            continue;

        //这是人工业务控制入口，默认0，不恢复历史压力或验收任务
        request = g_storage_debug.request;
        if (request != 0U)
        {
            g_storage_debug.request = 0U;
            if (request == 1U) result = App_StorageStartRecording();
            else if (request == 2U) result = App_StorageRequestSnapshot();
            else if (request == 3U) result = App_StorageStop();
            else if (request == 4U) result = App_StorageRecover();
            else result = SYS_ERR_INVALID_ARG;
            g_storage_debug.result = result;
        }
        App_StorageProcess();

        //最多每1000ms扫描一次栈，避免历史高频扫描引入视频降帧
        now = Platform_TimeGetMs();
        if ((uint32_t)(now - stack_tick) >= 1000U)
        {
            stack_tick = now;
            g_storage_debug.stack_free_words = uxTaskGetStackHighWaterMark(NULL);
        }
    }
}
