#include "control_task.h"
#include "app_tasks.h"
#include "app_events.h"
#include "app_messages.h"
#include "video_config.h"
#include "platform_time.h"
#include "stm32h7xx.h"
#include <stddef.h>

#define CONTROL_TASK_STACK_WORDS 768U
#define CONTROL_TASK_PRIORITY 2U
#define CONTROL_COMMAND_TIMEOUT_MS 5000U

typedef enum
{
    CONTROL_IDLE = 0,
    CONTROL_START_CAMERA,
    CONTROL_START_USB,
    CONTROL_RUNNING,
    CONTROL_STOP_USB,
    CONTROL_STOP_CAMERA,
    CONTROL_ERROR
} ControlState_t;

//保存Control任务的静态控制信息
static StaticTask_t s_control_tcb;

//保存Control任务的运行栈
static StackType_t s_control_stack[CONTROL_TASK_STACK_WORDS] __attribute__((aligned(8)));

//保存唯一Control任务句柄
static TaskHandle_t s_control_task;

//保存协调流程状态，不直接持有Media或USB对象
static ControlState_t s_control_state;

//保存待交付或等待ACK的唯一命令
static AppMessage_t s_control_command;

//记录命令是否已经提交到目标队列
static bool s_control_command_sent;

//保存命令编号，达到上限后禁止重用
static uint32_t s_control_sequence;

//保存本次命令的超时起点
static uint32_t s_control_command_tick;

//记录是否已经处理冷启动请求
static bool s_control_boot_started;

//提供启动、停止与命令结果调试信息
volatile ControlTaskDebug_t g_control_task_debug;

//发布屏障并等待命令与ACK
static void ControlTask_Run(void *argument);

//推进启动停止协调流程
static void ControlTask_Process(uint32_t events);

//建立下一条协调命令
static void ControlTask_Command(ControlState_t state);

//登记外部协调请求
static SysResult_t ControlTask_Request(AppMessageType_t type);

