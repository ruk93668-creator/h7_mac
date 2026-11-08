#include "camera_task.h"
#include "app_status.h"
#include "app_events.h"
#include "app_messages.h"
#include "app_camera.h"
#include "app_uvc_bridge.h"
#include "camera_capture.h"
#include "platform_time.h"
#include "stm32h7xx.h"
#include <stddef.h>

#define CAMERA_TASK_STACK_WORDS 1536U
#define CAMERA_TASK_PRIORITY 3U
#define CAMERA_TASK_WAIT_MS 20U

//栈余量统计周期，不作为任务唤醒周期
#define CAMERA_TASK_STACK_SAMPLE_MS 1000U

//保存Camera任务的静态控制信息
static StaticTask_t s_camera_tcb;

//保存Camera任务的运行栈
static StackType_t s_camera_stack[CAMERA_TASK_STACK_WORDS] __attribute__((aligned(8)));

//保存唯一Camera任务句柄
static TaskHandle_t s_camera_task;

//记录采集是否已成功启动
static bool s_camera_running;

//记录是否已有可停止的Media实例
static bool s_camera_prepared;

//记录停止要求，失败时也不能重新交付图像
static bool s_camera_stopping;

//保留待交付ACK，不因队列满丢失命令结果
static AppMessage_t s_camera_ack;

//记录ACK是否仍需入队
static bool s_camera_ack_pending;

//记录停止命令是否仍在等待资源归还
static bool s_camera_stop_command;

//记录故障是否已送达Control
static bool s_camera_fault_sent;

//保存最近一次实际采集故障
static SysResult_t s_camera_fault;

//保存FPS统计窗口的起点
static uint32_t s_camera_fps_tick;

//保存统计窗口起点的完整帧数
static uint32_t s_camera_fps_frames;

//记录任务状态和真实采集计数
volatile CameraTaskDebug_t g_camera_task_debug;

//等待事件并推进相机数据面
static void CameraTask_Run(void *argument);

//处理相机命令、Media进展和帧归还
static void CameraTask_Process(uint32_t events);

//提交本任务已完成处理的状态，不读写其他任务或SRAM4
static void CameraTask_PublishStatus(void);

//重试提交控制回复和故障提示
static void CameraTask_Reply(void);

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

//等待事件并推进相机数据面
static void CameraTask_Run(void *argument)
{
    uint32_t events;

    //独立记录栈扫描时刻，不改变任务等待和业务周期
    uint32_t stack_tick = Platform_TimeGetMs();
    uint32_t stack_now;
    SysResult_t result;
    (void)argument;

    for (;;)
    {

        //IRQ通知正常进度，短超时仅用于丢失提示后的检查和恢复重试
        result = App_EventsWait(RUNTIME_TARGET_CAMERA, g_camera_task_debug.boot_seen ?
            CAMERA_TASK_WAIT_MS : APP_EVENTS_WAIT_FOREVER, &events);
        if (result == SYS_ERR_TIMEOUT)
            g_camera_task_debug.timeout_count++;
        else if (result != SYS_OK)
        {
            g_camera_task_debug.result = result;
            configASSERT(result == SYS_OK);
            continue;
        }
        CameraTask_Process(events);
        CameraTask_PublishStatus();

        //栈余量按实际经过时间更新，不补做错过的扫描
        stack_now = Platform_TimeGetMs();
        if ((uint32_t)(stack_now - stack_tick) >= CAMERA_TASK_STACK_SAMPLE_MS)
        {
            stack_tick = stack_now;
            g_camera_task_debug.stack_free_words = uxTaskGetStackHighWaterMark(NULL);
        }
    }
}

