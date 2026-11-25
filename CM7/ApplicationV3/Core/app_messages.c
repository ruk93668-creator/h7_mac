#include "app_messages.h"
#include "app_events.h"
#include "../Runtime/queue_pool.h"
#include "app_services.h"
#include "platform_sync.h"
#include <stddef.h>

//每个业务通道独占的消息槽数量
#define APP_MESSAGE_QUEUE_LENGTH 4U
#define APP_MESSAGE_PHYSICAL_CHANNELS (APP_CHANNEL_COUNT + 2U)

//通道只保存池句柄，队列控制块与数据区集中由固定池管理。
static AppV3Queue_t s_message_handles[APP_MESSAGE_PHYSICAL_CHANNELS];

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
    for (channel_index = 0U; channel_index < APP_MESSAGE_PHYSICAL_CHANNELS; channel_index++)
    {
        SysResult_t result = AppV3Queue_Alloc(sizeof(AppMessage_t), APP_MESSAGE_QUEUE_LENGTH, &s_message_handles[channel_index]);
        if (result != SYS_OK)
            return result;
    }
    return SYS_OK;
}

//历史命令保持禁用。两条录像FIFO复用其原有静态槽，不增加池容量。
static AppChannel_t AppMessages_Slot(AppChannel_t channel)
{
    if (channel == APP_CHANNEL_STORAGE_TO_CAMERA)
        return APP_CHANNEL_CAMERA_COMMAND;
    if (channel == APP_CHANNEL_CAMERA_TO_STORAGE)
        return APP_CHANNEL_USB_COMMAND;
    if (channel == APP_CHANNEL_NETWORK_TO_CAMERA)
        return (AppChannel_t)APP_CHANNEL_COUNT;
    if (channel == APP_CHANNEL_CAMERA_TO_NETWORK)
        return (AppChannel_t)(APP_CHANNEL_COUNT + 1U);
    return channel;
}

//查找当前允许接收指定通道消息的任务身份
static RuntimeEventTarget_t AppMessages_Receiver(AppChannel_t channel)
{
    if (channel == APP_CHANNEL_STORAGE_TO_CAMERA)
        return RUNTIME_TARGET_CAMERA;
    if (channel == APP_CHANNEL_CAMERA_TO_STORAGE)
        return RUNTIME_TARGET_STORAGE;
    if (channel == APP_CHANNEL_NETWORK_TO_CAMERA)
        return RUNTIME_TARGET_CAMERA;
    if (channel == APP_CHANNEL_CAMERA_TO_NETWORK)
        return RUNTIME_TARGET_NETWORK;
    return s_message_receivers[channel];
}

//只允许已登记的通道进入队列。
static bool AppMessages_IsActive(AppChannel_t channel)
{
    return channel == APP_CHANNEL_CONTROL || channel == APP_CHANNEL_STARTUP ||
        channel == APP_CHANNEL_TO_CAMERA || channel == APP_CHANNEL_TO_USB ||
        channel == APP_CHANNEL_STORAGE_TO_CAMERA || channel == APP_CHANNEL_CAMERA_TO_STORAGE ||
        channel == APP_CHANNEL_NETWORK_TO_CAMERA || channel == APP_CHANNEL_CAMERA_TO_NETWORK;
}

//数据通道校验发送任务和方向，其他业务类型不得挤占租约归还通道
static SysResult_t AppMessages_CheckFrameRoute(AppChannel_t channel, const AppMessage_t *message)
{
    if (channel == APP_CHANNEL_TO_CAMERA || channel == APP_CHANNEL_STORAGE_TO_CAMERA ||
        channel == APP_CHANNEL_NETWORK_TO_CAMERA)
    {
        RuntimeEventTarget_t sender = channel == APP_CHANNEL_TO_CAMERA ? RUNTIME_TARGET_USB :
            (channel == APP_CHANNEL_STORAGE_TO_CAMERA ? RUNTIME_TARGET_STORAGE : RUNTIME_TARGET_NETWORK);
        if (message->type != APP_MESSAGE_FRAME_REQUEST && message->type != APP_MESSAGE_FRAME_CANCEL_REQUEST &&
            message->type != APP_MESSAGE_FRAME_RETURN && message->type != APP_MESSAGE_RETURN_ACK_CONSUMED)
            return SYS_ERR_NOT_SUPPORTED;
        if (message->source != sender)
            return SYS_ERR_INVALID_STATE;
    }
    if (channel == APP_CHANNEL_TO_USB || channel == APP_CHANNEL_CAMERA_TO_STORAGE ||
        channel == APP_CHANNEL_CAMERA_TO_NETWORK)
    {
        if (message->type != APP_MESSAGE_FRAME_OFFER && message->type != APP_MESSAGE_RETURN_ACK)
            return SYS_ERR_NOT_SUPPORTED;
        if (message->source != RUNTIME_TARGET_CAMERA)
            return SYS_ERR_INVALID_STATE;
    }
    return SYS_OK;
}

