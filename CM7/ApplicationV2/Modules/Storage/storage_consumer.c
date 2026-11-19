// 在 Storage 所有者上下文推进 Camera 资格事务。

#include "storage_consumer.h"
#include "app_camera_generation.h"
#include <string.h>
#define MAGIC 0x53544336U
// 校验所有者调用并设置重入保护，调用方仍负责跨任务串行化。
static SysResult_t enter(VStorageConsumer *consumer)
{
    if (!consumer || consumer->magic != MAGIC || !consumer->ops->is_owner(consumer->port))
        return SYS_ERR_INVALID_STATE;
    if (consumer->busy)
        return SYS_ERR_BUSY;
    consumer->busy = 1;
    return 0;
}
// 退出当前调用的重入保护。
static SysResult_t leave(VStorageConsumer *consumer, SysResult_t result)
{
    consumer->busy = 0;
    return result;
}
// Storage：绑定资格事务的消费者身份和所有者。
SysResult_t VStorageConsumer_Init(VStorageConsumer *consumer, const VStorageConsumerOps *ops, void *port_context)
{
    SysResult_t result;
    if (!consumer || consumer->magic || !ops || !ops->is_owner || !ops->send || !ops->receive_reply)
        return SYS_ERR_INVALID_ARG;
    if (!ops->is_owner(port_context))
        return SYS_ERR_INVALID_STATE;
    memset(consumer, 0, sizeof(*consumer));
    consumer->ops = ops;
    consumer->port = port_context;
    //复用生命周期 Tx 的身份及退休规则。
    result = App_LifecycleTxInit(&consumer->tx, APP_LIFECYCLE_STORAGE, APP_LIFECYCLE_CAMERA,
                            APP_CONSUMER_STORAGE);
    if (result)
        return result;
    consumer->magic = MAGIC;
    return 0;
}
// Storage：启动原资格申请流程，授权以匹配回复为准。
SysResult_t VStorageConsumer_Begin(VStorageConsumer *consumer)
{
    SysResult_t result = enter(consumer);
    if (result)
        return result;
    if (consumer->phase != VSC_IDLE || consumer->grant.session)
        return leave(consumer, SYS_ERR_BUSY);
    //新一轮启用分配新代次，耗尽时拒绝而不回绕。
    result = App_QualificationNextGeneration(&consumer->generation_counter, &consumer->generation);
    if (result)
        return leave(consumer, result);
    consumer->phase = VSC_QUERY;
    consumer->reserved = consumer->staged = 0;
    consumer->result = 0;
    return leave(consumer, 0);
}
// Storage：提交独立资格释放事务，等待真实释放确认。
SysResult_t VStorageConsumer_End(VStorageConsumer *consumer, SysResult_t error, SysResult_t cleanup,
                                 uint32_t flags)
{
    SysResult_t result = enter(consumer);
    const uint32_t required =
        APP_LIFECYCLE_INPUT_CLOSED | APP_LIFECYCLE_WORK_QUIESCENT | APP_LIFECYCLE_FRAMES_DRAINED;
    if (result)
        return result;
    if (error > SYS_OK || cleanup > SYS_OK || (flags & required) != required ||
        (flags & ~APP_LIFECYCLE_CONSUMER_FLAGS))
        return leave(consumer, SYS_ERR_INVALID_ARG);
    if (consumer->phase == VSC_RELEASE || consumer->phase == VSC_IDLE_REQUEST)
        return leave(consumer, consumer->business_error == error && consumer->cleanup_error == cleanup &&
                                consumer->release_flags == flags
                            ? SYS_OK
                            : SYS_ERR_PROTOCOL);
    if (consumer->phase != VSC_GRANTED || !consumer->grant.session)
        return leave(consumer, SYS_ERR_INVALID_STATE);
    consumer->business_error = error;
    consumer->cleanup_error = cleanup;
    consumer->release_flags = flags;
    consumer->phase = VSC_RELEASE;
    consumer->reserved = consumer->staged = 0;
    consumer->result = 0;
    return leave(consumer, 0);
}
// 按消费者当前阶段登记原资格事务，等待原 Tx/Rx 推进。
static SysResult_t stage(VStorageConsumer *consumer)
{
    SysResult_t result;
    uint32_t id;
    if (!consumer->reserved)
    {
        result = App_LifecycleTxReserveIds(&consumer->tx, 1, &id);
        if (result)
            return result;
        memset(&consumer->request, 0, sizeof(consumer->request));
        // 新事务只在准备阶段构造一次。重试保留原请求、会话和代次。
        consumer->request.version = APP_LIFECYCLE_VERSION;
        consumer->request.kind = APP_LIFECYCLE_REQUEST;
        consumer->request.source = APP_LIFECYCLE_STORAGE;
        consumer->request.target = APP_LIFECYCLE_CAMERA;
        consumer->request.identity.consumer_id = APP_CONSUMER_STORAGE;
        consumer->request.identity.consumer_generation = consumer->generation;
        consumer->request.identity.request_id = id;
        consumer->request.operation = consumer->phase == VSC_QUERY     ? APP_LIFECYCLE_SESSION_QUERY
                               : consumer->phase == VSC_ACQUIRE ? APP_LIFECYCLE_ACQUIRE
                               : consumer->phase == VSC_RELEASE ? APP_LIFECYCLE_RELEASE
                                                         : APP_LIFECYCLE_STOP_IF_IDLE;
        consumer->request.identity.session_id = consumer->phase == VSC_QUERY ? 0 : consumer->session;
        if (consumer->phase == VSC_RELEASE)
        {
            consumer->release.grant = consumer->grant;
            consumer->release.request = id;
            consumer->request.identity.session_id = consumer->grant.session;
            consumer->request.identity.consumer_generation = consumer->grant.generation;
            consumer->request.completion_flags = consumer->release_flags;
            consumer->request.cleanup_result = consumer->cleanup_error;
            consumer->request.business_result = consumer->business_error;
        }
        consumer->reserved = 1;
    }
    //只准备原请求，真正入队由 Flush 决定。
    result = App_LifecycleTxStage(&consumer->tx, &consumer->request);
    if (!result)
        consumer->staged = 1;
    return result;
}
// Storage：推进资格事务和匹配回复，不绕过原 Tx/Rx。
SysResult_t VStorageConsumer_Process(VStorageConsumer *consumer)
{
    SysResult_t result = enter(consumer);
    AppLifecycleMessage_t reply;
    if (result)
        return result;
    if (consumer->phase == VSC_IDLE || consumer->phase == VSC_GRANTED)
        return leave(consumer, 0);
    if (consumer->phase == VSC_BLOCKED)
        return leave(consumer, consumer->result ? consumer->result : SYS_ERR_INVALID_STATE);
    if (!consumer->staged)
    {
        result = stage(consumer);
        if (result)
        {
            consumer->result = result;
            consumer->phase = VSC_BLOCKED;
            return leave(consumer, result);
        }
    }
    //WAIT_REPLY超时不能清空Tx；队列满只重试原请求原编号。
    //队列背压时继续发送已登记的原请求。
    (void)App_LifecycleTxFlush(&consumer->tx, consumer->ops->send, consumer->port);
    result = consumer->ops->receive_reply(consumer->port, &reply);
    if (!result && App_LifecycleMatchesReply(&consumer->request, &reply))
        //让 Tx 核对完整回复身份，迟到回复不能推进阶段。
        (void)App_LifecycleTxAcceptReply(&consumer->tx, &reply);
    result = App_LifecycleTxGetCompleted(&consumer->tx, &reply);
    if (result)
        return leave(consumer, SYS_ERR_BUSY);
    //按原请求确认消费，然后才能准备下一事务。
    result = App_LifecycleTxConfirmConsumed(&consumer->tx, &consumer->request);
    if (result)
        return leave(consumer, result);
    consumer->result = reply.result;
    consumer->reserved = consumer->staged = 0;
    if (consumer->phase == VSC_QUERY)
    {
        if (!reply.result)
        {
            consumer->session = reply.identity.session_id;
            consumer->phase = VSC_ACQUIRE;
        }
        else
            consumer->phase = VSC_IDLE;
    }
    else if (consumer->phase == VSC_ACQUIRE)
    {
        if (!reply.result)
        {
            consumer->grant.session = consumer->request.identity.session_id;
            consumer->grant.consumer = APP_CONSUMER_STORAGE;
            consumer->grant.generation = consumer->request.identity.consumer_generation;
            consumer->grant.acquire = consumer->request.identity.request_id;
            consumer->phase = VSC_GRANTED;
        }
        else
            consumer->phase = VSC_IDLE;
    }
    else if (consumer->phase == VSC_RELEASE)
    {
        if (!reply.result && (reply.completion_flags & APP_LIFECYCLE_QUALIFICATION_RELEASED))
        {
            memset(&consumer->grant, 0, sizeof(consumer->grant));
            consumer->phase = VSC_IDLE_REQUEST;
        }
        else
        {
            if (!consumer->result)
                consumer->result = SYS_ERR_PROTOCOL;
            consumer->phase = VSC_BLOCKED;
        }
    }
    else
    {
        consumer->camera_stop_result = reply.result;
        consumer->result = 0;
        consumer->phase = VSC_IDLE;
    }
    return leave(consumer, consumer->result);
}
