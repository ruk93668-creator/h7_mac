#include "app_camera_consumer.h"
#include <string.h>

//建立原Tx请求，不直接修改Pending或issued_through
static SysResult_t Consumer_Stage(AppCameraConsumer_t *consumer, uint32_t operation,
    uint32_t request_id, SysResult_t business, SysResult_t cleanup);

//首次静态绑定本消费者的单方向Tx
SysResult_t App_CameraConsumerInit(AppCameraConsumer_t *c, uint32_t role, uint32_t id)
{
    if (c == NULL)
        return SYS_ERR_INVALID_ARG;
    return App_LifecycleTxInit(&c->tx, role, APP_LIFECYCLE_CAMERA, id);
}

//新的启动尝试消耗代次和编号，授权未释放时拒绝重复启动
SysResult_t App_CameraConsumerBegin(AppCameraConsumer_t *c)
{
    SysResult_t result;
    if (c == NULL)
        return SYS_ERR_INVALID_ARG;
    if ((c->phase != APP_CONSUMER_IDLE && c->phase != APP_CONSUMER_FAILED) || c->granted != 0U)
        return SYS_ERR_BUSY;
    result = App_QualificationNextGeneration(&c->next_generation, &c->generation);
    if (result != SYS_OK)
        return result;
    result = App_LifecycleTxReserveIds(&c->tx, 4U, &c->first_request);
    if (result != SYS_OK)
        return result;
    c->session = 0U;
    result = Consumer_Stage(c, APP_LIFECYCLE_SESSION_QUERY, c->first_request, SYS_OK, SYS_OK);
    if (result == SYS_OK)
    {
        c->phase = APP_CONSUMER_QUERY;
        c->result = SYS_ERR_BUSY;
    }
    return result;
}

//业务关闭只触发RELEASE，不声明FRAMES_DRAINED或资格已经撤销
SysResult_t App_CameraConsumerEnd(AppCameraConsumer_t *c, SysResult_t business, SysResult_t cleanup)
{
    SysResult_t result;
    if (c == NULL)
        return SYS_ERR_INVALID_ARG;
    if (c->phase != APP_CONSUMER_GRANTED || c->granted == 0U)
        return SYS_ERR_BUSY;
    result = Consumer_Stage(c, APP_LIFECYCLE_RELEASE, c->first_request + 2U, business, cleanup);
    if (result == SYS_OK)
        c->phase = APP_CONSUMER_RELEASE;
    return result;
}

//只接受原协议的完整身份回复，不接管反方向Shutdown请求
SysResult_t App_CameraConsumerReceive(AppCameraConsumer_t *c, const AppLifecycleMessage_t *message)
{
    if (c == NULL || message == NULL)
        return SYS_ERR_INVALID_ARG;
    c->receive_result = App_LifecycleTxAcceptReply(&c->tx, message);
    return c->receive_result;
}

//每轮最多消费一份回复，未收到回复时保留原Tx且绝不超时强清
SysResult_t App_CameraConsumerProcess(AppCameraConsumer_t *c)
{
    AppLifecycleMessage_t reply;
    SysResult_t result;
    if (c == NULL)
        return SYS_ERR_INVALID_ARG;
    if (c->phase == APP_CONSUMER_IDLE || c->phase == APP_CONSUMER_GRANTED || c->phase == APP_CONSUMER_FAILED)
        return SYS_OK;
    result = App_LifecycleTxGetCompleted(&c->tx, &reply);
    if (result != SYS_OK)
        return result;

    //先验证授权完成声明，再确认原请求身份被消费
    if (reply.result == SYS_OK && c->phase == APP_CONSUMER_RELEASE &&
        (reply.completion_flags & APP_LIFECYCLE_QUALIFICATION_RELEASED) == 0U)
    {
        c->result = SYS_ERR_PROTOCOL;
        return SYS_ERR_PROTOCOL;
    }
    result = App_LifecycleTxConfirmConsumed(&c->tx, &c->request);
    if (result != SYS_OK)
    {
        c->result = result;
        return result;
    }
    c->result = reply.result;
    if (reply.result != SYS_OK)
    {
        //其他消费者仍活动导致STOP_IF_IDLE BUSY，不等于本消费者释放失败
        c->phase = c->phase == APP_CONSUMER_STOP_IDLE ? APP_CONSUMER_IDLE : APP_CONSUMER_FAILED;
        return reply.result;
    }
    if (c->phase == APP_CONSUMER_QUERY)
    {
        c->session = reply.identity.session_id;
        result = Consumer_Stage(c, APP_LIFECYCLE_ACQUIRE, c->first_request + 1U, SYS_OK, SYS_OK);
        if (result == SYS_OK)
            c->phase = APP_CONSUMER_ACQUIRE;
    }
    else if (c->phase == APP_CONSUMER_ACQUIRE)
    {
        c->granted = 1U;
        c->phase = APP_CONSUMER_GRANTED;
        result = SYS_OK;
    }
    else if (c->phase == APP_CONSUMER_RELEASE)
    {
        //只有Camera的真实释放回复才能清除授权，随后才允许STOP_IF_IDLE
        c->granted = 0U;
        result = Consumer_Stage(c, APP_LIFECYCLE_STOP_IF_IDLE, c->first_request + 3U, SYS_OK, SYS_OK);
        if (result == SYS_OK)
            c->phase = APP_CONSUMER_STOP_IDLE;
    }
    else
    {
        c->phase = APP_CONSUMER_IDLE;
        result = SYS_OK;
    }
    if (result != SYS_OK)
    {
        //后继绑定异常不是业务重试，不重新执行已经确认的事务
        c->phase = APP_CONSUMER_FAILED;
        c->result = result;
    }
    return result;
}

//运输仍使用原Flush，只在Task合法上下文实际入队
SysResult_t App_CameraConsumerFlush(AppCameraConsumer_t *c, AppLifecycleSendFn send, void *context)
{
    return c == NULL ? SYS_ERR_INVALID_ARG : App_LifecycleTxFlush(&c->tx, send, context);
}

//按原协议填充普通C消息，清零未声明的完成条件
static SysResult_t Consumer_Stage(AppCameraConsumer_t *c, uint32_t operation,
    uint32_t request_id, SysResult_t business, SysResult_t cleanup)
{
    AppLifecycleMessage_t request;
    SysResult_t result;
    memset(&request, 0, sizeof(request));
    request.version = APP_LIFECYCLE_VERSION;
    request.kind = APP_LIFECYCLE_REQUEST;
    request.operation = operation;
    request.source = c->tx.source;
    request.target = APP_LIFECYCLE_CAMERA;
    request.identity.session_id = c->session;
    request.identity.request_id = request_id;
    request.identity.consumer_id = c->tx.consumer_id;
    request.identity.consumer_generation = c->generation;
    request.business_result = business;
    request.cleanup_result = cleanup;
    result = App_LifecycleTxStage(&c->tx, &request);
    if (result == SYS_OK)
        c->request = request;
    return result;
}

//Getter只复制身份及阶段，不改变协议窗口
void App_CameraConsumerGetStatus(const AppCameraConsumer_t *c, AppCameraConsumerStatus_t *status)
{
    if (c == NULL || status == NULL)
        return;
    status->phase = c->phase;
    status->session = c->session;
    status->generation = c->generation;
    status->request_id = c->request.identity.request_id;
    status->granted = c->granted;
    status->result = c->result;
    status->receive_result = c->receive_result;
}
