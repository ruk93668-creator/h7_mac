#include "app_camera_lifecycle_replay.h"
#include <string.h>

//检查输出范围是否覆盖管理对象，避免输出反向破坏编号和缓存
static bool Replay_Overlap(const void *a, size_t na, const void *b, size_t nb);
//验证四个固定请求方向，不根据消息任意改变对象绑定
static bool Replay_Domain(uint32_t source, uint32_t target, uint32_t consumer);
//核对完整消息所属方向，只处理REQUEST
static bool Replay_Bound(const AppLifecycleMessage_t *message, uint32_t source, uint32_t target, uint32_t consumer);
//进入发送对象的单所有者更新区，不改变IRQ或RTOS状态
static SysResult_t Replay_TxEnter(AppLifecycleTxContext_t *tx);
//进入接收对象的单所有者更新区，回调重入只返回BUSY
static SysResult_t Replay_RxEnter(AppLifecycleRxContext_t *rx);
//准备原回复重放，已有待发回复时只合并工作
static SysResult_t Replay_StageReply(AppLifecycleRxContext_t *rx);

//只比较地址范围，不解引用调用者提供的输出
static bool Replay_Overlap(const void *a, size_t na, const void *b, size_t nb)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= y ? y - x < na : x - y < nb;
}

//限制消费者与通信端点组合，Camera反向请求使用独立对象
static bool Replay_Domain(uint32_t source, uint32_t target, uint32_t consumer)
{
    uint32_t peer;
    if (consumer == APP_CONSUMER_USB)
        peer = APP_LIFECYCLE_USB;
    else if (consumer == APP_CONSUMER_STORAGE)
        peer = APP_LIFECYCLE_STORAGE;
    else
        return false;
    return (source == peer && target == APP_LIFECYCLE_CAMERA) ||
        (source == APP_LIFECYCLE_CAMERA && target == peer);
}

//先做既有协议校验，再核对本方向绑定
static bool Replay_Bound(const AppLifecycleMessage_t *message, uint32_t source, uint32_t target, uint32_t consumer)
{
    return App_LifecycleValidate(message) && message->kind == APP_LIFECYCLE_REQUEST &&
        message->source == source && message->target == target && message->identity.consumer_id == consumer;
}

//所有者由Task及现有消息权限保证，本层只禁止同对象重入
static SysResult_t Replay_TxEnter(AppLifecycleTxContext_t *tx)
{
    if (tx == NULL)
        return SYS_ERR_INVALID_ARG;
    if (tx->api_busy != 0U)
        return SYS_ERR_BUSY;
    if (tx->initialized != 1U)
        return SYS_ERR_INVALID_STATE;
    tx->api_busy = 1U;
    return SYS_OK;
}

//不允许在发送回调或提交中间再次修改当前窗口
static SysResult_t Replay_RxEnter(AppLifecycleRxContext_t *rx)
{
    if (rx == NULL)
        return SYS_ERR_INVALID_ARG;
    if (rx->api_busy != 0U)
        return SYS_ERR_BUSY;
    if (rx->initialized != 1U)
        return SYS_ERR_INVALID_STATE;
    rx->api_busy = 1U;
    return SYS_OK;
}

//首次绑定发送对象，不开放Reset或编号回退接口
SysResult_t App_LifecycleTxInit(AppLifecycleTxContext_t *tx, uint32_t source, uint32_t target, uint32_t consumer)
{
    if (tx == NULL || !Replay_Domain(source, target, consumer))
        return SYS_ERR_INVALID_ARG;
    if (tx->api_busy != 0U)
        return SYS_ERR_BUSY;
    if (tx->initialized != 0U)
        return SYS_ERR_ALREADY_EXISTS;
    memset(tx, 0, sizeof(*tx));
    tx->source = source;
    tx->target = target;
    tx->consumer_id = consumer;
    tx->initialized = 1U;
    return SYS_OK;
}

//预留只建立编号额度，不允许在待决事务内继续创建新事务
SysResult_t App_LifecycleTxReserveIds(AppLifecycleTxContext_t *tx, uint32_t count, uint32_t *first)
{
    SysResult_t result;
    if (tx == NULL || first == NULL || Replay_Overlap(tx, sizeof(*tx), first, sizeof(*first)))
        return SYS_ERR_INVALID_ARG;
    result = Replay_TxEnter(tx);
    if (result != SYS_OK)
        return result;
    if (tx->pending.phase != APP_LIFECYCLE_TX_EMPTY)
        result = SYS_ERR_BUSY;
    else
        result = App_LifecycleAllocateIds(&tx->allocated_through, count, first);
    tx->api_busy = 0U;
    return result;
}