SysResult_t ControlTask_Create(void)
{

    //只允许在调度器启动前的非中断上下文创建任务
    if (__get_IPSR() != 0U || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;

    //已存在任务时拒绝重复创建，避免复用同一份TCB和栈
    if (s_control_task != NULL)
        return SYS_ERR_ALREADY_EXISTS;

    //使用本文件的静态TCB和栈创建任务，不申请内核堆
    s_control_task = xTaskCreateStatic(ControlTask_Run, "Control", CONTROL_TASK_STACK_WORDS,
        NULL, CONTROL_TASK_PRIORITY, s_control_stack, &s_control_tcb);

    //创建失败立即返回，不登记无效句柄
    if (s_control_task == NULL)
        return SYS_ERR_NO_RESOURCE;

    //登记该任务的事件接收端，供启动屏障和通知定位
    return App_EventsAttach(RUNTIME_TARGET_CONTROL, s_control_task);
}

//发布屏障并等待命令与ACK
static void ControlTask_Run(void *argument)
{
    uint32_t events;
    SysResult_t result;
    (void)argument;

    //main已经执行并记录Initcall结果，Control只发布屏障
    result = App_TasksReleaseBoot();
    g_control_task_debug.result = result;
    configASSERT(result == SYS_OK);
    ControlTask_Process(RUNTIME_EVENT_BOOT_READY);
    for (;;)
    {

        //命令和ACK直接唤醒，100ms超时仅用于未回复命令的超时检查
        result = App_EventsWait(RUNTIME_TARGET_CONTROL, 100U, &events);
        if (result == SYS_ERR_TIMEOUT)
            g_control_task_debug.timeout_count++;
        else if (result != SYS_OK)
        {
            g_control_task_debug.result = result;
            configASSERT(result == SYS_OK);
            continue;
        }
        ControlTask_Process(events);
        g_control_task_debug.stack_free_words = uxTaskGetStackHighWaterMark(NULL);
    }
}

//推进启动停止协调流程
static void ControlTask_Process(uint32_t events)
{
    AppMessage_t message;
    uint32_t i;
    bool active;
    bool uvc = VideoConfig_GetDefault()->output == VIDEO_OUTPUT_USB_UVC;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_CONTROL))
        return;
    if ((events & RUNTIME_EVENT_BOOT_READY) != 0U)
        g_control_task_debug.boot_seen = 1U;
    if (!g_control_task_debug.boot_seen)
        return;
    g_control_task_debug.wake_count++;
    if (!s_control_boot_started)
    {
        s_control_boot_started = true;
        ControlTask_Command(uvc ? CONTROL_START_CAMERA : CONTROL_START_USB);
    }

    for (i = 0U; i < 4U && App_MessagesReceive(APP_CHANNEL_CONTROL, &message); i++)
    {
        if (message.type == APP_MESSAGE_FAULT)
        {

            //故障停止也先要求USB归还，Camera ISR错误路径仍可先停危险DMA
            g_control_task_debug.result = message.result;
            g_control_task_debug.last_fault = message.result;
            if (s_control_state != CONTROL_STOP_USB && s_control_state != CONTROL_STOP_CAMERA)
                ControlTask_Command(CONTROL_STOP_USB);
        }
        else if (message.type == APP_MESSAGE_STOP)
        {
            if (s_control_state != CONTROL_STOP_USB && s_control_state != CONTROL_STOP_CAMERA)
                ControlTask_Command(CONTROL_STOP_USB);
        }
        else if (message.type == APP_MESSAGE_START)
        {

            //ERROR必须先完成一次协调Stop，不能直接重启未知资源状态
            if (s_control_state == CONTROL_IDLE)
                ControlTask_Command(uvc ? CONTROL_START_CAMERA : CONTROL_START_USB);
            else
                g_control_task_debug.result = SYS_ERR_BUSY;
        }
        else if (message.type == APP_MESSAGE_ACK && s_control_command_sent &&
            message.command_id == s_control_command.command_id &&
            message.source == ((s_control_state == CONTROL_START_CAMERA ||
                s_control_state == CONTROL_STOP_CAMERA) ? RUNTIME_TARGET_CAMERA : RUNTIME_TARGET_USB))
        {
            s_control_command_sent = false;
            g_control_task_debug.result = message.result;
            if (message.result != SYS_OK)
                s_control_state = CONTROL_ERROR;
            else if (s_control_state == CONTROL_START_CAMERA)
                ControlTask_Command(CONTROL_START_USB);
            else if (s_control_state == CONTROL_START_USB)
                s_control_state = CONTROL_RUNNING;
            else if (s_control_state == CONTROL_STOP_USB)
            {
                if (uvc)
                    ControlTask_Command(CONTROL_STOP_CAMERA);
                else
                    s_control_state = CONTROL_IDLE;
            }
            else if (s_control_state == CONTROL_STOP_CAMERA)
                s_control_state = CONTROL_IDLE;
        }
    }
    active = s_control_state == CONTROL_START_CAMERA || s_control_state == CONTROL_START_USB ||
        s_control_state == CONTROL_STOP_USB || s_control_state == CONTROL_STOP_CAMERA;
    if (active)
    {

        //消息入队失败时保留同一编号，不能丢命令或生成第二次启停
        if (!s_control_command_sent)
        {
            AppChannel_t channel = (s_control_state == CONTROL_START_CAMERA ||
                s_control_state == CONTROL_STOP_CAMERA) ? APP_CHANNEL_CAMERA_COMMAND : APP_CHANNEL_USB_COMMAND;
            if (App_MessagesSend(channel, &s_control_command) == SYS_OK)
                s_control_command_sent = true;
        }
        if ((uint32_t)(Platform_TimeGetMs() - s_control_command_tick) >= CONTROL_COMMAND_TIMEOUT_MS)
        {

            //超时只报告失败，不释放帧、不假定目标任务已经停止
            s_control_state = CONTROL_ERROR;
            s_control_command_sent = false;
            g_control_task_debug.result = SYS_ERR_TIMEOUT;
        }
    }
    g_control_task_debug.state = s_control_state;
}

//建立下一条协调命令
static void ControlTask_Command(ControlState_t state)
{
    if (s_control_sequence == UINT32_MAX)
    {
        s_control_state = CONTROL_ERROR;
        s_control_command_sent = false;
        g_control_task_debug.result = SYS_ERR_NO_RESOURCE;
        return;
    }
    s_control_state = state;
    s_control_command.type = state == CONTROL_START_CAMERA || state == CONTROL_START_USB ?
        APP_MESSAGE_START : APP_MESSAGE_STOP;
    s_control_command.source = RUNTIME_TARGET_CONTROL;
    s_control_command.command_id = ++s_control_sequence;
    s_control_command_sent = false;
    s_control_command_tick = Platform_TimeGetMs();
    g_control_task_debug.command_id = s_control_command.command_id;
}

//登记外部协调请求
static SysResult_t ControlTask_Request(AppMessageType_t type)
{
    AppMessage_t message = {.type = APP_MESSAGE_FAULT};
    uint32_t i;

    //调用者也必须属于已登记的业务任务，ISR不能调用队列任务接口
    for (i = 0U; i < RUNTIME_TARGET_COUNT; i++)
        if (App_MessagesIsOwner((RuntimeEventTarget_t)i))
            break;
    if (i == RUNTIME_TARGET_COUNT)
        return SYS_ERR_INVALID_STATE;
    message.type = type;
    message.source = (RuntimeEventTarget_t)i;
    return App_MessagesSend(APP_CHANNEL_CONTROL, &message);
}

//请求协调停止数据面
SysResult_t ControlTask_RequestStop(void)
{
    return ControlTask_Request(APP_MESSAGE_STOP);
}

//请求协调启动数据面
SysResult_t ControlTask_RequestStart(void)
{
    return ControlTask_Request(APP_MESSAGE_START);
}
