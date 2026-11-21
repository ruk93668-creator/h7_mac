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
static uint32_t s_active_ticket;

typedef enum
{
    BUSINESS_RESULT_EMPTY = 0,
    BUSINESS_RESULT_PENDING,
    BUSINESS_RESULT_READY
} BusinessResultState_t;

typedef struct
{
    AppV3ServiceEvent_t event;
    BusinessResultState_t state;
} BusinessResultSlot_t;

//普通任务各有一个固定结果槽；Control继续使用同步订阅
static BusinessResultSlot_t s_results[RUNTIME_TARGET_COUNT - 1U];

//按实际任务身份取槽，数组不为Control分配结果存储
static BusinessResultSlot_t *Business_ResultSlot(RuntimeEventTarget_t requester)
{
    uint32_t index = (uint32_t)requester;

    if (index >= RUNTIME_TARGET_COUNT || requester == RUNTIME_TARGET_CONTROL)
        return NULL;
    if (index > RUNTIME_TARGET_CONTROL)
        --index;
    return &s_results[index];
}

//先预留结果再入队；上次结果未取走时不接受该任务的新请求
SysResult_t AppV3Business_ReserveResult(RuntimeEventTarget_t requester, RuntimeEventTarget_t service,
    uint32_t command_id)
{
    BusinessResultSlot_t *slot = Business_ResultSlot(requester);
    SysResult_t result = SYS_ERR_BUSY;

    if (slot == NULL || !App_MessagesIsOwner(requester))
        return SYS_ERR_INVALID_STATE;
    if (command_id == 0U || requester == service || AppV3_FindService(service) == NULL)
        return SYS_ERR_INVALID_ARG;

    //结果槽仅在任务之间共享，提交身份与阶段在同一个短临界区更新
    taskENTER_CRITICAL();
    if (slot->state == BUSINESS_RESULT_EMPTY)
    {
        slot->event.service = service;
        slot->event.requester = requester;
        slot->event.command_id = command_id;
        slot->state = BUSINESS_RESULT_PENDING;
        result = SYS_OK;
    }
    taskEXIT_CRITICAL();
    return result;
}

//撤销只限原请求者和仍待完成的准确键，不覆盖已经收到的真实结果
void AppV3Business_CancelReservation(RuntimeEventTarget_t requester, RuntimeEventTarget_t service,
    uint32_t command_id)
{
    BusinessResultSlot_t *slot = Business_ResultSlot(requester);
    if (slot == NULL || !App_MessagesIsOwner(requester))
        return;

    taskENTER_CRITICAL();
    if (slot->state == BUSINESS_RESULT_PENDING && slot->event.service == service &&
        slot->event.command_id == command_id)
        slot->state = BUSINESS_RESULT_EMPTY;
    taskEXIT_CRITICAL();
}

//只让调用任务取走自己的匹配结果；零等待，完整结果按值复制
SysResult_t AppV3Business_TakeResult(RuntimeEventTarget_t service, uint32_t command_id,
    AppV3ServiceEvent_t *result)
{
    RuntimeEventTarget_t requester;
    BusinessResultSlot_t *slot;
    SysResult_t status = SYS_ERR_NOT_FOUND;

    if (result == NULL || command_id == 0U || AppV3_FindService(service) == NULL)
        return SYS_ERR_INVALID_ARG;
    for (requester = RUNTIME_TARGET_CAMERA; requester < RUNTIME_TARGET_COUNT; ++requester)
    {
        if (App_MessagesIsOwner(requester))
            break;
    }
    slot = Business_ResultSlot(requester);
    if (slot == NULL)
        return SYS_ERR_INVALID_STATE;

    taskENTER_CRITICAL();
    if (slot->state != BUSINESS_RESULT_EMPTY && slot->event.service == service &&
        slot->event.command_id == command_id)
    {
        status = SYS_ERR_BUSY;
        if (slot->state == BUSINESS_RESULT_READY)
        {
            *result = slot->event;
            slot->state = BUSINESS_RESULT_EMPTY;
            status = SYS_OK;
        }
    }
    taskEXIT_CRITICAL();
    return status;
}

//Control先保存终结事实，再发通知；漏通知也不影响后续轮询取得结果
static bool Business_RouteResult(const AppV3ServiceEvent_t *event)
{
    BusinessResultSlot_t *slot = Business_ResultSlot(event->requester);
    bool delivered = false;

    if (slot == NULL)
        return event->requester == RUNTIME_TARGET_CONTROL;
    taskENTER_CRITICAL();
    if (slot->state == BUSINESS_RESULT_PENDING && slot->event.service == event->service &&
        slot->event.command_id == event->command_id)
    {
        slot->event = *event;
        slot->state = BUSINESS_RESULT_READY;
        delivered = true;
    }
    taskEXIT_CRITICAL();
    if (delivered)
        (void)RuntimeEvent_Post(event->requester, RUNTIME_EVENT_WAKE);
    return delivered;
}

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
        //活跃回调的Context仍在使用，撤销必须等本次同步调用退出
        if (s_active_ticket == ticket)
            return SYS_ERR_BUSY;
        s_subscriptions[slot_index] = (AppV3BusinessSubscription_t){0};
        return SYS_OK;
    }
    return SYS_ERR_NOT_FOUND;
}

//兼容固定52B业务队列，只有明确的值类型结果标记才能解码
static bool Business_Decode(const AppMessage_t *message, AppV3ServiceEvent_t *event)
{
    bool hardware = message->frame.size_bytes == APP_V3_HARDWARE_RESULT_TAG;

    if (AppV3_FindService(message->source) == NULL || message->command_id == 0U ||
        (!hardware && message->frame.size_bytes != APP_V3_SERVICE_RESULT_TAG) ||
        message->stream_epoch == 0U || message->timestamp_ms >= RUNTIME_TARGET_COUNT ||
        message->frame.data != NULL || message->frame.data2 != NULL ||
        message->frame_id > (hardware ? (uint32_t)APP_V3_HW_FAULT : 1U) ||
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
        hardware ? 0U : message->lease_id,
        !hardware && message->frame_id != 0U,
        hardware,
        { .state = APP_V3_HW_UNINITIALIZED }
    };
    if (hardware)
    {
        event->hardware.state = (AppV3HardwareState_t)message->frame_id;
        event->hardware.first_fault = -(SysResult_t)message->frame.height;
        event->hardware.primary = message->lease_id;
        event->hardware.detail = message->frame.size_bytes2;
        event->hardware.flags = message->frame.width;
    }
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
        s_active_ticket = slot->ticket;
        result = slot->callback(event, slot->context);
        s_active_ticket = 0U;
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
        if (Business_Decode(&message, &event) && Business_RouteResult(&event))
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