//非阻塞提交消息
SysResult_t App_MessagesSend(AppChannel_t channel, const AppMessage_t *message)
{
    SysResult_t result;

    if ((uint32_t)channel > APP_CHANNEL_CAMERA_TO_NETWORK || message == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!AppMessages_IsActive(channel))
        return SYS_ERR_NOT_SUPPORTED;
    result = AppMessages_CheckFrameRoute(channel, message);
    if (result != SYS_OK)
        return result;
    //未分配通道或中断调用不能进入任务消息接口
    if (s_message_handles[AppMessages_Slot(channel)].generation == 0U || Platform_SyncGetException() != 0U)
        return SYS_ERR_INVALID_STATE;

    //启动报告只接受Registry服务的实际所有者，业务结果保持原来源校验
    if (!App_MessagesIsOwner(message->source) ||
        (channel == APP_CHANNEL_STARTUP && AppV3_FindService(message->source) == NULL))
        return SYS_ERR_INVALID_STATE;

    //成功入队就是消息已提交，通知位只负责唤醒，不作为交付凭据
    if (AppV3Queue_Put(s_message_handles[AppMessages_Slot(channel)], message, 0U) != SYS_OK)
        return SYS_ERR_NO_RESOURCE;
    if (channel != APP_CHANNEL_STARTUP)
        (void)RuntimeEvent_Post(AppMessages_Receiver(channel), RUNTIME_EVENT_WAKE);
    return SYS_OK;
}

//取得当前任务所属通道的下一条消息
bool App_MessagesReceive(AppChannel_t channel, AppMessage_t *message)
{
    if (!AppMessages_IsActive(channel) || message == NULL ||
        s_message_handles[AppMessages_Slot(channel)].generation == 0U || Platform_SyncGetException() != 0U ||
        !App_MessagesIsOwner(AppMessages_Receiver(channel)))
        return false;
    //只有当前任务成功取走队首时才通知发送者重试
    if (AppV3Queue_Get(s_message_handles[AppMessages_Slot(channel)], message, 0U) != SYS_OK)
        return false;

    //腾出槽位后唤醒发送方，队列满时保存的消息才能继续提交
    if (channel == APP_CHANNEL_CONTROL)
    {
        (void)RuntimeEvent_Post(RUNTIME_TARGET_CAMERA, RUNTIME_EVENT_WAKE);
        (void)RuntimeEvent_Post(RUNTIME_TARGET_USB, RUNTIME_EVENT_WAKE);
        (void)RuntimeEvent_Post(RUNTIME_TARGET_STORAGE, RUNTIME_EVENT_WAKE);
        (void)RuntimeEvent_Post(RUNTIME_TARGET_NETWORK, RUNTIME_EVENT_WAKE);
    }
    else if (channel == APP_CHANNEL_TO_CAMERA)
        (void)RuntimeEvent_Post(RUNTIME_TARGET_USB, RUNTIME_EVENT_WAKE);
    else if (channel == APP_CHANNEL_NETWORK_TO_CAMERA)
        (void)RuntimeEvent_Post(RUNTIME_TARGET_NETWORK, RUNTIME_EVENT_WAKE);
    else if (channel == APP_CHANNEL_STORAGE_TO_CAMERA)
        (void)RuntimeEvent_Post(RUNTIME_TARGET_STORAGE, RUNTIME_EVENT_WAKE);
    else if (channel == APP_CHANNEL_TO_USB || channel == APP_CHANNEL_CAMERA_TO_STORAGE ||
        channel == APP_CHANNEL_CAMERA_TO_NETWORK)
        (void)RuntimeEvent_Post(RUNTIME_TARGET_CAMERA, RUNTIME_EVENT_WAKE);
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
