#include "storage_endpoint.h"
#include <string.h>
#define CLIENT_MAGIC 0x53564336U
#define ENDPOINT_MAGIC 0x53564536U
static int valid(const VStorageTransport *t)
{
    return t && t->ops && t->ops->is_storage && t->ops->is_supervisor && t->ops->send_request &&
           t->ops->receive_request && t->ops->send_reply && t->ops->receive_reply &&
           t->ops->send_consumed && t->ops->receive_consumed && t->ops->notify;
}
static SysResult_t client_enter(VStorageClient *c)
{
    if (!c || c->magic != CLIENT_MAGIC || !c->transport.ops->is_supervisor(c->transport.port))
        return SYS_ERR_INVALID_STATE;
    if (c->busy)
        return SYS_ERR_BUSY;
    c->busy = 1;
    return 0;
}
static SysResult_t client_leave(VStorageClient *c, SysResult_t z)
{
    c->busy = 0;
    return z;
}
SysResult_t VStorageClient_Init(VStorageClient *c, uint32_t module, uint32_t session,
                                const VStorageTransport *t)
{
    if (!c || c->magic || !module || !session || !valid(t))
        return SYS_ERR_INVALID_ARG;
    if (!t->ops->is_supervisor(t->port))
        return SYS_ERR_INVALID_STATE;
    memset(c, 0, sizeof(*c));
    c->module = module;
    c->session = session;
    c->transport = *t;
    c->magic = CLIENT_MAGIC;
    return 0;
}
static SysResult_t submit(void *p, const LCRequest *r)
{
    VStorageClient *c = p;
    SysResult_t z = client_enter(c);
    if (z)
        return z;
    if (!r || r->id.module != c->module || r->id.session != c->session || !r->id.request ||
        !LC_Target(r->id.operation) || !LC_Stable(r->expected) || !r->timeout ||
        r->timeout > 0x7FFFFFFFU)
        return client_leave(c, SYS_ERR_PROTOCOL);
    if (r->id.request == c->request.id.request)
        return client_leave(c, !memcmp(r, &c->request, sizeof(*r)) ? 0 : SYS_ERR_PROTOCOL);
    if (r->id.request < c->request.id.request)
        return client_leave(c, SYS_ERR_PROTOCOL);
    if (c->active)
        return client_leave(c, SYS_ERR_BUSY);
    z = c->transport.ops->send_request(c->transport.port, r);
    if (!z)
    {
        c->request = *r;
        c->active = 1;
        c->has_reply = 0;
        c->transport.ops->notify(c->transport.port, VST_ACTOR_STORAGE);
    }
    return client_leave(c, z);
}
static SysResult_t read_reply(void *p, const V2ExecId *id, LCCompletion *out)
{
    VStorageClient *c = p;
    VStorageReply v;
    SysResult_t z = client_enter(c);
    if (z)
        return z;
    if (!out || V2_Overlap(c, sizeof(*c), out, sizeof(*out)) || !LC_IdEqual(id, &c->request.id))
        return client_leave(c, SYS_ERR_PROTOCOL);
    if (!c->has_reply)
    {
        z = c->transport.ops->receive_reply(c->transport.port, &v);
        if (z)
            return client_leave(c, z);
        if (!LC_IdEqual(&v.completion.id, id))
            return client_leave(c, SYS_ERR_NOT_FOUND); /* stale is drained, never applied */
        if (v.delivery_error > 0 ||
            (!v.delivery_error && (v.completion.result > 0 || v.completion.actual_result > 0 ||
                                   !LC_Stable(v.completion.fact))))
            return client_leave(c, SYS_ERR_PROTOCOL);
        c->reply = v;
        c->has_reply = 1;
    }
    if (c->reply.delivery_error)
    {
        /* Execution Manager不会消费拒绝执行业务的回复，由传输层确认。 */
        if (c->active)
        {
            z = c->transport.ops->send_consumed(c->transport.port, &c->reply.completion);
            if (z)
                return client_leave(c, z == SYS_ERR_NO_RESOURCE ? SYS_ERR_BUSY : z);
            c->active = 0;
            c->transport.ops->notify(c->transport.port, VST_ACTOR_STORAGE);
        }
        return client_leave(c, c->reply.delivery_error);
    }
    *out = c->reply.completion;
    return client_leave(c, 0);
}
static SysResult_t consumed(void *p, const LCCompletion *v)
{
    VStorageClient *c = p;
    SysResult_t z = client_enter(c);
    if (z)
        return z;
    if (!v || !c->has_reply || c->reply.delivery_error ||
        memcmp(v, &c->reply.completion, sizeof(*v)))
        return client_leave(c, SYS_ERR_PROTOCOL);
    if (!c->active)
        return client_leave(c, 0);
    z = c->transport.ops->send_consumed(c->transport.port, v);
    if (!z)
    {
        c->active = 0;
        c->transport.ops->notify(c->transport.port, VST_ACTOR_STORAGE);
    }
    return client_leave(c, z);
}
SysResult_t VStorageClient_Endpoint(VStorageClient *c, EMEndpoint *out)
{
    SysResult_t z;
    if (!out || !c || V2_Overlap(c, sizeof(*c), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    z = client_enter(c);
    if (z)
        return z;
    out->module = c->module;
    out->port = c;
    out->submit = submit;
    out->read = read_reply;
    out->consume = consumed;
    return client_leave(c, 0);
}
SysResult_t VStorageEndpoint_Init(VStorageEndpoint *e, VStorage *c, const VStorageTransport *t)
{
    if (!e || e->magic || !c || !valid(t))
        return SYS_ERR_INVALID_ARG;
    if (!t->ops->is_storage(t->port))
        return SYS_ERR_INVALID_STATE;
    memset(e, 0, sizeof(*e));
    e->storage = c;
    e->transport = *t;
    e->magic = ENDPOINT_MAGIC;
    return 0;
}
SysResult_t VStorageEndpoint_RunRound(VStorageEndpoint *e, uint32_t now)
{
    SysResult_t z;
    LCRequest incoming;
    if (!e || e->magic != ENDPOINT_MAGIC || !e->transport.ops->is_storage(e->transport.port))
        return SYS_ERR_INVALID_STATE;
    if (e->busy)
        return SYS_ERR_BUSY;
    e->busy = 1;

    if (e->active && e->sent)
    {
        if (!e->ack_pending &&
            e->transport.ops->receive_consumed(e->transport.port, &e->ack) == 0 &&
            !memcmp(&e->ack, &e->reply.completion, sizeof(e->ack)))
            e->ack_pending = 1;
        if (e->ack_pending &&
            (!e->accepted || VStorage_ConfirmCompletion(e->storage, &e->ack) == SYS_OK))
        {
            e->active = 0;
            e->ack_pending = 0;
        }
    }
    if (!e->active && e->transport.ops->receive_request(e->transport.port, &incoming) == SYS_OK &&
        incoming.id.module == e->storage->lifecycle.module &&
        incoming.id.session == e->storage->lifecycle.session &&
        incoming.id.request > e->request.id.request)
    {
        e->request = incoming;
        e->active = 1;
        e->accepted = e->ready = e->sent = 0;
        memset(&e->reply, 0, sizeof(e->reply));
        e->reply.completion.id = incoming.id;
        z = VStorage_Request(e->storage, &incoming, now);
        if (!z)
            e->accepted = 1;
        else
        {
            e->reply.delivery_error = z;
            e->ready = 1;
        }
    }
    /* 运输背压不影响UVC推进、帧归还或后台停止。 */
    z = VStorage_RunRound(e->storage, now);
    if (!z && e->active && e->accepted && !e->ready &&
        VStorage_ReadCompletion(e->storage, &e->request.id, &e->reply.completion) == SYS_OK)
        e->ready = 1;
    if (!z && e->active && e->ready && !e->sent)
    {
        z = e->transport.ops->send_reply(e->transport.port, &e->reply);
        if (!z)
        {
            e->sent = 1;
            e->transport.ops->notify(e->transport.port, VST_ACTOR_SUPERVISOR);
        }
    }
    e->busy = 0;
    return z;
}
