#include "usb_task.h"
#include "app_events.h"
#include "usb_mode.h"
#include <stddef.h>
#include "stm32h7xx.h"

#define USB_TASK_STACK_WORDS 2048U
#define USB_TASK_PRIORITY    4U
#define USB_TASK_WAIT_MS     20U

//保存Usb任务的控制信息
static StaticTask_t s_usb_tcb;

//保存Usb任务的运行栈
static StackType_t s_usb_stack[USB_TASK_STACK_WORDS] __attribute__((aligned(8)));

//保存Usb任务的任务句柄
static TaskHandle_t s_usb_task;

//记录本任务是否已成功启动USB，避免重复Start
static bool s_usb_started;

//记录本轮故障是否已通知Control，避免重复发送提示
static bool s_usb_fault_reported;

//记录Usb任务的启动、唤醒和栈余量
volatile UsbTaskDebug_t g_usb_task_debug;

//等待USB事件并推进TEST控制面
static void UsbTask_Run(void *argument);

//处理启动屏障并推进USB模式状态
static void UsbTask_Process(uint32_t events);

//创建并登记唯一的静态Usb任务
SysResult_t UsbTask_Create(void)
{

    //只允许在调度器启动前的非中断上下文创建任务
    if (__get_IPSR() != 0U || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;

    //已存在任务时拒绝重复创建，避免复用同一份TCB和栈
    if (s_usb_task != NULL)
        return SYS_ERR_ALREADY_EXISTS;

    //使用本文件的静态TCB和栈创建任务，不申请内核堆
    s_usb_task = xTaskCreateStatic(UsbTask_Run, "Usb", USB_TASK_STACK_WORDS,
        NULL, USB_TASK_PRIORITY, s_usb_stack, &s_usb_tcb);

    //创建失败立即返回，不登记无效句柄
    if (s_usb_task == NULL)
        return SYS_ERR_NO_RESOURCE;

    //登记该任务的事件接收端，供启动屏障和通知定位
    return App_EventsAttach(RUNTIME_TARGET_USB, s_usb_task);
}

//等待USB事件并推进TEST控制面
static void UsbTask_Run(void *argument)
{
    uint32_t events;
    SysResult_t result;
    (void)argument;

    for (;;)
    {

        //BOOT前永久等待；BOOT后由事件唤醒或每20ms检查枚举超时
        result = App_EventsWait(RUNTIME_TARGET_USB,
            g_usb_task_debug.boot_seen != 0U ? USB_TASK_WAIT_MS : APP_EVENTS_WAIT_FOREVER, &events);

        //超时属于正常唤醒，其他等待失败属于调用环境异常
        if (result == SYS_ERR_TIMEOUT)
            g_usb_task_debug.timeout_count++;
        else if (result != SYS_OK)
        {
            g_usb_task_debug.result = result;
            configASSERT(result == SYS_OK);
            continue;
        }

        //在同一个任务中串行推进模式管理，不调用相机或Media
        UsbTask_Process(events);

        //记录历史最小剩余栈空间，供实板检查任务栈
        g_usb_task_debug.stack_free_words = uxTaskGetStackHighWaterMark(NULL);
    }
}

//处理启动屏障并推进USB模式状态
static void UsbTask_Process(uint32_t events)
{

    //第一次运行必须收到BOOT，不能被普通提示提前启动
    if (g_usb_task_debug.boot_seen == 0U)
    {
        if ((events & RUNTIME_EVENT_BOOT_READY) == 0U)
            return;

        g_usb_task_debug.boot_seen = 1U;

        //本阶段只允许TEST控制面，USB启动成功不代表主机已完成枚举
        g_usb_task_debug.result = USB_Mode_GetMode() == USB_MODE_TEST
            ? USB_Mode_Start() : SYS_ERR_NOT_SUPPORTED;
        s_usb_started = g_usb_task_debug.result == SYS_OK;
        if (!s_usb_started)
        {

            //启动失败只通知Control，不自动重试或提前释放资源
            (void)RuntimeEvent_Post(RUNTIME_TARGET_CONTROL, RUNTIME_EVENT_FAULT);
            s_usb_fault_reported = true;
        }
    }

    g_usb_task_debug.wake_count++;
    if (s_usb_started)
    {

        //USB启动后非阻塞检查配置状态、重连和设备级错误
        USB_Mode_Process();
        g_usb_task_debug.result = USB_Mode_GetLastResult();

        //运行故障只上报一次，保留实际模式错误供调试
        if (g_usb_task_debug.result != SYS_OK && !s_usb_fault_reported)
        {
            (void)RuntimeEvent_Post(RUNTIME_TARGET_CONTROL, RUNTIME_EVENT_FAULT);
            s_usb_fault_reported = true;
        }
    }
}