//处理相机命令、Media进展和帧归还
static void CameraTask_Process(uint32_t events)
{

    AppMessage_t command;
    CameraCaptureStats_t stats;
    SysResult_t result;
    uint32_t now, elapsed, frames;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_CAMERA))
        return;
    if ((events & RUNTIME_EVENT_BOOT_READY) != 0U)
        g_camera_task_debug.boot_seen = 1U;
    if (!g_camera_task_debug.boot_seen)
        return;
    g_camera_task_debug.wake_count++;
    CameraTask_Reply();

    //有未交付结果时不能覆盖ACK；命令和帧通道互相独立
    if (!s_camera_ack_pending && !s_camera_stop_command &&
        App_MessagesReceive(APP_CHANNEL_CAMERA_COMMAND, &command))
    {
        s_camera_ack = command;
        s_camera_ack.type = APP_MESSAGE_ACK;
        s_camera_ack.source = RUNTIME_TARGET_CAMERA;
        s_camera_ack.result = SYS_ERR_INVALID_ARG;
        g_camera_task_debug.command_id = command.command_id;
        if (command.type == APP_MESSAGE_START)
        {
            if (s_camera_running || s_camera_stopping)
                result = SYS_ERR_BUSY;
            else
            {

                //先建立新代次，再配置并启动传感器和Media
                result = App_UvcBridgeCameraBegin();
                if (result == SYS_OK)
                    result = App_CameraInitStream();
                s_camera_prepared = CameraCapture_GetLastResult() != SYS_ERR_INVALID_STATE;
                if (result == SYS_OK)
                {
                    s_camera_running = true;
                    s_camera_fault = SYS_OK;
                    s_camera_fault_sent = false;
                    s_camera_fps_tick = Platform_TimeGetMs();
                    s_camera_fps_frames = 0U;
                }
                else if (s_camera_prepared)
                    s_camera_stopping = true;
            }
            if (result != SYS_OK && result != SYS_ERR_BUSY)
                s_camera_fault = result;
            s_camera_ack.result = result;
            s_camera_ack_pending = true;
            g_camera_task_debug.result = result;
        }
        else if (command.type == APP_MESSAGE_STOP)
        {

            //先停止新Offer，已有租借必须由USB完成Return握手
            s_camera_stopping = true;
            s_camera_stop_command = true;
        }
        else
            s_camera_ack_pending = true;
    }

    if (s_camera_prepared)
    {

        //只有CameraTask调用Media任务侧API，ISR仍使用原轮换回调
        App_CameraRun();
        result = CameraCapture_GetLastResult();
        if (result != SYS_OK && s_camera_running)
        {
            s_camera_fault = result;
            s_camera_running = false;
            s_camera_stopping = true;
            g_camera_task_debug.result = result;
        }
    }

    //归还始终可推进，停机或故障期间拒绝新的取帧请求
    App_UvcBridgeCameraProcess(s_camera_running && !s_camera_stopping);
    if (s_camera_stopping)
    {
        result = s_camera_prepared ? App_CameraStop() : SYS_OK;
        if (result == SYS_OK && !App_UvcBridgeCameraIsIdle())
            result = SYS_ERR_BUSY;
        if (result == SYS_OK)
        {
            s_camera_stopping = false;
            s_camera_running = false;
        }
        if (s_camera_stop_command && result != SYS_ERR_BUSY)
        {
            s_camera_ack.result = result;
            s_camera_ack_pending = true;
            s_camera_stop_command = false;
            g_camera_task_debug.result = result;
        }
        if (result != SYS_OK && result != SYS_ERR_BUSY && s_camera_fault == SYS_OK)
            s_camera_fault = result;
    }
    g_camera_task_debug.state = s_camera_stopping ? 2U : (s_camera_running ? 1U : 0U);

    if (s_camera_prepared && CameraCapture_GetStats(&stats) == SYS_OK)
    {
        g_camera_task_debug.captured_frames = stats.completed_frames;
        g_camera_task_debug.dropped_frames = stats.dropped_frames;
        now = Platform_TimeGetMs();
        elapsed = now - s_camera_fps_tick;
        if (elapsed >= 1000U)
        {
            frames = stats.completed_frames - s_camera_fps_frames;
            g_camera_task_debug.measured_fps_milli = frames <= 4294U ? frames * 1000000U / elapsed : 0U;
            s_camera_fps_tick = now;
            s_camera_fps_frames = stats.completed_frames;
        }
    }
    CameraTask_Reply();
}

//重试提交控制回复和故障提示
static void CameraTask_Reply(void)
{
    AppMessage_t message = {.type = APP_MESSAGE_FAULT};

    if (s_camera_ack_pending && App_MessagesSend(APP_CHANNEL_CONTROL, &s_camera_ack) == SYS_OK)
        s_camera_ack_pending = false;
    if (s_camera_fault != SYS_OK && !s_camera_fault_sent)
    {
        message.type = APP_MESSAGE_FAULT;
        message.source = RUNTIME_TARGET_CAMERA;
        message.result = s_camera_fault;
        if (App_MessagesSend(APP_CHANNEL_CONTROL, &message) == SYS_OK)
            s_camera_fault_sent = true;
    }
}

//提交Camera任务自己的统计和已成功启动过的配置
static void CameraTask_PublishStatus(void)
{
    static CameraConfig_t applied;
    static bool applied_valid;
    AppCameraStatus_t status = {0};
    const CameraConfig_t *config;

    //只有原启动路径成功后才把配置声明为实际应用
    if (s_camera_running)
    {
        config = App_CameraGetConfig();
        if (config != NULL)
        {
            applied = *config;
            applied_valid = true;
        }
    }
    status.state = g_camera_task_debug.state;
    status.fps_milli = g_camera_task_debug.measured_fps_milli;
    status.captured_frames = g_camera_task_debug.captured_frames;
    status.dropped_frames = g_camera_task_debug.dropped_frames;
    status.result = g_camera_task_debug.result;
    status.config_valid = applied_valid;
    status.width = applied.width;
    status.height = applied.height;
    status.format = applied.format;
    (void)App_StatusPublishCamera(&status);
}