//按真实Stage结果绑定编号，READY取消也不能重新使用该编号
SysResult_t App_LifecycleTxStage(AppLifecycleTxContext_t *tx, const AppLifecycleMessage_t *request)
{
    SysResult_t result = Replay_TxEnter(tx);
    if (result != SYS_OK)
        return result;
    if (!Replay_Bound(request, tx->source, tx->target, tx->consumer_id))
        result = SYS_ERR_INVALID_ARG;
    else if (tx->pending.phase != APP_LIFECYCLE_TX_EMPTY)
        result = memcmp(&tx->pending.message, request, sizeof(*request)) == 0 ? SYS_ERR_ALREADY_EXISTS : SYS_ERR_BUSY;
    else if (request->identity.request_id <= tx->issued_through ||
        request->identity.request_id > tx->allocated_through)
        result = SYS_ERR_PROTOCOL;
    else
    {
        result = App_LifecycleStage(&tx->pending, request);
        if (result == SYS_OK)
            tx->issued_through = request->identity.request_id;
    }
    tx->api_busy = 0U;
    return result;
}

//发送回调期间持有重入门控，队列失败保留完整READY记录
SysResult_t App_LifecycleTxFlush(AppLifecycleTxContext_t *tx, AppLifecycleSendFn send, void *context)
{
    SysResult_t result = Replay_TxEnter(tx);
    if (result != SYS_OK)
        return result;
    result = App_LifecycleFlush(&tx->pending, send, context);
    tx->api_busy = 0U;
    return result;
}

//旧回复不能清掉当前请求，终结结果仍等待所属模块显式消费
SysResult_t App_LifecycleTxAcceptReply(AppLifecycleTxContext_t *tx, const AppLifecycleMessage_t *reply)
{
    SysResult_t result = Replay_TxEnter(tx);
    if (result != SYS_OK)
        return result;
    result = App_LifecycleAcceptReply(&tx->pending, reply);
    tx->api_busy = 0U;
    return result;
}

//只读终结回复，读取不会让下一个请求获得入口
SysResult_t App_LifecycleTxGetCompleted(const AppLifecycleTxContext_t *tx, AppLifecycleMessage_t *reply)
{
    if (tx == NULL || reply == NULL || Replay_Overlap(tx, sizeof(*tx), reply, sizeof(*reply)))
        return SYS_ERR_INVALID_ARG;
    if (tx->api_busy != 0U)
        return SYS_ERR_BUSY;
    if (tx->initialized != 1U || tx->pending.phase != APP_LIFECYCLE_TX_COMPLETE)
        return SYS_ERR_INVALID_STATE;
    *reply = tx->pending.reply;
    return SYS_OK;
}

//确认完整原请求，不能只凭request_id消费其他会话或方向的结果
SysResult_t App_LifecycleTxConfirmConsumed(AppLifecycleTxContext_t *tx, const AppLifecycleMessage_t *request)
{
    SysResult_t result = Replay_TxEnter(tx);
    if (result != SYS_OK)
        return result;
    if (request == NULL)
        result = SYS_ERR_INVALID_ARG;
    else if (tx->pending.phase != APP_LIFECYCLE_TX_COMPLETE)
        result = SYS_ERR_INVALID_STATE;
    else if (memcmp(request, &tx->pending.message, sizeof(*request)) != 0 ||
        !App_LifecycleMatchesReply(request, &tx->pending.reply))
        result = SYS_ERR_PROTOCOL;
    else
        result = App_LifecycleClear(&tx->pending);
    tx->api_busy = 0U;
    return result;
}

//撤销只作用于未提交请求，issued和预留上界都保持原值
SysResult_t App_LifecycleTxCancelReady(AppLifecycleTxContext_t *tx, const AppLifecycleMessage_t *request)
{
    SysResult_t result = Replay_TxEnter(tx);
    if (result != SYS_OK)
        return result;
    if (request == NULL)
        result = SYS_ERR_INVALID_ARG;
    else if (tx->pending.phase != APP_LIFECYCLE_TX_READY)
        result = SYS_ERR_INVALID_STATE;
    else if (memcmp(request, &tx->pending.message, sizeof(*request)) != 0)
        result = SYS_ERR_PROTOCOL;
    else
        result = App_LifecycleCancelReady(&tx->pending);
    tx->api_busy = 0U;
    return result;
}

//首次绑定接收窗口，普通会话切换不得重新初始化
SysResult_t App_LifecycleRxInit(AppLifecycleRxContext_t *rx, uint32_t source, uint32_t target, uint32_t consumer)
{
    if (rx == NULL || !Replay_Domain(source, target, consumer))
        return SYS_ERR_INVALID_ARG;
    if (rx->api_busy != 0U)
        return SYS_ERR_BUSY;
    if (rx->initialized != 0U)
        return SYS_ERR_ALREADY_EXISTS;
    memset(rx, 0, sizeof(*rx));
    rx->source = source;
    rx->target = target;
    rx->consumer_id = consumer;
    rx->initialized = 1U;
    return SYS_OK;
}

