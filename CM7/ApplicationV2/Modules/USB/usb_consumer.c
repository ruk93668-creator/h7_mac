#include "usb_consumer.h"
#include "app_camera_qualification.h"
#include <string.h>
#define MAGIC 0x55514335U
static SysResult_t enter(VUSBConsumer *c)
{
    if (!c || c->magic != MAGIC || !c->ops->is_owner(c->port))
        return SYS_ERR_INVALID_STATE;
    if (c->busy)
        return SYS_ERR_BUSY;
    c->busy = 1;
    return 0;
}
static SysResult_t leave(VUSBConsumer *c, SysResult_t z)
{
    c->busy = 0;
    return z;
}
static void fault(VUSBConsumer *c, SysResult_t z)
{
    if (z && !c->first_error)
        c->first_error = z;
}
SysResult_t VUSBConsumer_Init(VUSBConsumer *c, const VUSBConsumerOps *ops, void *p)
{
    SysResult_t z;
    if (!c || c->magic || !ops || !ops->is_owner || !ops->send || !ops->receive_reply)
        return SYS_ERR_INVALID_ARG;
    if (!ops->is_owner(p))
        return SYS_ERR_INVALID_STATE;
    memset(c, 0, sizeof(*c));
    c->ops = ops;
    c->port = p;
    z = App_LifecycleTxInit(&c->tx, APP_LIFECYCLE_USB, APP_LIFECYCLE_CAMERA, APP_CONSUMER_USB);
    if (z)
        return z;
    c->magic = MAGIC;
    return 0;
}
SysResult_t VUSBConsumer_Begin(VUSBConsumer *c)
{
    SysResult_t z = enter(c);
    if (z)
        return z;
    if (c->phase != VUC_IDLE || c->grant.session)
        return leave(c, SYS_ERR_BUSY);
    z = App_QualificationNextGeneration(&c->generation_counter, &c->generation);
    if (z)
        return leave(c, z);
    c->phase = VUC_QUERY;
    c->reserved = c->staged = 0;
    c->result = 0;
    c->first_error = 0;
    return leave(c, 0);
}
SysResult_t VUSBConsumer_End(VUSBConsumer *c, SysResult_t error)
{
    SysResult_t z = enter(c);
    if (z)
        return z;
    if (c->phase == VUC_RELEASE || c->phase == VUC_IDLE_REQUEST)
        return leave(c, 0);
    if (c->phase != VUC_GRANTED || !c->grant.session)
        return leave(c, SYS_ERR_INVALID_STATE);
    c->business_error = error;
    c->phase = VUC_RELEASE;
    c->reserved = c->staged = 0;
    c->result = 0;
    return leave(c, 0);
}
static SysResult_t stage(VUSBConsumer *c)
{
    SysResult_t z;
    uint32_t id;
    if (!c->reserved)
    {
        z = App_LifecycleTxReserveIds(&c->tx, 1, &id);
        if (z)
            return z;
        memset(&c->request, 0, sizeof(c->request));
        c->request.version = APP_LIFECYCLE_VERSION;
        c->request.kind = APP_LIFECYCLE_REQUEST;
        c->request.source = APP_LIFECYCLE_USB;
        c->request.target = APP_LIFECYCLE_CAMERA;
        c->request.identity.consumer_id = APP_CONSUMER_USB;
        c->request.identity.consumer_generation = c->generation;
        c->request.identity.request_id = id;
        c->request.operation = c->phase == VUC_QUERY     ? APP_LIFECYCLE_SESSION_QUERY
                               : c->phase == VUC_ACQUIRE ? APP_LIFECYCLE_ACQUIRE
                               : c->phase == VUC_RELEASE ? APP_LIFECYCLE_RELEASE
                                                         : APP_LIFECYCLE_STOP_IF_IDLE;
        c->request.identity.session_id = c->phase == VUC_QUERY ? 0 : c->session;
        if (c->phase == VUC_RELEASE)
        {
            c->release.grant = c->grant;
            c->release.request = id;
            c->request.identity.session_id = c->grant.session;
            c->request.identity.consumer_generation = c->grant.generation;
            c->request.completion_flags = APP_LIFECYCLE_INPUT_CLOSED |
                                          APP_LIFECYCLE_WORK_QUIESCENT |
                                          APP_LIFECYCLE_FRAMES_DRAINED;
            c->request.business_result = c->business_error;
        }
        c->reserved = 1;
    }
    z = App_LifecycleTxStage(&c->tx, &c->request);
    if (!z)
        c->staged = 1;
    return z;
}
SysResult_t VUSBConsumer_Process(VUSBConsumer *c)
{
    SysResult_t z = enter(c);
    AppLifecycleMessage_t reply;
    if (z)
        return z;
    if (c->phase == VUC_IDLE || c->phase == VUC_GRANTED)
        return leave(c, 0);
    if (c->phase == VUC_BLOCKED)
        return leave(c, c->result ? c->result : SYS_ERR_INVALID_STATE);
    if (!c->staged)
    {
        z = stage(c);
        if (z)
        {
            fault(c, z);
            return leave(c, z);
        }
    }
    /* WAIT_REPLY超时不能清空Tx；队列满只重试原请求原编号。 */
    (void)App_LifecycleTxFlush(&c->tx, c->ops->send, c->port);
    z = c->ops->receive_reply(c->port, &reply);
    if (!z && App_LifecycleMatchesReply(&c->request, &reply))
    {
        z = App_LifecycleTxAcceptReply(&c->tx, &reply);
        if (z)
            fault(c, z);
    }
    z = App_LifecycleTxGetCompleted(&c->tx, &reply);
    if (z)
        return leave(c, z);
    z = App_LifecycleTxConfirmConsumed(&c->tx, &c->request);
    if (z)
    {
        fault(c, z);
        return leave(c, z);
    }
    c->result = reply.result;
    c->reserved = c->staged = 0;
    if (c->phase == VUC_QUERY)
    {
        if (!reply.result)
        {
            c->session = reply.identity.session_id;
            c->phase = VUC_ACQUIRE;
        }
        else
            c->phase = VUC_IDLE;
    }
    else if (c->phase == VUC_ACQUIRE)
    {
        if (!reply.result)
        {
            c->grant.session = c->request.identity.session_id;
            c->grant.consumer = APP_CONSUMER_USB;
            c->grant.generation = c->request.identity.consumer_generation;
            c->grant.acquire = c->request.identity.request_id;
            c->phase = VUC_GRANTED;
        }
        else
            c->phase = VUC_IDLE;
    }
    else if (c->phase == VUC_RELEASE)
    {
        if (!reply.result && (reply.completion_flags & APP_LIFECYCLE_QUALIFICATION_RELEASED))
        {
            memset(&c->grant, 0, sizeof(c->grant));
            c->phase = VUC_IDLE_REQUEST;
        }
        else
        {
            if (!c->result)
                c->result = SYS_ERR_PROTOCOL;
            c->phase = VUC_BLOCKED;
        }
    }
    else
    {
        c->camera_stop_result = reply.result;
        c->result = 0;
        c->phase = VUC_IDLE;
    }
    if (c->result)
        fault(c, c->result);
    return leave(c, c->result);
}
