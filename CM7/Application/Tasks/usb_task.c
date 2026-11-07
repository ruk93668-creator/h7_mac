#include "usb_task.h"
#include "app_status.h"
#include "app_events.h"
#include "app_messages.h"
#include "app_uvc_bridge.h"
#include "usb_mode.h"
#include "usb_device.h"
#include "usb_uvc.h"
#include "platform_time.h"
#include "stm32h7xx.h"
#include <stddef.h>

#define USB_TASK_STACK_WORDS 2048U
#define USB_TASK_PRIORITY 4U
#define USB_TASK_WAIT_MS 20U

//保存Usb任务的静态控制信息
static StaticTask_t s_usb_tcb;

//保存Usb任务的运行栈
static StackType_t s_usb_stack[USB_TASK_STACK_WORDS] __attribute__((aligned(8)));

//保存唯一Usb任务句柄
static TaskHandle_t s_usb_task;

//记录本任务是否已启动USB
static bool s_usb_started;

//记录停止要求，失败时也禁止重新提交视频
static bool s_usb_stopping;

//记录停止命令是否仍在等待资源归还
static bool s_usb_stop_command;

//保留待交付的控制回复
static AppMessage_t s_usb_ack;

//记录ACK是否仍需入队
static bool s_usb_ack_pending;

//记录故障是否已通知Control
static bool s_usb_fault_reported;

//保存最近一次设备或停止故障
static SysResult_t s_usb_fault;

//保存EOF速率统计窗口
static uint32_t s_usb_fps_tick;

//保存窗口起点的累计EOF计数
static uint32_t s_usb_fps_frames;

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

//等待USB事件并推进数据面
static void UsbTask_Run(void *argument)
{
    uint32_t events;
    SysResult_t result;
    (void)argument;

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
        g_usb_task_debug.stack_free_words = uxTaskGetStackHighWaterMark(NULL);
    }
}

//处理USB命令、传输完成及帧归还
static void UsbTask_Process(uint32_t events)
{
    AppMessage_t command;
    UsbUvcStreamStatus_t status;
    SysResult_t result;
    bool uvc;
    uint32_t now, elapsed, frames, total, sof, gap;

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

    if (!s_usb_ack_pending && !s_usb_stop_command &&
        App_MessagesReceive(APP_CHANNEL_USB_COMMAND, &command))
    {
        s_usb_ack = command;
        s_usb_ack.type = APP_MESSAGE_ACK;
        s_usb_ack.source = RUNTIME_TARGET_USB;
        s_usb_ack.result = SYS_ERR_INVALID_ARG;
        g_usb_task_debug.command_id = command.command_id;
        if (command.type == APP_MESSAGE_START)
        {
            result = s_usb_started || s_usb_stopping ? SYS_ERR_BUSY : USB_Mode_Start();
            if (result == SYS_OK)
            {
                s_usb_started = true;
                s_usb_fault = SYS_OK;
                s_usb_fault_reported = false;
                s_usb_fps_tick = Platform_TimeGetMs();
                s_usb_fps_frames = USB_UVC_GetCompletedFrameCount();
            }
            if (result != SYS_OK)
                s_usb_fault = result;
            s_usb_ack.result = result;
            s_usb_ack_pending = true;
            g_usb_task_debug.result = result;
        }
        else if (command.type == APP_MESSAGE_STOP)
        {

            //先关提交入口，后续只推进取消、Return和ACK
            s_usb_stopping = true;
            s_usb_stop_command = true;
        }
        else
            s_usb_ack_pending = true;
    }

    if (s_usb_started && !s_usb_stopping)
    {
        USB_Mode_Process();
        result = USB_Mode_GetLastResult();
        if (result != SYS_OK)
        {
            s_usb_fault = result;
            s_usb_stopping = true;
            g_usb_task_debug.result = result;
        }
    }
    if (uvc)
    {
        if (s_usb_stopping)
            (void)USB_UVC_CancelFrame();

        //所有任务侧UVC操作都在UsbTask；ISR仍同步处理EP0和保存Token
        USB_UVC_Process();
        App_UvcBridgeUsbProcess(s_usb_started && !s_usb_stopping);
        if (s_usb_stopping)
            USB_UVC_Process();
    }
    if (s_usb_stopping)
    {
        result = SYS_ERR_BUSY;
        if (App_UvcBridgeUsbIsIdle())
            result = USB_Mode_Stop();
        else if (uvc && USB_UVC_GetStreamStatus(&status) == SYS_OK &&
            status.frame_owned && status.state == USB_UVC_RECOVERY && status.result != SYS_OK)
            result = status.result;

        //Abort失败时仍保留原帧和停止要求，只返回失败ACK，不伪造已停
        if (result == SYS_OK)
        {
            s_usb_started = false;
            s_usb_stopping = false;
        }
        if (s_usb_stop_command && result != SYS_ERR_BUSY)
        {
            s_usb_ack.result = result;
            s_usb_ack_pending = true;
            s_usb_stop_command = false;
            g_usb_task_debug.result = result;
        }
        if (result != SYS_OK && result != SYS_ERR_BUSY && s_usb_fault == SYS_OK)
            s_usb_fault = result;
    }
    g_usb_task_debug.state = s_usb_stopping ? 2U : (s_usb_started ? 1U : 0U);
    total = USB_UVC_GetCompletedFrameCount();
    g_usb_task_debug.completed_frames = total;
    now = Platform_TimeGetMs();
    elapsed = now - s_usb_fps_tick;
    if (elapsed >= 1000U)
    {
        frames = total - s_usb_fps_frames;
        g_usb_task_debug.measured_fps_milli = frames <= 4294U ? frames * 1000000U / elapsed : 0U;
        s_usb_fps_tick = now;
        s_usb_fps_frames = total;
    }
    UsbTask_Reply();
}

//重试交付ACK和故障通知
static void UsbTask_Reply(void)
{
    AppMessage_t message = {.type = APP_MESSAGE_FAULT};

    if (s_usb_ack_pending && App_MessagesSend(APP_CHANNEL_CONTROL, &s_usb_ack) == SYS_OK)
        s_usb_ack_pending = false;
    if (s_usb_fault != SYS_OK && !s_usb_fault_reported)
    {
        message.type = APP_MESSAGE_FAULT;
        message.source = RUNTIME_TARGET_USB;
        message.result = s_usb_fault;
        if (App_MessagesSend(APP_CHANNEL_CONTROL, &message) == SYS_OK)
            s_usb_fault_reported = true;
    }
}

//提交USB实际完成统计，端点和UVC生命周期保持原样
static void UsbTask_PublishStatus(void)
{
    AppUsbStatus_t status;

    status.state = g_usb_task_debug.state;
    status.fps_milli = g_usb_task_debug.measured_fps_milli;
    status.completed_frames = g_usb_task_debug.completed_frames;
    status.result = g_usb_task_debug.result;
    (void)App_StatusPublishUsb(&status);
}
