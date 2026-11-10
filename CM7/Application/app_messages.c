#include "app_messages.h"
#include "app_events.h"
#include "queue.h"
#include "platform_sync.h"
#include <stddef.h>

#define APP_MESSAGE_QUEUE_LENGTH 4U

//每个方向使用独立静态队列，命令不与图像消息争用槽位
static StaticQueue_t s_message_queues[APP_CHANNEL_COUNT];

//队列内只存描述和凭据，图像仍由Media管理
static uint8_t s_message_storage[APP_CHANNEL_COUNT][APP_MESSAGE_QUEUE_LENGTH * sizeof(AppMessage_t)]
    __attribute__((aligned(8)));

//保存各通道的内核队列句柄
static QueueHandle_t s_message_handles[APP_CHANNEL_COUNT];

//记录队列初始化尝试，部分失败后不复用静态控制块
static bool s_messages_attempted;

//保存各通道唯一的接收任务
static const RuntimeEventTarget_t s_message_receivers[APP_CHANNEL_COUNT] =
{
    RUNTIME_TARGET_CAMERA, RUNTIME_TARGET_USB, RUNTIME_TARGET_CONTROL,
    RUNTIME_TARGET_CAMERA, RUNTIME_TARGET_USB
};

//检查当前任务是否为指定业务所有者
bool App_MessagesIsOwner(RuntimeEventTarget_t target)
{
    return App_EventsIsOwner(target);
}