//只有EMPTY回复槽才准备重放，重复请求不增加待发槽位
static SysResult_t Replay_StageReply(AppLifecycleRxContext_t *rx)
{
    if (rx->reply_pending.phase != APP_LIFECYCLE_TX_EMPTY)
        return SYS_OK;
    return App_LifecycleStage(&rx->reply_pending, &rx->replay.reply);
}

//按固定窗口分类请求，所有分支都不调用业务或清理资源
SysResult_t App_LifecycleRxReceiveRequest(AppLifecycleRxContext_t *rx,
    const AppLifecycleMessage_t *request, uint32_t *disposition)
{
    uint32_t id;
    SysResult_t result;
    if (rx == NULL || disposition == NULL ||
        Replay_Overlap(rx, sizeof(*rx), disposition, sizeof(*disposition)) ||
        (request != NULL && Replay_Overlap(request, sizeof(*request), disposition, sizeof(*disposition))))
        return SYS_ERR_INVALID_ARG;
    result = Replay_RxEnter(rx);
    if (result != SYS_OK)
        return result;
    //错误的大编号不能进入任何编号和缓存更新分支
    if (!Replay_Bound(request, rx->source, rx->target, rx->consumer_id))
    {
        rx->api_busy = 0U;
        return SYS_ERR_INVALID_ARG;
    }
    id = request->identity.request_id;
    if (rx->replay.occupied != 0U && id == rx->active_request_id)
    {
        //先按数值ID识别，再核对全部原文，不能把改代次当成另一事务
        if (memcmp(request, &rx->replay.request, sizeof(*request)) != 0)
        {
            *disposition = APP_LIFECYCLE_RX_CONFLICT;
            result = SYS_ERR_PROTOCOL;
        }
        else if (rx->replay.completed == 0U)
        {
            *disposition = APP_LIFECYCLE_RX_DUPLICATE_RUNNING;
            result = SYS_OK;
        }
        else
        {
            *disposition = APP_LIFECYCLE_RX_REPLAY;
            result = Replay_StageReply(rx);
        }
    }
    else if (id <= rx->highest_retired_request_id ||
        (rx->active_request_id != 0U && id < rx->active_request_id))
    {
        //跳过的较小编号也不能重新执行，退休后不再保留历史正文
        *disposition = APP_LIFECYCLE_RX_STALE;
        result = SYS_ERR_PROTOCOL;
    }
    else if (rx->candidate_state != APP_LIFECYCLE_CANDIDATE_EMPTY)
    {
        if (memcmp(request, &rx->candidate, sizeof(*request)) == 0)
        {
            *disposition = APP_LIFECYCLE_RX_DUPLICATE_CANDIDATE;
            result = rx->candidate_state == APP_LIFECYCLE_CANDIDATE_HELD ? SYS_OK : SYS_ERR_PROTOCOL;
        }
        else
        {
            //第二个不同后继违反单未完成请求约束，原候选现场不能丢失
            rx->candidate_state = APP_LIFECYCLE_CANDIDATE_QUARANTINED;
            rx->protocol_fault = 1U;
            *disposition = APP_LIFECYCLE_RX_CONFLICT;
            result = SYS_ERR_PROTOCOL;
        }
    }
    else if (rx->protocol_fault != 0U)
    {
        *disposition = APP_LIFECYCLE_RX_QUARANTINED;
        result = SYS_ERR_PROTOCOL;
    }
    else
    {
        //先完整保存，再发布槽位状态；原Replay仍保留到安全接替
        rx->candidate = *request;
        if (rx->replay.occupied != 0U &&
            (rx->replay.completed == 0U || rx->final_reply_enqueued == 0U))
        {
            //旧回复从未提交时提前到达的请求，之后也不能自动转合法
            rx->candidate_state = APP_LIFECYCLE_CANDIDATE_QUARANTINED;
            rx->protocol_fault = 1U;
            *disposition = APP_LIFECYCLE_RX_QUARANTINED;
            result = SYS_ERR_PROTOCOL;
        }
        else
        {
            rx->candidate_state = APP_LIFECYCLE_CANDIDATE_HELD;
            *disposition = APP_LIFECYCLE_RX_CANDIDATE;
            result = SYS_OK;
        }
    }
    rx->api_busy = 0U;
    return result;
}

