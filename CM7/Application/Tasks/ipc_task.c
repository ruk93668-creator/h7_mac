#include "ipc_task.h"
#include "app_status.h"
#include "app_events.h"
#include "ipc_service.h"
#include "ipc_config.h"
#include "platform_time.h"
#include "stm32h7xx.h"
#include <stddef.h>

#define IPC_TASK_STACK_WORDS 512U
#define IPC_TASK_PRIORITY 1U

//保存唯一IPC任务的静态控制块
static StaticTask_t s_ipc_tcb;

//保存IPC任务栈，单位为32位Word
static StackType_t s_ipc_stack[IPC_TASK_STACK_WORDS] __attribute__((aligned(8)));

//保存唯一运行期发布者的任务身份
static TaskHandle_t s_ipc_task;

//提供Keil观察的任务诊断
volatile IpcTaskDebug_t g_ipc_task_debug;

//等待BOOT后周期推进IPC，不处理其他业务
static void IpcTask_Run(void *argument);

//验证当前执行者就是登记的IPC Task
static bool IpcTask_IsOwner(void *context);

//创建IPC静态任务并绑定运行期发布身份
SysResult_t IpcTask_Create(void)
{
    SysResult_t result;

    //只允许调度器启动前在非中断上下文创建静态任务
    if (__get_IPSR() != 0U || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;

    //拒绝重复创建，避免复用同一份TCB和栈
    if (s_ipc_task != NULL)
        return SYS_ERR_ALREADY_EXISTS;

    //静态创建，不使用内核堆；失败仍由统一任务创建路径处理
    s_ipc_task = xTaskCreateStatic(IpcTask_Run, "IPC", IPC_TASK_STACK_WORDS,
        NULL, IPC_TASK_PRIORITY, s_ipc_stack, &s_ipc_tcb);
    if (s_ipc_task == NULL)
        return SYS_ERR_NO_RESOURCE;

    //登记BOOT事件接收端，任务创建后仍需等待统一放行
    result = App_EventsAttach(RUNTIME_TARGET_IPC, s_ipc_task);
    if (result != SYS_OK)
        return result;

    //将唯一任务身份交给Service校验，不公开共享写权限
    return IPC_Service_SetRuntimeOwner(IpcTask_IsOwner, NULL);
}

//接收BOOT屏障并以阻塞方式等待心跳周期
static void IpcTask_Run(void *argument)
{
    uint32_t events;
    uint32_t application_tick = 0U;
    IpcApplicationStatus_t application = {0};
    SysResult_t result;
    (void)argument;

    //未放行时不启动心跳，不以Ready代替BOOT屏障
    for (;;)
    {
        result = App_EventsWait(RUNTIME_TARGET_IPC, APP_EVENTS_WAIT_FOREVER, &events);
        if (result == SYS_OK && (events & RUNTIME_EVENT_BOOT_READY) != 0U)
            break;
        configASSERT(result == SYS_OK);
    }

    //确认启动屏障已通过，再建立本核运行期计时基线
    g_ipc_task_debug.boot_seen = 1U;
    result = IPC_Service_RuntimeStart(Platform_TimeGetMs());
    g_ipc_task_debug.result = result;
    configASSERT(result == SYS_OK);
    for (;;)
    {

        //先取得一致本地汇总，再由原授权路径连同心跳一起发布
        g_ipc_task_debug.snapshot_result = App_StatusGetSnapshotAt(&application, &application_tick);

        //只有汇总成功才替换待发布载荷，失败时保留原结果供诊断
        if (g_ipc_task_debug.snapshot_result == SYS_OK)
            g_ipc_task_debug.snapshot_result = IPC_Service_SetApplication(&application, application_tick);

        //通过授权路径发布心跳与快照，同时读取CM4运行状态
        g_ipc_task_debug.result = IPC_Service_RuntimeProcess(Platform_TimeGetMs());

        //记录任务运行次数和历史栈余量，供实板检查调度与资源
        g_ipc_task_debug.wake_count++;
        g_ipc_task_debug.stack_free_words = uxTaskGetStackHighWaterMark(NULL);

        //让出CPU，调度延迟只记一次真实心跳，不进行突发补偿
        vTaskDelay(pdMS_TO_TICKS(IPC_HEARTBEAT_PERIOD_MS));
    }
}

//通过Application适配层比较当前任务，不向Service泄露RTOS类型
static bool IpcTask_IsOwner(void *context)
{
    (void)context;
    return App_EventsIsOwner(RUNTIME_TARGET_IPC);
}
