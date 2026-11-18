#include "control_execution_link.h"
#include <string.h>

#define CLIENT_MAGIC 0x434c4337U
#define SERVER_MAGIC 0x434c5337U
#define LINK_RECEIVED 1U
#define LINK_RUNNING 2U
#define LINK_RESULT 3U
#define LINK_SENT 4U

static int valid(const CSExecLinkOps *o)
{
    return o && o->control_owner && o->execution_owner && o->send_work && o->receive_work &&
           o->send_result && o->receive_result && o->send_ack && o->receive_ack;
}

SysResult_t CSExecClient_Init(CSExecClient *c, const CSExecLinkOps *o, void *p)
{
    if (!c || c->magic || !valid(o))
        return SYS_ERR_INVALID_ARG;
    if (!o->control_owner(p))
        return SYS_ERR_INVALID_STATE;
    memset(c, 0, sizeof(*c));
    c->magic = CLIENT_MAGIC;
    c->ops = o;
    c->port = p;
    return SYS_OK;
}

static SysResult_t enter(CSExecClient *c)
{
    if (!c || c->magic != CLIENT_MAGIC || !c->ops->control_owner(c->port))
        return SYS_ERR_INVALID_STATE;
    if (c->busy)
        return SYS_ERR_BUSY;
    c->busy = 1U;
    return SYS_OK;
}

static SysResult_t leave(CSExecClient *c, SysResult_t z)
{
    c->busy = 0U;
    return z;
}

SysResult_t CSExecClient_Submit(CSExecClient *c, const CSExecution *w)
{
    SysResult_t z = enter(c);
    if (z)
        return z;
    if (!w || !w->id.ticket || w->id.ticket <= c->id.ticket)
        return leave(c, SYS_ERR_PROTOCOL);
    if (c->active)
        return leave(c, SYS_ERR_BUSY);
    z = c->ops->send_work(c->port, w);
    if (!z)
    {
        c->id = w->id;
        c->active = 1U;
        c->cached = 0U;
    }
    return leave(c, z);
}

SysResult_t CSExecClient_Read(CSExecClient *c, const CSIdentity *id, CSExecResult *out)
{
    CSExecResult r;
    SysResult_t z = enter(c);
    if (z)
        return z;
    if (!out || V2_Overlap(c, sizeof(*c), out, sizeof(*out)) || !CS_IdEqual(id, &c->id))
        return leave(c, SYS_ERR_PROTOCOL);
    if (!c->cached)
    {
        z = c->ops->receive_result(c->port, &r);
        if (z)
            return leave(c, z);
        if (!CS_IdEqual(id, &r.id))
            return leave(c, SYS_ERR_NOT_FOUND);
        if ((r.state != EM_DONE && r.state != EM_BLOCKED && r.state != CS_EXEC_REFUSED) ||
            r.result > SYS_OK || r.recovery > SYS_OK ||
            ((r.state == EM_BLOCKED || r.state == CS_EXEC_REFUSED) && !r.result))
            return leave(c, SYS_ERR_PROTOCOL);
        c->result = r;
        c->cached = 1U;
    }
    *out = c->result;
    return leave(c, SYS_OK);
}

SysResult_t CSExecClient_Consume(CSExecClient *c, const CSExecResult *r)
{
    SysResult_t z = enter(c);
    if (z)
        return z;
    if (!r || !c->cached || memcmp(r, &c->result, sizeof(*r)))
        return leave(c, SYS_ERR_PROTOCOL);
    if (!c->active)
        return leave(c, SYS_OK);
    z = c->ops->send_ack(c->port, r);
    if (!z)
        c->active = 0U;
    return leave(c, z);
}

SysResult_t CSExecServer_Init(CSExecServer *s, CSExecutionEndpoint *e, const CSExecLinkOps *o,
                              void *p)
{
    if (!s || s->magic || !e || !valid(o))
        return SYS_ERR_INVALID_ARG;
    if (!o->execution_owner(p))
        return SYS_ERR_INVALID_STATE;
    memset(s, 0, sizeof(*s));
    s->magic = SERVER_MAGIC;
    s->endpoint = e;
    s->ops = o;
    s->port = p;
    return SYS_OK;
}

static SysResult_t progress(CSExecServer *s, uint32_t now)
{
    SysResult_t z;
    CSExecResult ack;

    if (s->phase == LINK_SENT)
    {
        z = s->ops->receive_ack(s->port, &ack);
        if (z)
            return z;
        if (memcmp(&ack, &s->result, sizeof(ack)))
            return SYS_ERR_NOT_FOUND;
        if (s->result.state != CS_EXEC_REFUSED)
        {
            z = CSExec_Consume(s->endpoint, &ack);
            if (z)
                return z;
        }
        s->phase = 0U;
    }
    if (!s->phase)
    {
        z = s->ops->receive_work(s->port, &s->work);
        if (z)
            return z;
        if (s->work.id.boot != s->endpoint->boot || s->work.id.ticket <= s->highest)
            return SYS_ERR_NOT_FOUND;
        s->highest = s->work.id.ticket;
        s->phase = LINK_RECEIVED;
    }
    if (s->phase == LINK_RECEIVED)
    {
        z = CSExec_Submit(s->endpoint, &s->work);
        if (z == SYS_ERR_BUSY || z == SYS_ERR_NO_RESOURCE)
            return z;
        if (z)
        {
            s->result = (CSExecResult){s->work.id, CS_EXEC_REFUSED, z, SYS_OK};
            s->phase = LINK_RESULT;
        }
        else
            s->phase = LINK_RUNNING;
        return SYS_OK;
    }
    if (s->phase == LINK_RUNNING)
    {
        z = CSExec_Process(s->endpoint, now);
        if (!z)
            z = CSExec_Read(s->endpoint, &s->work.id, &s->result);
        if (z)
            return z;
        s->phase = LINK_RESULT;
    }
    /* Reply满时只重试冻结的结果；消费确认前保留接收端状态。 */
    z = s->ops->send_result(s->port, &s->result);
    if (!z)
        s->phase = LINK_SENT;
    return z;
}

SysResult_t CSExecServer_Process(CSExecServer *s, uint32_t now)
{
    SysResult_t z;
    if (!s || s->magic != SERVER_MAGIC || !s->ops->execution_owner(s->port))
        return SYS_ERR_INVALID_STATE;
    if (s->busy)
        return SYS_ERR_BUSY;
    s->busy = 1U;
    z = progress(s, now);
    s->busy = 0U;
    return z;
}
