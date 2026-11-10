#include "usb_task.h"

#include "app_status.h"
#include "app_events.h"
#include "app_messages.h"
#include "app_usb.h"
#include "app_uvc_bridge.h"
#include "usb_mode.h"
#include "usb_device.h"
#include "usb_uvc.h"
#include "platform_time.h"
#include "platform_sync.h"
#include <stddef.h>

#define USB_TASK_STACK_WORDS 2048U
#define USB_TASK_PRIORITY 4U
#define USB_TASK_WAIT_MS 20U

//栈余量统计周期，不作为任务唤醒周期
#define USB_TASK_STACK_SAMPLE_MS 1000U

//保存Usb任务的静态控制信息
static StaticTask_t s_usb_tcb;

//保存Usb任务的运行栈
static StackType_t s_usb_stack[USB_TASK_STACK_WORDS] __attribute__((aligned(8)));

//保存唯一Usb任务句柄
static TaskHandle_t s_usb_task;

//记录任务状态和设备侧EOF速率
volatile UsbTaskDebug_t g_usb_task_debug;

//等待USB事件并推进数据面
static void UsbTask_Run(void *argument);

//处理USB命令、传输完成及帧归还
static void UsbTask_Process(uint32_t events);

//提交本任务已完成处理的状态，不读写其他任务或SRAM4
static void UsbTask_PublishStatus(void);

//重试交付ACK和故障通知
static void UsbTask_Reply(void);

//只投影业务指定的Debug字段，不修改USB业务
static void UsbTask_Observe(AppUsbDiagnosticField_t field,
    const AppUsbRuntimeStatus_t *status);

//创建并登记唯一静态Usb任务
SysResult_t UsbTask_Create(void)
{

    //只允许在调度器启动前的非中断上下文创建任务
    if (Platform_SyncGetException() != 0U || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
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

//等待USB事件并推进数据面
static void UsbTask_Run(void *argument)
{
    uint32_t events;

    //独立记录栈扫描时刻，不改变任务等待和业务周期
    uint32_t stack_tick = Platform_TimeGetMs();
    uint32_t stack_now;
    SysResult_t result;
    (void)argument;

    //观察者只更新诊断投影，BOOT和等待异常仍由Task维护
    App_USBSetDiagnosticObserver(UsbTask_Observe);

    for (;;)
    {

        //SOF和传输回调直接唤醒；20ms超时仅作枚举和恢复兜底
        result = App_EventsWait(RUNTIME_TARGET_USB, g_usb_task_debug.boot_seen ?
            USB_TASK_WAIT_MS : APP_EVENTS_WAIT_FOREVER, &events);

        if (result == SYS_ERR_TIMEOUT)
            g_usb_task_debug.timeout_count++;
        else if (result != SYS_OK)
        {
            g_usb_task_debug.result = result;
            configASSERT(result == SYS_OK);
            continue;
        }
        UsbTask_Process(events);
        UsbTask_PublishStatus();

        //栈扫描每秒最多一次，避免占用每包处理时隙
        stack_now = Platform_TimeGetMs();
        if ((uint32_t)(stack_now - stack_tick) >= USB_TASK_STACK_SAMPLE_MS)
        {
            stack_tick = stack_now;
            g_usb_task_debug.stack_free_words = uxTaskGetStackHighWaterMark(NULL);
        }

    }
}

//处理USB命令、传输完成及帧归还
static void UsbTask_Process(uint32_t events)
{
    AppMessage_t command;
    bool uvc;
    uint32_t sof, gap;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB))
        return;
    if ((events & RUNTIME_EVENT_BOOT_READY) != 0U)
        g_usb_task_debug.boot_seen = 1U;
    if (!g_usb_task_debug.boot_seen)
        return;
    g_usb_task_debug.wake_count++;

    //记录任务实际观察到的SOF间隔，不把配置FPS当作测量结果
    sof = USB_Device_GetSofSequence();
    gap = sof - g_usb_task_debug.sof_sequence;
    if (gap > 1U)
        g_usb_task_debug.coalesced_sof_slots += gap - 1U;
    if (gap > g_usb_task_debug.max_sof_gap)
        g_usb_task_debug.max_sof_gap = gap;
    g_usb_task_debug.sof_sequence = sof;
    UsbTask_Reply();
    uvc = USB_Mode_GetMode() == USB_MODE_UVC;

    //ACK和STOP未完成时不接收下一条，每轮仍只接收一条命令
    if (App_USBCanAcceptCommand() &&
        App_MessagesReceive(APP_CHANNEL_USB_COMMAND, &command))
        App_USBHandleCommand(&command);

    //这里只推进原模式流程，不把UVC或Bridge隐藏进业务Process
    App_USBProcess();
    if (uvc)
    {
        if (App_USBIsStopping())
            (void)USB_UVC_CancelFrame();

        //保持Cancel、Process、Bridge、再次Process的原顺序
        USB_UVC_Process();
        App_UvcBridgeUsbProcess(App_USBCanTransfer());

        if (App_USBIsStopping())
            USB_UVC_Process();
    }

    //归还处理后才判断停止完成，并按原时点更新EOF及FPS
    App_USBFinishRound(uvc);
    UsbTask_Reply();
}

//重试独立的ACK和FAULT，不把ACK失败当作跳过FAULT的条件
static void UsbTask_Reply(void)
{
    AppMessage_t message;

    //只有成功入队才能确认ACK，失败时保留原记录
    if (App_USBGetPendingAck(&message) &&
        App_MessagesSend(APP_CHANNEL_CONTROL, &message) == SYS_OK)
        App_USBConfirmAckEnqueued();

    //FAULT始终独立尝试，成功入队不代表Control已经处理
    if (App_USBGetPendingFault(&message) &&
        App_MessagesSend(APP_CHANNEL_CONTROL, &message) == SYS_OK)
        App_USBConfirmFaultEnqueued();
}

//提交原Debug口径的USB统计，保留App_Status发布位置
static void UsbTask_PublishStatus(void)
{
    AppUsbStatus_t status;

    status.state = g_usb_task_debug.state;
    status.fps_milli = g_usb_task_debug.measured_fps_milli;
    status.completed_frames = g_usb_task_debug.completed_frames;
    status.result = g_usb_task_debug.result;
    (void)App_StatusPublishUsb(&status);
}

//仅投影本次指定字段，禁止根据Debug反向推进USB业务
static void UsbTask_Observe(AppUsbDiagnosticField_t field,
    const AppUsbRuntimeStatus_t *status)
{
    switch (field)
    {
        case APP_USB_DIAG_COMMAND:
            g_usb_task_debug.command_id = status->command_id;
            break;
        case APP_USB_DIAG_RESULT:
            g_usb_task_debug.result = status->last_result;
            break;
        case APP_USB_DIAG_STATE:
            g_usb_task_debug.state = status->state;
            break;
        case APP_USB_DIAG_COMPLETED:
            g_usb_task_debug.completed_frames = status->completed_frames;
            break;
        case APP_USB_DIAG_FPS:
            g_usb_task_debug.measured_fps_milli = status->measured_fps_milli;
            break;
        default:
            break;
    }
}
