#include "camera_task.h"
#include "app_status.h"
#include "app_events.h"
#include "app_messages.h"
#include "app_camera.h"
#include "app_uvc_bridge.h"
#include "platform_time.h"
#include "platform_sync.h"
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

//在原业务更新位置投影Debug，不保存第二份业务状态
static void CameraTask_Observe(AppCameraDiagnosticField_t field,
    const AppCameraRuntimeStatus_t *status);

//创建并登记唯一静态Camera任务
SysResult_t CameraTask_Create(void)
{

    //只允许在调度器启动前的非中断上下文创建任务
    if (Platform_SyncGetException() != 0U || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
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

    //观察者只复制诊断字段，BOOT和等待错误仍归任务维护
    App_CameraSetDiagnosticObserver(CameraTask_Observe);

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

    if (!App_MessagesIsOwner(RUNTIME_TARGET_CAMERA))
        return;
    if ((events & RUNTIME_EVENT_BOOT_READY) != 0U)
        g_camera_task_debug.boot_seen = 1U;
    if (!g_camera_task_debug.boot_seen)
        return;
    //在START回退可能发生前建立停止记账边界
    App_CameraBeginRound();
    g_camera_task_debug.wake_count++;
    CameraTask_Reply();

    //ACK或STOP未完成时不接收新命令，每轮仍最多接收一条
    if (App_CameraCanAcceptCommand() &&
        App_MessagesReceive(APP_CHANNEL_CAMERA_COMMAND, &command))
        App_CameraHandleCommand(&command);

    //保持Media、Bridge归还和停止完成的原执行顺序
    App_CameraProcess();
    App_UvcBridgeCameraProcess(App_CameraCanOfferFrame());
    App_CameraFinishRound();
    CameraTask_Reply();
}

//重试独立的ACK和FAULT，ACK队列满也继续尝试FAULT
static void CameraTask_Reply(void)
{
    AppMessage_t message;

    //只有成功入队才能确认ACK，失败时由业务上下文保留
    if (App_CameraGetPendingAck(&message) &&
        App_MessagesSend(APP_CHANNEL_CONTROL, &message) == SYS_OK)
        App_CameraConfirmAckEnqueued();

    //FAULT独立交付，不因ACK失败而跳过
    if (App_CameraGetPendingFault(&message) &&
        App_MessagesSend(APP_CHANNEL_CONTROL, &message) == SYS_OK)
        App_CameraConfirmFaultEnqueued();
}

//提交原诊断口径和最近运行配置，保持状态发布位置
static void CameraTask_PublishStatus(void)
{
    AppCameraRuntimeStatus_t camera;
    AppCameraStatus_t status = {0};

    //显式采样与只读Getter分开，保留原配置记录时点
    App_CameraSampleStatus();
    App_CameraGetStatus(&camera);

    //业务诊断按原时点更新，result保留任务等待错误的原语义
    status.state = g_camera_task_debug.state;
    status.fps_milli = g_camera_task_debug.measured_fps_milli;
    status.captured_frames = g_camera_task_debug.captured_frames;
    status.dropped_frames = g_camera_task_debug.dropped_frames;
    status.result = g_camera_task_debug.result;
    status.config_valid = camera.applied_valid;
    status.width = camera.applied.width;
    status.height = camera.applied.height;
    status.format = camera.applied.format;
    (void)App_StatusPublishCamera(&status);
}

//只更新指定Debug字段，不根据Debug反向改变Camera业务
static void CameraTask_Observe(AppCameraDiagnosticField_t field,
    const AppCameraRuntimeStatus_t *status)
{
    switch (field)
    {
        case APP_CAMERA_DIAG_COMMAND:
            g_camera_task_debug.command_id = status->command_id;
            break;
        case APP_CAMERA_DIAG_RESULT:
            g_camera_task_debug.result = status->last_result;
            break;
        case APP_CAMERA_DIAG_STATE:
            g_camera_task_debug.state = status->state;
            break;
        case APP_CAMERA_DIAG_COUNTERS:
            g_camera_task_debug.captured_frames = status->captured_frames;
            g_camera_task_debug.dropped_frames = status->dropped_frames;
            break;
        case APP_CAMERA_DIAG_FPS:
            g_camera_task_debug.measured_fps_milli = status->measured_fps_milli;
            break;
        default:
            break;
    }
}
