#include "app_camera_lifecycle.h"
#include <string.h>

//检查角色是否属于本阶段的Camera或两个消费者
static bool Lifecycle_Role(uint32_t role);

//比较请求键，不混入结果和完成阶段
static bool Lifecycle_SameKey(const AppLifecycleMessage_t *a, const AppLifecycleMessage_t *b);

//检查角色是否属于本阶段的Camera或两个消费者
static bool Lifecycle_Role(uint32_t role)
{
    return role == APP_LIFECYCLE_CAMERA || role == APP_LIFECYCLE_USB || role == APP_LIFECYCLE_STORAGE;
}

//校验消息布局和声明方向，不把完成标志当成硬件证明
bool App_LifecycleValidate(const AppLifecycleMessage_t *message)
{
    uint32_t consumer_role, request_source, request_target, flags;
    bool camera_request;

    //未知版本、种类或操作不能进入已知协议处理
    if (message == NULL || message->version != APP_LIFECYCLE_VERSION ||
        (message->kind != APP_LIFECYCLE_REQUEST && message->kind != APP_LIFECYCLE_REPLY) ||
        message->operation < APP_LIFECYCLE_SESSION_QUERY || message->operation > APP_LIFECYCLE_FAULT_NOTICE)
        return false;

    //固定消费者身份必须与通信角色一致，零请求和零代次不能匹配事务
    if (!Lifecycle_Role(message->source) || !Lifecycle_Role(message->target) ||
        message->identity.request_id == 0U || message->identity.consumer_generation == 0U)
        return false;
    if (message->identity.consumer_id == APP_CONSUMER_USB)
        consumer_role = APP_LIFECYCLE_USB;
    else if (message->identity.consumer_id == APP_CONSUMER_STORAGE)
        consumer_role = APP_LIFECYCLE_STORAGE;
    else
        return false;

    //主动释放要求和故障通知由Camera发起，其余操作由消费者发起
    camera_request = message->operation == APP_LIFECYCLE_RELEASE_REQUEST ||
        message->operation == APP_LIFECYCLE_FAULT_NOTICE;
    request_source = camera_request ? APP_LIFECYCLE_CAMERA : consumer_role;
    request_target = camera_request ? consumer_role : APP_LIFECYCLE_CAMERA;
    if (message->kind == APP_LIFECYCLE_REPLY)
    {
        uint32_t swap = request_source;
        request_source = request_target;
        request_target = swap;
    }
    if (message->source != request_source || message->target != request_target)
        return false;

    //只有查询请求和查询失败回复可没有资源会话
    if (message->identity.session_id == 0U &&
        !(message->operation == APP_LIFECYCLE_SESSION_QUERY &&
        (message->kind == APP_LIFECYCLE_REQUEST || message->result != SYS_OK)))
        return false;
    if (message->operation == APP_LIFECYCLE_SESSION_QUERY &&
        message->kind == APP_LIFECYCLE_REQUEST && message->identity.session_id != 0U)
        return false;

    //不接受正数伪成功码，关联编号只用于消费者释放事务
    if (message->result > SYS_OK || message->business_result > SYS_OK || message->cleanup_result > SYS_OK ||
        (message->related_request_id != 0U && message->operation != APP_LIFECYCLE_RELEASE))
        return false;
    if (message->kind == APP_LIFECYCLE_REQUEST)
    {
        if (message->operation == APP_LIFECYCLE_FAULT_NOTICE)
        {
            if (message->result == SYS_OK)
                return false;
        }
        else if (message->result != SYS_OK)
            return false;
        if (message->operation != APP_LIFECYCLE_RELEASE && message->operation != APP_LIFECYCLE_FAULT_NOTICE &&
            (message->business_result != SYS_OK || message->cleanup_result != SYS_OK))
            return false;
    }

    //未知标志必须拒绝，消费者不能宣称资格或Camera硬件已释放
    flags = message->completion_flags;
    if ((flags & ~APP_LIFECYCLE_ALL_FLAGS) != 0U)
        return false;
    if ((flags & APP_LIFECYCLE_QUALIFICATION_RELEASED) != 0U &&
        !(message->source == APP_LIFECYCLE_CAMERA && message->kind == APP_LIFECYCLE_REPLY &&
        message->operation == APP_LIFECYCLE_RELEASE && message->result == SYS_OK))
        return false;
    if ((flags & APP_LIFECYCLE_HARDWARE_STOPPED) != 0U &&
        !(message->source == APP_LIFECYCLE_CAMERA && message->kind == APP_LIFECYCLE_REPLY &&
        message->operation == APP_LIFECYCLE_STOP_IF_IDLE && message->result == SYS_OK))
        return false;
    if ((flags & APP_LIFECYCLE_CONSUMER_FLAGS) != 0U &&
        !(message->operation == APP_LIFECYCLE_RELEASE ||
        (message->operation == APP_LIFECYCLE_RELEASE_REQUEST && message->kind == APP_LIFECYCLE_REPLY)))
        return false;
    return true;
}

