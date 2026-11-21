#include "service_events.h"
#include "service_mailbox.h"
#include "../Core/app_services.h"

#define BUSINESS_SUBSCRIPTIONS 3U
#define BUSINESS_BATCH 4U

typedef struct
{
    AppV3BusinessCallback_t callback;
    void *context;
    RuntimeEventTarget_t service;
    uint32_t ticket;
} AppV3BusinessSubscription_t;

//订阅、分发与诊断仅由Control操作，不在全局中断锁内调用外部回调
static AppV3BusinessSubscription_t s_subscriptions[BUSINESS_SUBSCRIPTIONS];
static AppV3BusinessDiagnostics_t s_diagnostics;
static uint32_t s_last_ticket;
static bool s_draining;

//分配不可回绕的订阅票据，释放后旧票据不能撤销新订阅
SysResult_t AppV3Business_Subscribe(RuntimeEventTarget_t service,
    AppV3BusinessCallback_t callback, void *context, uint32_t *ticket)
{
    uint32_t slot_index, free_index = BUSINESS_SUBSCRIPTIONS;

    if (callback == NULL || ticket == NULL ||
        (service != RUNTIME_TARGET_COUNT && AppV3_FindService(service) == NULL))
        return SYS_ERR_INVALID_ARG;
    if (!App_MessagesIsOwner(RUNTIME_TARGET_CONTROL))
        return SYS_ERR_INVALID_STATE;

    //检查完整订阅身份，并保留第一个可用槽
    for (slot_index = 0U; slot_index < BUSINESS_SUBSCRIPTIONS; ++slot_index)
    {
        const AppV3BusinessSubscription_t *slot = &s_subscriptions[slot_index];
        if (slot->ticket == 0U && free_index == BUSINESS_SUBSCRIPTIONS)
            free_index = slot_index;
        if (slot->callback == callback && slot->context == context && slot->service == service)
            return SYS_ERR_ALREADY_EXISTS;
    }
    if (free_index == BUSINESS_SUBSCRIPTIONS || s_last_ticket == UINT32_MAX)
        return SYS_ERR_NO_RESOURCE;
    *ticket = ++s_last_ticket;
    s_subscriptions[free_index] = (AppV3BusinessSubscription_t){callback, context, service, *ticket};
    return SYS_OK;
}

//所属Control完成撤销后，上下文不会再被后续回调访问
SysResult_t AppV3Business_Unsubscribe(uint32_t ticket)
{
    uint32_t slot_index;

    if (ticket == 0U)
        return SYS_ERR_INVALID_ARG;
    if (!App_MessagesIsOwner(RUNTIME_TARGET_CONTROL))
        return SYS_ERR_INVALID_STATE;
    for (slot_index = 0U; slot_index < BUSINESS_SUBSCRIPTIONS; ++slot_index)
    {
        if (s_subscriptions[slot_index].ticket != ticket)
            continue;
        s_subscriptions[slot_index] = (AppV3BusinessSubscription_t){0};
        return SYS_OK;
    }
    return SYS_ERR_NOT_FOUND;
}

//兼容固定52B公共队列。仅识别明确标记的模拟结果，不解析真实帧字段
static bool Business_Decode(const AppMessage_t *message, AppV3ServiceEvent_t *event)
{
    if (AppV3_FindService(message->source) == NULL || message->command_id == 0U ||
        message->frame.size_bytes != APP_V3_SERVICE_RESULT_TAG || message->stream_epoch == 0U ||
        message->frame_id > 1U || message->timestamp_ms >= RUNTIME_TARGET_COUNT ||
        message->timestamp_ms == (uint32_t)message->source)
        return false;
    if (!((message->type == APP_MESSAGE_ACK && message->result == SYS_OK) ||
        (message->type == APP_MESSAGE_FAULT && message->result < SYS_OK)))
        return false;

    *event = (AppV3ServiceEvent_t)
    {
        message->source,
        (RuntimeEventTarget_t)message->timestamp_ms,
        message->command_id,
        message->stream_epoch,
        message->result,
        message->lease_id,
        message->frame_id != 0U
    };
    return true;
}

//快照只复制票据；撤销或本轮新增不会访问已经失效的回调上下文
static SysResult_t Business_Publish(const AppV3ServiceEvent_t *event)
{
    uint32_t tickets[BUSINESS_SUBSCRIPTIONS], slot_index;
    SysResult_t first_error = SYS_OK;

    for (slot_index = 0U; slot_index < BUSINESS_SUBSCRIPTIONS; ++slot_index)
        tickets[slot_index] = s_subscriptions[slot_index].ticket;
    for (slot_index = 0U; slot_index < BUSINESS_SUBSCRIPTIONS; ++slot_index)
    {
        const AppV3BusinessSubscription_t *slot = &s_subscriptions[slot_index];
        SysResult_t result;
        if (tickets[slot_index] == 0U || tickets[slot_index] != slot->ticket ||
            (slot->service != event->service && slot->service != RUNTIME_TARGET_COUNT))
            continue;

        //回调失败只记发布诊断，不改原Service的操作结果，其他订阅者仍有机会执行
        result = slot->callback(event, slot->context);
        if (result != SYS_OK)
        {
            if (s_diagnostics.callback_failures != UINT32_MAX)
                ++s_diagnostics.callback_failures;
            if (first_error == SYS_OK)
                first_error = result;
        }
    }
    return first_error;
}

//Control每轮最多消费公共队列深度，回调不可递归进入Drain
SysResult_t AppV3Business_Drain(uint32_t *consumed)
{
    SysResult_t first_error = SYS_OK;
    AppMessage_t message;

    if (consumed == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!App_MessagesIsOwner(RUNTIME_TARGET_CONTROL) || !AppV3_BusinessReady())
        return SYS_ERR_INVALID_STATE;
    if (s_draining)
        return SYS_ERR_BUSY;

    s_draining = true;
    for (*consumed = 0U; *consumed < BUSINESS_BATCH &&
        App_MessagesReceive(APP_CHANNEL_CONTROL, &message); ++*consumed)
    {
        AppV3ServiceEvent_t event;
        SysResult_t result = SYS_ERR_PROTOCOL;

        //非法消息有明确的拒绝结果和完整诊断，不能伪造ACK
        if (Business_Decode(&message, &event))
        {
            if (s_diagnostics.accepted != UINT32_MAX)
                ++s_diagnostics.accepted;
            result = Business_Publish(&event);
        }
        else
        {
            if (s_diagnostics.rejected != UINT32_MAX)
                ++s_diagnostics.rejected;
            //仅保留可审查的值字段，未知消息的帧指针绝不进入诊断快照
            s_diagnostics.rejected_type = message.type;
            s_diagnostics.rejected_source = message.source;
            s_diagnostics.rejected_command_id = message.command_id;
        }
        if (first_error == SYS_OK)
            first_error = result;
    }

    //保持首次消费或分发错误，仅供Control读取
    if (s_diagnostics.first_error == SYS_OK)
        s_diagnostics.first_error = first_error;
    s_draining = false;
    return first_error;
}

//复制诊断，不公开内部对象或跨任务裸引用
SysResult_t AppV3Business_GetDiagnostics(AppV3BusinessDiagnostics_t *diagnostics)
{
    if (diagnostics == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!App_MessagesIsOwner(RUNTIME_TARGET_CONTROL))
        return SYS_ERR_INVALID_STATE;
    *diagnostics = s_diagnostics;
    return SYS_OK;
}
