#include "storage_consumer.h"
#include "app_camera_qualification.h"
#include <string.h>
#define MAGIC 0x53544336U
static SysResult_t enter(VStorageConsumer *c)
{
    if (!c || c->magic != MAGIC || !c->ops->is_owner(c->port))
        return SYS_ERR_INVALID_STATE;
    if (c->busy)
        return SYS_ERR_BUSY;
    c->busy = 1;
    return 0;
}
static SysResult_t leave(VStorageConsumer *c, SysResult_t z)
{
    c->busy = 0;
    return z;
}
SysResult_t VStorageConsumer_Init(VStorageConsumer *c, const VStorageConsumerOps *ops, void *p)
{
    SysResult_t z;
    if (!c || c->magic || !ops || !ops->is_owner || !ops->send || !ops->receive_reply)
        return SYS_ERR_INVALID_ARG;
    if (!ops->is_owner(p))
        return SYS_ERR_INVALID_STATE;
    memset(c, 0, sizeof(*c));
    c->ops = ops;
    c->port = p;
    z = App_LifecycleTxInit(&c->tx, APP_LIFECYCLE_STORAGE, APP_LIFECYCLE_CAMERA,
                            APP_CONSUMER_STORAGE);
    if (z)
        return z;
    c->magic = MAGIC;
    return 0;
}
SysResult_t VStorageConsumer_Begin(VStorageConsumer *c)
{
    SysResult_t z = enter(c);
    if (z)
        return z;
    if (c->phase != VSC_IDLE || c->grant.session)
        return leave(c, SYS_ERR_BUSY);
    z = App_QualificationNextGeneration(&c->generation_counter, &c->generation);
    if (z)
        return leave(c, z);
    c->phase = VSC_QUERY;
    c->reserved = c->staged = 0;
    c->result = 0;
    return leave(c, 0);
}
SysResult_t VStorageConsumer_End(VStorageConsumer *c, SysResult_t error, SysResult_t cleanup,
                                 uint32_t flags)
{
    SysResult_t z = enter(c);
    const uint32_t required =
        APP_LIFECYCLE_INPUT_CLOSED | APP_LIFECYCLE_WORK_QUIESCENT | APP_LIFECYCLE_FRAMES_DRAINED;
    if (z)
        return z;
    if (error > SYS_OK || cleanup > SYS_OK || (flags & required) != required ||
        (flags & ~APP_LIFECYCLE_CONSUMER_FLAGS))
        return leave(c, SYS_ERR_INVALID_ARG);
    if (c->phase == VSC_RELEASE || c->phase == VSC_IDLE_REQUEST)
        return leave(c, c->business_error == error && c->cleanup_error == cleanup &&
                                c->release_flags == flags
                            ? SYS_OK
                            : SYS_ERR_PROTOCOL);
    if (c->phase != VSC_GRANTED || !c->grant.session)
        return leave(c, SYS_ERR_INVALID_STATE);
    c->business_error = error;
    c->cleanup_error = cleanup;
    c->release_flags = flags;
    c->phase = VSC_RELEASE;
    c->reserved = c->staged = 0;
    c->result = 0;
    return leave(c, 0);
}
static SysResult_t stage(VStorageConsumer *c)
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
        c->request.source = APP_LIFECYCLE_STORAGE;
        c->request.target = APP_LIFECYCLE_CAMERA;
        c->request.identity.consumer_id = APP_CONSUMER_STORAGE;
        c->request.identity.consumer_generation = c->generation;
        c->request.identity.request_id = id;
        c->request.operation = c->phase == VSC_QUERY     ? APP_LIFECYCLE_SESSION_QUERY
                               : c->phase == VSC_ACQUIRE ? APP_LIFECYCLE_ACQUIRE
                               : c->phase == VSC_RELEASE ? APP_LIFECYCLE_RELEASE
                                                         : APP_LIFECYCLE_STOP_IF_IDLE;
        c->request.identity.session_id = c->phase == VSC_QUERY ? 0 : c->session;
        if (c->phase == VSC_RELEASE)
        {
            c->release.grant = c->grant;
            c->release.request = id;
            c->request.identity.session_id = c->grant.session;
            c->request.identity.consumer_generation = c->grant.generation;
            c->request.completion_flags = c->release_flags;
            c->request.cleanup_result = c->cleanup_error;
            c->request.business_result = c->business_error;
        }
        c->reserved = 1;
    }
    z = App_LifecycleTxStage(&c->tx, &c->request);
    if (!z)
        c->staged = 1;
    return z;
}
SysResult_t VStorageConsumer_Process(VStorageConsumer *c)
{
    SysResult_t z = enter(c);
    AppLifecycleMessage_t reply;
    if (z)
        return z;
    if (c->phase == VSC_IDLE || c->phase == VSC_GRANTED)
        return leave(c, 0);
    if (c->phase == VSC_BLOCKED)
        return leave(c, c->result ? c->result : SYS_ERR_INVALID_STATE);
    if (!c->staged)
    {
        z = stage(c);
        if (z)
        {
            c->result = z;
            c->phase = VSC_BLOCKED;
            return leave(c, z);
        }
    }
    /* WAIT_REPLY超时不能清空Tx；队列满只重试原请求原编号。 */
    (void)App_LifecycleTxFlush(&c->tx, c->ops->send, c->port);
    z = c->ops->receive_reply(c->port, &reply);
    if (!z && App_LifecycleMatchesReply(&c->request, &reply))
        (void)App_LifecycleTxAcceptReply(&c->tx, &reply);
    z = App_LifecycleTxGetCompleted(&c->tx, &reply);
    if (z)
        return leave(c, SYS_ERR_BUSY);
    z = App_LifecycleTxConfirmConsumed(&c->tx, &c->request);
    if (z)
        return leave(c, z);
    c->result = reply.result;
    c->reserved = c->staged = 0;
    if (c->phase == VSC_QUERY)
    {
        if (!reply.result)
        {
            c->session = reply.identity.session_id;
            c->phase = VSC_ACQUIRE;
        }
        else
            c->phase = VSC_IDLE;
    }
    else if (c->phase == VSC_ACQUIRE)
    {
        if (!reply.result)
        {
            c->grant.session = c->request.identity.session_id;
            c->grant.consumer = APP_CONSUMER_STORAGE;
            c->grant.generation = c->request.identity.consumer_generation;
            c->grant.acquire = c->request.identity.request_id;
            c->phase = VSC_GRANTED;
        }
        else
            c->phase = VSC_IDLE;
    }
    else if (c->phase == VSC_RELEASE)
    {
        if (!reply.result && (reply.completion_flags & APP_LIFECYCLE_QUALIFICATION_RELEASED))
        {
            memset(&c->grant, 0, sizeof(c->grant));
            c->phase = VSC_IDLE_REQUEST;
        }
        else
        {
            if (!c->result)
                c->result = SYS_ERR_PROTOCOL;
            c->phase = VSC_BLOCKED;
        }
    }
    else
    {
        c->camera_stop_result = reply.result;
        c->result = 0;
        c->phase = VSC_IDLE;
    }
    return leave(c, c->result);
}