//准备静态消息队列
SysResult_t App_MessagesInit(void)
{
    uint32_t i;

    //只允许调度前创建一次，任何部分失败都由main终止启动
    if (s_messages_attempted || Platform_SyncGetException() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;
    s_messages_attempted = true;
    for (i = 0U; i < APP_CHANNEL_COUNT; i++)
    {
        s_message_handles[i] = xQueueCreateStatic(APP_MESSAGE_QUEUE_LENGTH,
            sizeof(AppMessage_t), s_message_storage[i], &s_message_queues[i]);
        if (s_message_handles[i] == NULL)
            return SYS_ERR_NO_RESOURCE;
    }
    return SYS_OK;
}

//非阻塞提交消息
SysResult_t App_MessagesSend(AppChannel_t channel, const AppMessage_t *message)
{
    bool allowed;

    if ((uint32_t)channel >= APP_CHANNEL_COUNT || message == NULL)
        return SYS_ERR_INVALID_ARG;
    if (s_message_handles[channel] == NULL || Platform_SyncGetException() != 0U)
        return SYS_ERR_INVALID_STATE;

    //命令只由Control发送，帧消息只允许相应所有者发送
    if (channel == APP_CHANNEL_CAMERA_COMMAND || channel == APP_CHANNEL_USB_COMMAND)
        allowed = App_MessagesIsOwner(RUNTIME_TARGET_CONTROL);
    else if (channel == APP_CHANNEL_TO_CAMERA)
        allowed = App_MessagesIsOwner(RUNTIME_TARGET_USB);
    else if (channel == APP_CHANNEL_TO_USB)
        allowed = App_MessagesIsOwner(RUNTIME_TARGET_CAMERA);
    else
        allowed = App_MessagesIsOwner(message->source);
    if (!allowed)
        return SYS_ERR_INVALID_STATE;

    //成功入队就是消息已提交，通知位只负责唤醒，不作为交付凭据
    if (xQueueSend(s_message_handles[channel], message, 0U) != pdPASS)
        return SYS_ERR_NO_RESOURCE;
    (void)RuntimeEvent_Post(s_message_receivers[channel], RUNTIME_EVENT_WAKE);
    return SYS_OK;
}

//取得当前任务所属通道的下一条消息
bool App_MessagesReceive(AppChannel_t channel, AppMessage_t *message)
{
    if ((uint32_t)channel >= APP_CHANNEL_COUNT || message == NULL ||
        s_message_handles[channel] == NULL || !App_MessagesIsOwner(s_message_receivers[channel]))
        return false;
    if (xQueueReceive(s_message_handles[channel], message, 0U) != pdPASS)
        return false;

    //腾出槽位后唤醒发送方，队列满时保存的消息才能继续提交
    if (channel == APP_CHANNEL_TO_CAMERA)
        (void)RuntimeEvent_Post(RUNTIME_TARGET_USB, RUNTIME_EVENT_WAKE);
    else if (channel == APP_CHANNEL_TO_USB)
        (void)RuntimeEvent_Post(RUNTIME_TARGET_CAMERA, RUNTIME_EVENT_WAKE);
    else if (channel == APP_CHANNEL_CONTROL)
    {
        (void)RuntimeEvent_Post(RUNTIME_TARGET_CAMERA, RUNTIME_EVENT_WAKE);
        (void)RuntimeEvent_Post(RUNTIME_TARGET_USB, RUNTIME_EVENT_WAKE);
    }
    else
        (void)RuntimeEvent_Post(RUNTIME_TARGET_CONTROL, RUNTIME_EVENT_WAKE);
    return true;
}

//C1独立通道仅准备基础设施，原有五条队列和收发函数保持不变
#include "framework_init.h"
#define APP_LIFECYCLE_QUEUE_LENGTH 4U

//保存三条生命周期队列的静态控制块
static StaticQueue_t s_lifecycle_queues[APP_LIFECYCLE_CHANNEL_COUNT];

//每项固定56B，不扩大旧AppMessage的复制范围
static uint8_t s_lifecycle_storage[APP_LIFECYCLE_CHANNEL_COUNT]
    [APP_LIFECYCLE_QUEUE_LENGTH * sizeof(AppLifecycleMessage_t)] __attribute__((aligned(8)));

//保存各生命周期通道的内核句柄
static QueueHandle_t s_lifecycle_handles[APP_LIFECYCLE_CHANNEL_COUNT];

//部分初始化失败后也不重复使用同一份静态控制块
static bool s_lifecycle_attempted;

//只有全部通道成功创建后才开放收发
static bool s_lifecycle_ready;

//将普通C协议角色映射到现有任务身份
static RuntimeEventTarget_t Lifecycle_Task(uint32_t role);

//查询指定通道唯一的接收角色
static uint32_t Lifecycle_Receiver(AppLifecycleChannel_t channel);

//检查普通任务是否可以使用非阻塞RTOS队列
static bool Lifecycle_Context(void);

//将协议角色映射到已登记任务，协议中不保存TaskHandle
static RuntimeEventTarget_t Lifecycle_Task(uint32_t role)
{
    switch (role)
    {
        case APP_LIFECYCLE_CAMERA: return RUNTIME_TARGET_CAMERA;
        case APP_LIFECYCLE_USB: return RUNTIME_TARGET_USB;
        case APP_LIFECYCLE_STORAGE: return RUNTIME_TARGET_STORAGE;
        default: return RUNTIME_TARGET_COUNT;
    }
}

//每条通道只服务一个接收任务，不能按消息内容任意改路由
static uint32_t Lifecycle_Receiver(AppLifecycleChannel_t channel)
{
    switch (channel)
    {
        case APP_LIFECYCLE_CHANNEL_CAMERA: return APP_LIFECYCLE_CAMERA;
        case APP_LIFECYCLE_CHANNEL_USB: return APP_LIFECYCLE_USB;
        case APP_LIFECYCLE_CHANNEL_STORAGE: return APP_LIFECYCLE_STORAGE;
        default: return 0U;
    }
}

//不允许ISR、屏蔽中断或调度器未运行时进入任务侧收发
static bool Lifecycle_Context(void)
{
    return Platform_SyncGetException() == 0U && Platform_SyncGetPrimask() == 0U &&
        Platform_SyncGetBasepri() == 0U && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

//Framework准备独立静态队列，不请求采集、USB或文件系统
SysResult_t App_LifecycleMessagesInit(void)
{
    uint32_t i;

    //初始化只能发生一次，失败后保持所有新通道关闭
    if (s_lifecycle_attempted || Platform_SyncGetException() != 0U ||
        Platform_SyncGetPrimask() != 0U || Platform_SyncGetBasepri() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;
    s_lifecycle_attempted = true;
    for (i = 0U; i < APP_LIFECYCLE_CHANNEL_COUNT; i++)
    {
        s_lifecycle_handles[i] = xQueueCreateStatic(APP_LIFECYCLE_QUEUE_LENGTH,
            sizeof(AppLifecycleMessage_t), s_lifecycle_storage[i], &s_lifecycle_queues[i]);
        if (s_lifecycle_handles[i] == NULL)
            return SYS_ERR_NO_RESOURCE;
    }
    s_lifecycle_ready = true;
    return SYS_OK;
}

//只有消息声明与当前任务身份同时合法才允许入队
SysResult_t App_LifecycleMessagesSend(AppLifecycleChannel_t channel, const AppLifecycleMessage_t *message)
{
    if ((uint32_t)channel >= APP_LIFECYCLE_CHANNEL_COUNT || !App_LifecycleValidate(message))
        return SYS_ERR_INVALID_ARG;
    if (!s_lifecycle_ready || !Lifecycle_Context())
        return SYS_ERR_INVALID_STATE;

    //不能冒充另一消费者，也不能把合法消息投向错误任务
    if (message->target != Lifecycle_Receiver(channel) ||
        !App_EventsIsOwner(Lifecycle_Task(message->source)))
        return SYS_ERR_INVALID_STATE;
    if (xQueueSend(s_lifecycle_handles[channel], message, 0U) != pdPASS)
        return SYS_ERR_NO_RESOURCE;

    //入队后即已交付给队列，通知失败也不能要求重复入队
    (void)RuntimeEvent_Post(Lifecycle_Task(message->target), RUNTIME_EVENT_WAKE);
    return SYS_OK;
}

//取走消息后唤醒可能等待槽位的所有合法发送者
bool App_LifecycleMessagesReceive(AppLifecycleChannel_t channel, AppLifecycleMessage_t *message)
{
    if ((uint32_t)channel >= APP_LIFECYCLE_CHANNEL_COUNT || message == NULL ||
        !s_lifecycle_ready || !Lifecycle_Context() ||
        !App_EventsIsOwner(Lifecycle_Task(Lifecycle_Receiver(channel))))
        return false;
    if (xQueueReceive(s_lifecycle_handles[channel], message, 0U) != pdPASS)
        return false;

    //两个消费者共享Camera收件箱，腾出槽位后都需要获得重试机会
    if (channel == APP_LIFECYCLE_CHANNEL_CAMERA)
    {
        (void)RuntimeEvent_Post(RUNTIME_TARGET_USB, RUNTIME_EVENT_WAKE);
        (void)RuntimeEvent_Post(RUNTIME_TARGET_STORAGE, RUNTIME_EVENT_WAKE);
    }
    else
        (void)RuntimeEvent_Post(RUNTIME_TARGET_CAMERA, RUNTIME_EVENT_WAKE);
    return true;
}

FRAMEWORK_INIT_REGISTER(App_LifecycleMessagesInit, INIT_LEVEL_APPLICATION);
