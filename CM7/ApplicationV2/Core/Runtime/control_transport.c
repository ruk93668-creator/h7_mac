#include "control_transport.h"
#include <string.h>

#define MAGIC 0x43544137U

SysResult_t CSAdapter_Init(CSAdapter *a, CSService *s, uint32_t channel, const CSAdapterOps *o,
                           void *p)
{
    if (!a || a->magic || !s || channel < CS_IPC || channel > CS_ETHERNET || !o || !o->owner ||
        !o->receive || !o->send)
        return SYS_ERR_INVALID_ARG;
    if (!o->owner(p))
        return SYS_ERR_INVALID_STATE;
    memset(a, 0, sizeof(*a));
    a->service = s;
    a->channel = channel;
    a->ops = o;
    a->port = p;
    a->magic = MAGIC;
    return SYS_OK;
}

static SysResult_t dispatch(CSAdapter *a, const CSIngress *m, uint32_t now)
{
    CSService *s = a->service;
    CSEgress *out = &a->output;
    const CSPeer *p = &m->route.peer;

    switch (m->type)
    {
    case CSA_OPEN:
        return CS_Open(s, p, &out->session);
    case CSA_SUBMIT:
        return CS_Submit(s, p, &m->body.request, now, &out->reply);
    case CSA_QUERY:
        return CS_Query(s, p, &m->body.id, &out->reply);
    case CSA_CONFIRM:
        return CS_Confirm(s, p, &m->body.reply);
    case CSA_CANCEL:
        return CS_Cancel(s, p, &m->body.id);
    case CSA_CLOSE:
        return CS_Close(s, p, m->body.id.boot, m->body.id.session);
    default:
        return SYS_ERR_PROTOCOL;
    }
}

SysResult_t CSAdapter_Process(CSAdapter *a, uint32_t now)
{
    CSIngress m;
    SysResult_t z;
    if (!a || a->magic != MAGIC || !a->ops->owner(a->port))
        return SYS_ERR_INVALID_STATE;
    if (a->busy)
        return SYS_ERR_BUSY;
    a->busy = 1U;
    if (!a->pending)
    {
        z = a->ops->receive(a->port, &m);
        if (z || m.route.peer.transport != a->channel || !m.route.exchange)
        {
            a->busy = 0U;
            return z ? z : SYS_ERR_PROTOCOL;
        }
        memset(&a->output, 0, sizeof(a->output));
        a->output.route = m.route;
        a->output.type = m.type;
        a->output.boot = a->service->boot;
        a->output.result = dispatch(a, &m, now);
        a->pending = 1U;
    }
    /* 回复背压保留同一个exchange，不重新受理，不代表客户端消费。 */
    z = a->ops->send(a->port, &a->output);
    if (!z)
        a->pending = 0U;
    a->busy = 0U;
    return z;
}