//比较同方向请求键，重复内容另行逐字节核对
static bool Lifecycle_SameKey(const AppLifecycleMessage_t *a, const AppLifecycleMessage_t *b)
{
    return a->version == b->version && a->kind == b->kind && a->operation == b->operation &&
        a->source == b->source && a->target == b->target &&
        a->identity.session_id == b->identity.session_id &&
        a->identity.request_id == b->identity.request_id &&
        a->identity.consumer_id == b->identity.consumer_id &&
        a->identity.consumer_generation == b->identity.consumer_generation &&
        a->related_request_id == b->related_request_id;
}

//回复只匹配原发起事务，查询成功可以返回新取得的会话
bool App_LifecycleMatchesReply(const AppLifecycleMessage_t *request, const AppLifecycleMessage_t *reply)
{
    if (!App_LifecycleValidate(request) || !App_LifecycleValidate(reply) ||
        request->kind != APP_LIFECYCLE_REQUEST || reply->kind != APP_LIFECYCLE_REPLY)
        return false;

    //来源、操作和三项事务身份全部匹配，不能仅比较request_id
    if (request->operation != reply->operation || request->source != reply->target ||
        request->target != reply->source || request->identity.request_id != reply->identity.request_id ||
        request->identity.consumer_id != reply->identity.consumer_id ||
        request->identity.consumer_generation != reply->identity.consumer_generation ||
        request->related_request_id != reply->related_request_id)
        return false;
    if (request->operation == APP_LIFECYCLE_SESSION_QUERY && reply->result == SYS_OK)
        return reply->identity.session_id != 0U;
    return request->identity.session_id == reply->identity.session_id;
}

//只检查当前身份，不递增会话、代次或修改资格
bool App_LifecycleMatchesCurrent(const AppLifecycleMessage_t *message, uint32_t session,
    uint32_t consumer, uint32_t generation)
{
    return session != 0U && generation != 0U && App_LifecycleValidate(message) &&
        message->identity.session_id == session && message->identity.consumer_id == consumer &&
        message->identity.consumer_generation == generation;
}

//在完整临时消息通过校验后才写出回复
SysResult_t App_LifecycleMakeReply(const AppLifecycleMessage_t *request, uint32_t session,
    SysResult_t result, AppLifecycleMessage_t *reply)
{
    AppLifecycleMessage_t value;
    if (reply == NULL || !App_LifecycleValidate(request) || request->kind != APP_LIFECYCLE_REQUEST || result > SYS_OK)
        return SYS_ERR_INVALID_ARG;
    value = *request;
    value.kind = APP_LIFECYCLE_REPLY;
    value.source = request->target;
    value.target = request->source;
    value.identity.session_id = session;
    value.result = result;
    value.completion_flags = 0U;
    if (!App_LifecycleMatchesReply(request, &value))
        return SYS_ERR_INVALID_ARG;
    *reply = value;
    return SYS_OK;
}

//唯一所有者分配一段编号，失败不改变计数或输出
SysResult_t App_LifecycleAllocateIds(uint32_t *sequence, uint32_t count, uint32_t *first)
{
    if (sequence == NULL || first == NULL || sequence == first || count == 0U)
        return SYS_ERR_INVALID_ARG;
    if (count > UINT32_MAX - *sequence)
        return SYS_ERR_NO_RESOURCE;
    *first = *sequence + 1U;
    *sequence += count;
    return SYS_OK;
}

//保留完整原文，重复登记不覆盖在途记录
SysResult_t App_LifecycleStage(AppLifecyclePending_t *pending, const AppLifecycleMessage_t *message)
{
    if (pending == NULL || !App_LifecycleValidate(message))
        return SYS_ERR_INVALID_ARG;
    if (pending->phase != APP_LIFECYCLE_TX_EMPTY)
        return memcmp(&pending->message, message, sizeof(*message)) == 0 ? SYS_ERR_ALREADY_EXISTS : SYS_ERR_BUSY;
    pending->message = *message;
    pending->phase = APP_LIFECYCLE_TX_READY;
    return SYS_OK;
}

//成功入队后不重复提交，失败保持原身份供下一轮重试
SysResult_t App_LifecycleFlush(AppLifecyclePending_t *pending, AppLifecycleSendFn send, void *context)
{
    SysResult_t result;
    if (pending == NULL || send == NULL)
        return SYS_ERR_INVALID_ARG;
    if (pending->phase != APP_LIFECYCLE_TX_READY)
        return pending->phase == APP_LIFECYCLE_TX_EMPTY ? SYS_ERR_INVALID_STATE : SYS_ERR_BUSY;

    //回调期间阻止同一记录重入；这里只处理消息，不操作资源
    pending->phase = APP_LIFECYCLE_TX_SENDING;
    result = send(context, &pending->message);
    if (result != SYS_OK)
    {
        pending->phase = APP_LIFECYCLE_TX_READY;
        return result;
    }
    pending->phase = pending->message.kind == APP_LIFECYCLE_REQUEST ?
        APP_LIFECYCLE_TX_WAIT_REPLY : APP_LIFECYCLE_TX_COMMITTED;
    return SYS_OK;
}

