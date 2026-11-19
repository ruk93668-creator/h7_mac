#include "legacy_lifecycle_messages.h"
#include "app_events.h"
#include "queue.h"
#include "platform_sync.h"
#include <stddef.h>

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

// 历史测试显式初始化三条生命周期队列，不注册产品 Initcall。
SysResult_t App_LifecycleMessagesInit(void)
{
    uint32_t i;

    //初始化只能发生一次，失败后保持所有新通道关闭
    //调度器启动前，FreeRTOS创建任务/队列可能保留BASEPRI，不能据此拒绝初始化
    //仍拒绝重复初始化、ISR、PRIMASK屏蔽和已经启动的调度器
    if (s_lifecycle_attempted || Platform_SyncGetException() != 0U ||
        Platform_SyncGetPrimask() != 0U ||
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
