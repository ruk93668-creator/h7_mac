#include "app_messages.h"
#include "app_events.h"
#include "../Runtime/queue_pool.h"
#include "app_services.h"
#include "platform_sync.h"
#include <stddef.h>

#define APP_MESSAGE_QUEUE_LENGTH 4U

//通道只保存池句柄，队列控制块与数据区集中由固定池管理。
static AppV3Queue_t s_message_handles[APP_CHANNEL_COUNT];

//记录队列初始化尝试，部分失败后不复用静态控制块
static bool s_messages_attempted;

//保存各通道唯一的接收任务
static const RuntimeEventTarget_t s_message_receivers[APP_CHANNEL_COUNT] =
{
    RUNTIME_TARGET_CAMERA, RUNTIME_TARGET_USB, RUNTIME_TARGET_CONTROL,
    RUNTIME_TARGET_CAMERA, RUNTIME_TARGET_USB, RUNTIME_TARGET_CONTROL
};

//检查当前任务是否为指定业务所有者
bool App_MessagesIsOwner(RuntimeEventTarget_t target)
{
    return App_EventsIsOwner(target);
}

//准备静态消息队列
SysResult_t App_MessagesInit(void)
{
    uint32_t channel_index;

    //只允许调度前创建一次，任何部分失败都由main终止启动
    if (s_messages_attempted || Platform_SyncGetException() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;
    s_messages_attempted = true;
    //逐通道分配固定槽，部分失败交由启动流程处理
    for (channel_index = 0U; channel_index < APP_CHANNEL_COUNT; channel_index++)
    {
        SysResult_t result = AppV3Queue_Alloc(sizeof(AppMessage_t), APP_MESSAGE_QUEUE_LENGTH, &s_message_handles[channel_index]);
        if (result != SYS_OK)
            return result;
    }
    return SYS_OK;
}

//非阻塞提交消息
SysResult_t App_MessagesSend(AppChannel_t channel, const AppMessage_t *message)
{
    bool allowed;

    if ((uint32_t)channel >= APP_CHANNEL_COUNT || message == NULL)
        return SYS_ERR_INVALID_ARG;
    //未分配通道或中断调用不能进入任务消息接口
    if (s_message_handles[channel].generation == 0U || Platform_SyncGetException() != 0U)
        return SYS_ERR_INVALID_STATE;

    //命令只由Control发送，帧消息只允许相应所有者发送
    if (channel == APP_CHANNEL_CAMERA_COMMAND || channel == APP_CHANNEL_USB_COMMAND)
        allowed = App_MessagesIsOwner(RUNTIME_TARGET_CONTROL);
    else if (channel == APP_CHANNEL_TO_CAMERA)
        allowed = App_MessagesIsOwner(RUNTIME_TARGET_USB);
    else if (channel == APP_CHANNEL_TO_USB)
        allowed = App_MessagesIsOwner(RUNTIME_TARGET_CAMERA);
    else if (channel == APP_CHANNEL_STARTUP)
        allowed = AppV3_FindService(message->source) != NULL && App_MessagesIsOwner(message->source);
    else
        allowed = App_MessagesIsOwner(message->source);
    if (!allowed)
        return SYS_ERR_INVALID_STATE;

    //成功入队就是消息已提交，通知位只负责唤醒，不作为交付凭据
    if (AppV3Queue_Put(s_message_handles[channel], message, 0U) != SYS_OK)
        return SYS_ERR_NO_RESOURCE;
    if (channel != APP_CHANNEL_STARTUP)
        (void)RuntimeEvent_Post(s_message_receivers[channel], RUNTIME_EVENT_WAKE);
    return SYS_OK;
}

//取得当前任务所属通道的下一条消息
bool App_MessagesReceive(AppChannel_t channel, AppMessage_t *message)
{
    if ((uint32_t)channel >= APP_CHANNEL_COUNT || message == NULL ||
        s_message_handles[channel].generation == 0U || !App_MessagesIsOwner(s_message_receivers[channel]))
        return false;
    //只有当前任务成功取走队首时才通知发送者重试
    if (AppV3Queue_Get(s_message_handles[channel], message, 0U) != SYS_OK)
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
        (void)RuntimeEvent_Post(RUNTIME_TARGET_STORAGE, RUNTIME_EVENT_WAKE);
    }
    else if (channel != APP_CHANNEL_STARTUP)
        (void)RuntimeEvent_Post(RUNTIME_TARGET_CONTROL, RUNTIME_EVENT_WAKE);
    return true;
}

//启动队列仅交给 Control 的 Event Loop，报告由各 Service 经验证后提交。
SysResult_t AppV3_GetStartupQueue(AppV3Queue_t *queue)
{
    if (queue == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!App_MessagesIsOwner(RUNTIME_TARGET_CONTROL) || s_message_handles[APP_CHANNEL_STARTUP].generation == 0U)
        return SYS_ERR_INVALID_STATE;
    //交给Control的句柄指向原队列，Event Loop无需另建缓冲区
    *queue = s_message_handles[APP_CHANNEL_STARTUP];
    return SYS_OK;
}