//不匹配的迟到回复不会终结当前请求
SysResult_t App_LifecycleAcceptReply(AppLifecyclePending_t *pending, const AppLifecycleMessage_t *reply)
{
    if (pending == NULL || reply == NULL)
        return SYS_ERR_INVALID_ARG;
    if (pending->phase != APP_LIFECYCLE_TX_WAIT_REPLY)
        return SYS_ERR_INVALID_STATE;
    if (!App_LifecycleMatchesReply(&pending->message, reply))
        return SYS_ERR_PROTOCOL;
    pending->reply = *reply;
    pending->phase = APP_LIFECYCLE_TX_COMPLETE;
    return SYS_OK;
}

//仅退休已提交回复或已收到回复的请求，不提供超时强清入口
SysResult_t App_LifecycleClear(AppLifecyclePending_t *pending)
{
    if (pending == NULL)
        return SYS_ERR_INVALID_ARG;
    if (pending->phase != APP_LIFECYCLE_TX_COMMITTED && pending->phase != APP_LIFECYCLE_TX_COMPLETE)
        return SYS_ERR_BUSY;
    memset(pending, 0, sizeof(*pending));
    return SYS_OK;
}

//仅取消尚未提交的请求，不清理已入队或正在发送的事务
SysResult_t App_LifecycleCancelReady(AppLifecyclePending_t *pending)
{
    if (pending == NULL)
        return SYS_ERR_INVALID_ARG;

    //回复仍由缓存重发，不能通过请求取消接口丢弃ACK
    if (pending->phase != APP_LIFECYCLE_TX_READY ||
        pending->message.kind != APP_LIFECYCLE_REQUEST)
        return SYS_ERR_INVALID_STATE;

    //记录仅包含协议数值，清空不触及业务或帧资源
    memset(pending, 0, sizeof(*pending));
    return SYS_OK;
}

//相同请求只报告重复，业务处理者不能据此重复执行
SysResult_t App_LifecycleReplayBegin(AppLifecycleReplay_t *record, const AppLifecycleMessage_t *request)
{
    if (record == NULL || !App_LifecycleValidate(request) || request->kind != APP_LIFECYCLE_REQUEST)
        return SYS_ERR_INVALID_ARG;
    if (record->occupied != 0U)
    {
        if (!Lifecycle_SameKey(&record->request, request))
            return SYS_ERR_BUSY;
        return memcmp(&record->request, request, sizeof(*request)) == 0 ?
            SYS_ERR_ALREADY_EXISTS : SYS_ERR_PROTOCOL;
    }
    record->request = *request;
    record->occupied = 1U;
    record->completed = 0U;
    return SYS_OK;
}

//重复完成必须保持原回复，不能用后来的成功覆盖故障
SysResult_t App_LifecycleReplayComplete(AppLifecycleReplay_t *record, const AppLifecycleMessage_t *reply)
{
    if (record == NULL || reply == NULL)
        return SYS_ERR_INVALID_ARG;
    if (record->occupied == 0U || !App_LifecycleMatchesReply(&record->request, reply))
        return SYS_ERR_PROTOCOL;
    if (record->completed != 0U)
        return memcmp(&record->reply, reply, sizeof(*reply)) == 0 ? SYS_ERR_ALREADY_EXISTS : SYS_ERR_PROTOCOL;
    record->reply = *reply;
    record->completed = 1U;
    return SYS_OK;
}

//读取终结回复，不消费缓存或更改业务状态
SysResult_t App_LifecycleReplayGetReply(const AppLifecycleReplay_t *record, AppLifecycleMessage_t *reply)
{
    if (record == NULL || reply == NULL)
        return SYS_ERR_INVALID_ARG;
    if (record->occupied == 0U || record->completed == 0U)
        return SYS_ERR_NOT_FOUND;
    *reply = record->reply;
    return SYS_OK;
}

//缓存退休时机由未来业务协议决定，不接受执行中的请求清理
SysResult_t App_LifecycleReplayRetire(AppLifecycleReplay_t *record)
{
    if (record == NULL)
        return SYS_ERR_INVALID_ARG;
    if (record->occupied == 0U || record->completed == 0U)
        return SYS_ERR_BUSY;
    //没有可验证的旧请求失效边界，完成或ACK入队都不能授权退休
    return SYS_ERR_NOT_SUPPORTED;
}
