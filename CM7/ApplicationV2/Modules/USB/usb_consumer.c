// 在 USB 所有者上下文推进 Camera 资格事务。

#include "usb_consumer.h"
#include "app_camera_qualification.h"
#include <string.h>
#define MAGIC 0x55514335U
// 校验所有者调用并设置重入保护，调用方仍负责跨任务串行化。
static SysResult_t enter(VUSBConsumer *consumer)
{
    if (!consumer || consumer->magic != MAGIC || !consumer->ops->is_owner(consumer->port))
        return SYS_ERR_INVALID_STATE;
    if (consumer->busy)
        return SYS_ERR_BUSY;
    consumer->busy = 1;
    return 0;
}
// 退出当前调用的重入保护。
static SysResult_t leave(VUSBConsumer *consumer, SysResult_t result)
{
    consumer->busy = 0;
    return result;
}
// 将本轮观察到的故障记录到模块上下文。
static void fault(VUSBConsumer *consumer, SysResult_t result)
{
    if (result && !consumer->first_error)
        consumer->first_error = result;
}
// USB：绑定资格事务的消费者身份和所有者。
SysResult_t VUSBConsumer_Init(VUSBConsumer *consumer, const VUSBConsumerOps *ops, void *port_context)
{
    SysResult_t result;
    if (!consumer || consumer->magic || !ops || !ops->is_owner || !ops->send || !ops->receive_reply)
        return SYS_ERR_INVALID_ARG;
    if (!ops->is_owner(port_context))
        return SYS_ERR_INVALID_STATE;
    memset(consumer, 0, sizeof(*consumer));
    consumer->ops = ops;
    consumer->port = port_context;
    result = App_LifecycleTxInit(&consumer->tx, APP_LIFECYCLE_USB, APP_LIFECYCLE_CAMERA, APP_CONSUMER_USB);
    if (result)
        return result;
    consumer->magic = MAGIC;
    return 0;
}
// USB：启动原资格申请流程，授权以匹配回复为准。
SysResult_t VUSBConsumer_Begin(VUSBConsumer *consumer)
{
    SysResult_t result = enter(consumer);
    if (result)
        return result;
    if (consumer->phase != VUC_IDLE || consumer->grant.session)
        return leave(consumer, SYS_ERR_BUSY);
    result = App_QualificationNextGeneration(&consumer->generation_counter, &consumer->generation);
    if (result)
        return leave(consumer, result);
    consumer->phase = VUC_QUERY;
    consumer->reserved = consumer->staged = 0;
    consumer->result = 0;
    consumer->first_error = 0;
    return leave(consumer, 0);
}
// USB：提交独立资格释放事务，等待真实释放确认。
SysResult_t VUSBConsumer_End(VUSBConsumer *consumer, SysResult_t error)
{
    SysResult_t result = enter(consumer);
    if (result)
        return result;
    if (consumer->phase == VUC_RELEASE || consumer->phase == VUC_IDLE_REQUEST)
        return leave(consumer, 0);
    if (consumer->phase != VUC_GRANTED || !consumer->grant.session)
        return leave(consumer, SYS_ERR_INVALID_STATE);
    consumer->business_error = error;
    consumer->phase = VUC_RELEASE;
    consumer->reserved = consumer->staged = 0;
    consumer->result = 0;
    return leave(consumer, 0);
}
// 按消费者当前阶段登记原资格事务，等待原 Tx/Rx 推进。
static SysResult_t stage(VUSBConsumer *consumer)
{
    SysResult_t result;
    uint32_t id;
    if (!consumer->reserved)
    {
        result = App_LifecycleTxReserveIds(&consumer->tx, 1, &id);
        if (result)
            return result;
        memset(&consumer->request, 0, sizeof(consumer->request));
        consumer->request.version = APP_LIFECYCLE_VERSION;
        consumer->request.kind = APP_LIFECYCLE_REQUEST;
        consumer->request.source = APP_LIFECYCLE_USB;
        consumer->request.target = APP_LIFECYCLE_CAMERA;
        consumer->request.identity.consumer_id = APP_CONSUMER_USB;
        consumer->request.identity.consumer_generation = consumer->generation;
        consumer->request.identity.request_id = id;
        consumer->request.operation = consumer->phase == VUC_QUERY     ? APP_LIFECYCLE_SESSION_QUERY
                               : consumer->phase == VUC_ACQUIRE ? APP_LIFECYCLE_ACQUIRE
                               : consumer->phase == VUC_RELEASE ? APP_LIFECYCLE_RELEASE
                                                         : APP_LIFECYCLE_STOP_IF_IDLE;
        consumer->request.identity.session_id = consumer->phase == VUC_QUERY ? 0 : consumer->session;
        if (consumer->phase == VUC_RELEASE)
        {
            consumer->release.grant = consumer->grant;
            consumer->release.request = id;
            consumer->request.identity.session_id = consumer->grant.session;
            consumer->request.identity.consumer_generation = consumer->grant.generation;
            consumer->request.completion_flags = APP_LIFECYCLE_INPUT_CLOSED |
                                          APP_LIFECYCLE_WORK_QUIESCENT |
                                          APP_LIFECYCLE_FRAMES_DRAINED;
            consumer->request.business_result = consumer->business_error;
        }
        consumer->reserved = 1;
    }
    result = App_LifecycleTxStage(&consumer->tx, &consumer->request);
    if (!result)
        consumer->staged = 1;
    return result;
}
// USB：推进资格事务和匹配回复，不绕过原 Tx/Rx。
SysResult_t VUSBConsumer_Process(VUSBConsumer *consumer)
{
    SysResult_t result = enter(consumer);
    AppLifecycleMessage_t reply;
    if (result)
        return result;
    if (consumer->phase == VUC_IDLE || consumer->phase == VUC_GRANTED)
        return leave(consumer, 0);
    if (consumer->phase == VUC_BLOCKED)
        return leave(consumer, consumer->result ? consumer->result : SYS_ERR_INVALID_STATE);
    if (!consumer->staged)
    {
        result = stage(consumer);
        if (result)
        {
            fault(consumer, result);
            return leave(consumer, result);
        }
    }
    /* WAIT_REPLY超时不能清空Tx；队列满只重试原请求原编号。 */
    (void)App_LifecycleTxFlush(&consumer->tx, consumer->ops->send, consumer->port);
    result = consumer->ops->receive_reply(consumer->port, &reply);
    if (!result && App_LifecycleMatchesReply(&consumer->request, &reply))
    {
        result = App_LifecycleTxAcceptReply(&consumer->tx, &reply);
        if (result)
            fault(consumer, result);
    }
    result = App_LifecycleTxGetCompleted(&consumer->tx, &reply);
    if (result)
        return leave(consumer, result);
    result = App_LifecycleTxConfirmConsumed(&consumer->tx, &consumer->request);
    if (result)
    {
        fault(consumer, result);
        return leave(consumer, result);
    }
    consumer->result = reply.result;
    consumer->reserved = consumer->staged = 0;
    if (consumer->phase == VUC_QUERY)
    {
        if (!reply.result)
        {
            consumer->session = reply.identity.session_id;
            consumer->phase = VUC_ACQUIRE;
        }
        else
            consumer->phase = VUC_IDLE;
    }
    else if (consumer->phase == VUC_ACQUIRE)
    {
        if (!reply.result)
        {
            consumer->grant.session = consumer->request.identity.session_id;
            consumer->grant.consumer = APP_CONSUMER_USB;
            consumer->grant.generation = consumer->request.identity.consumer_generation;
            consumer->grant.acquire = consumer->request.identity.request_id;
            consumer->phase = VUC_GRANTED;
        }
        else
            consumer->phase = VUC_IDLE;
    }
    else if (consumer->phase == VUC_RELEASE)
    {
        if (!reply.result && (reply.completion_flags & APP_LIFECYCLE_QUALIFICATION_RELEASED))
        {
            memset(&consumer->grant, 0, sizeof(consumer->grant));
            consumer->phase = VUC_IDLE_REQUEST;
        }
        else
        {
            if (!consumer->result)
                consumer->result = SYS_ERR_PROTOCOL;
            consumer->phase = VUC_BLOCKED;
        }
    }
    else
    {
        consumer->camera_stop_result = reply.result;
        consumer->result = 0;
        consumer->phase = VUC_IDLE;
    }
    if (consumer->result)
        fault(consumer, consumer->result);
    return leave(consumer, consumer->result);
}