//在临时Replay构造成功前，不改变高水位、旧Replay或候选
SysResult_t App_LifecycleRxPromote(AppLifecycleRxContext_t *rx, AppLifecycleMessage_t *request)
{
    AppLifecycleReplay_t next;
    uint32_t next_id;
    SysResult_t result;
    if (rx == NULL || request == NULL || Replay_Overlap(rx, sizeof(*rx), request, sizeof(*request)))
        return SYS_ERR_INVALID_ARG;
    result = Replay_RxEnter(rx);
    if (result != SYS_OK)
        return result;
    if (rx->protocol_fault != 0U || rx->candidate_state == APP_LIFECYCLE_CANDIDATE_QUARANTINED)
        result = SYS_ERR_PROTOCOL;
    else if (rx->candidate_state != APP_LIFECYCLE_CANDIDATE_HELD)
        result = SYS_ERR_NOT_FOUND;
    else if (rx->reply_pending.phase != APP_LIFECYCLE_TX_EMPTY ||
        (rx->replay.occupied != 0U && (rx->replay.completed == 0U || rx->final_reply_enqueued == 0U)))
        result = SYS_ERR_BUSY;
    else if (!Replay_Bound(&rx->candidate, rx->source, rx->target, rx->consumer_id) ||
        rx->candidate.identity.request_id <= rx->highest_retired_request_id ||
        rx->candidate.identity.request_id <= rx->active_request_id ||
        ((rx->replay.occupied == 0U) != (rx->active_request_id == 0U)) ||
        (rx->active_request_id != 0U && rx->active_request_id <= rx->highest_retired_request_id))
        result = SYS_ERR_PROTOCOL;
    else
    {
        memset(&next, 0, sizeof(next));
        result = App_LifecycleReplayBegin(&next, &rx->candidate);
        if (result == SYS_OK)
        {
            next_id = rx->candidate.identity.request_id;
            //单所有者提交区内没有回调，先封闭旧编号再替换缓存
            if (rx->active_request_id != 0U)
                rx->highest_retired_request_id = rx->active_request_id;
            rx->replay = next;
            rx->active_request_id = next_id;
            rx->final_reply_enqueued = 0U;
            rx->candidate_state = APP_LIFECYCLE_CANDIDATE_EMPTY;
            memset(&rx->candidate, 0, sizeof(rx->candidate));
            *request = rx->replay.request;
        }
    }
    rx->api_busy = 0U;
    return result;
}

//终结回复写入缓存后准备运输，不改变任何业务完成条件
SysResult_t App_LifecycleRxComplete(AppLifecycleRxContext_t *rx, const AppLifecycleMessage_t *reply)
{
    SysResult_t result = Replay_RxEnter(rx);
    if (result != SYS_OK)
        return result;
    result = App_LifecycleReplayComplete(&rx->replay, reply);
    if (result == SYS_OK)
        result = Replay_StageReply(rx);
    rx->api_busy = 0U;
    return result;
}

//成功入队后锁存提交事实，当前回复槽可清理但Replay继续保留
SysResult_t App_LifecycleRxFlushReply(AppLifecycleRxContext_t *rx, AppLifecycleSendFn send, void *context)
{
    SysResult_t result = Replay_RxEnter(rx);
    if (result != SYS_OK)
        return result;
    if (rx->reply_pending.phase == APP_LIFECYCLE_TX_EMPTY)
        result = SYS_ERR_NOT_FOUND;
    else if (rx->replay.completed == 0U ||
        !App_LifecycleMatchesReply(&rx->replay.request, &rx->reply_pending.message) ||
        memcmp(&rx->replay.reply, &rx->reply_pending.message, sizeof(rx->replay.reply)) != 0)
        result = SYS_ERR_PROTOCOL;
    else
    {
        result = App_LifecycleFlush(&rx->reply_pending, send, context);
        if (result == SYS_OK)
        {
            rx->final_reply_enqueued = 1U;
            result = App_LifecycleClear(&rx->reply_pending);
        }
    }
    rx->api_busy = 0U;
    return result;
}

//只读当前管理进展，重入时拒绝观察提交中间状态
SysResult_t App_LifecycleRxGetStatus(const AppLifecycleRxContext_t *rx, AppLifecycleReplayStatus_t *status)
{
    AppLifecycleReplayStatus_t value;
    if (rx == NULL || status == NULL || Replay_Overlap(rx, sizeof(*rx), status, sizeof(*status)))
        return SYS_ERR_INVALID_ARG;
    if (rx->api_busy != 0U)
        return SYS_ERR_BUSY;
    if (rx->initialized != 1U)
        return SYS_ERR_INVALID_STATE;
    value.highest_retired_request_id = rx->highest_retired_request_id;
    value.active_request_id = rx->active_request_id;
    value.occupied = rx->replay.occupied;
    value.completed = rx->replay.completed;
    value.final_reply_enqueued = rx->final_reply_enqueued;
    value.candidate_state = rx->candidate_state;
    value.protocol_fault = rx->protocol_fault;
    *status = value;
    return SYS_OK;
}
