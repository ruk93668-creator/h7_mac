#include "storage_task.h"
#include "../../ApplicationV2/Core/Runtime/v2_rtos_runtime.h"
#include "app_storage.h"
#include "app_events.h"
#include "video_config.h"
#include "platform_time.h"
#include "platform_sync.h"
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
    if (Platform_SyncGetException() != 0U || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED || s_task != NULL)
        return SYS_ERR_INVALID_STATE;
    s_task = xTaskCreateStatic(StorageTask_Run, "Storage", STORAGE_STACK_WORDS,
        NULL, STORAGE_PRIORITY, s_stack, &s_tcb);
    return s_task == NULL ? SYS_ERR_NO_RESOURCE : App_EventsAttach(RUNTIME_TARGET_STORAGE, s_task);
}

//文件系统由本任务唯一使用，空闲轮次不识卡或写入
static void StorageTask_Run(void *argument)
{
    uint32_t events, stack_tick = 0U, now;
    bool boot = false;
    bool storage_ready;
    SysResult_t result;
    (void)argument;
    for (;;)
    {
        result = App_EventsWait(RUNTIME_TARGET_STORAGE, boot ? 100U : APP_EVENTS_WAIT_FOREVER, &events);
        if (result != SYS_OK && result != SYS_ERR_TIMEOUT)
        {
            g_storage_status.result = result;
            continue;
        }
        if (!boot && (events & RUNTIME_EVENT_BOOT_READY) != 0U)
        {
            boot = true;
            result = App_StoragePrepare(VideoConfig_GetStorage());
            g_storage_status.result = result;
        }
        storage_ready = boot && App_StorageIsReady();
        if (storage_ready)
            App_StorageProcess();
        V2Runtime_Round(RUNTIME_TARGET_STORAGE, events, Platform_TimeGetMs());
        if (!storage_ready)
            continue;

        //最多每1000ms扫描一次栈，避免历史高频扫描引入视频降帧
        now = Platform_TimeGetMs();
        if ((uint32_t)(now - stack_tick) >= 1000U)
        {
            stack_tick = now;
            g_storage_status.stack_free_words = uxTaskGetStackHighWaterMark(NULL);
        }
    }
}
