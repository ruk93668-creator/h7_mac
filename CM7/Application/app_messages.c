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
