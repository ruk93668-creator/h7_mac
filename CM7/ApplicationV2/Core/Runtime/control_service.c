#include "control_service.h"
#include <string.h>

#define MAGIC 0x43535637U

int CS_IdEqual(const CSIdentity *a, const CSIdentity *b)
{
    return a && b && a->boot == b->boot && a->session == b->session && a->sequence == b->sequence &&
           a->ticket == b->ticket;
}

static int peer_equal(const CSPeer *a, const CSPeer *b)
{
    return a->transport == b->transport && a->principal == b->principal && a->client == b->client;
}

static int trusted_peer(const CSPeer *p)
{
    return p && p->transport >= CS_IPC && p->transport <= CS_ETHERNET && p->principal && p->client;
}

static SysResult_t enter(CSService *s)
{
    if (!s || s->magic != MAGIC || !s->ops->owner(s->port))
        return SYS_ERR_INVALID_STATE;
    if (s->busy)
        return SYS_ERR_BUSY;
    s->busy = 1U;
    return SYS_OK;
}

static SysResult_t leave(CSService *s, SysResult_t z)
{
    s->busy = 0U;
    return z;
}

static const CSProfile *profile(const CSService *s, uint32_t id)
{
    uint16_t i;
    for (i = 0; i < s->catalog->profile_count; ++i)
        if (s->catalog->profiles[i].id == id)
            return &s->catalog->profiles[i];
    return NULL;
}

static CSSession *session(CSService *s, const CSPeer *p, uint32_t boot, uint32_t id)
{
    uint32_t i;
    if (boot != s->boot || !id || !trusted_peer(p))
        return NULL;
    for (i = 0; i < CS_SESSION_MAX; ++i)
        if (s->sessions[i].id == id && peer_equal(p, &s->sessions[i].peer))
            return &s->sessions[i];
    return NULL;
}

static CSRecord *record(CSService *s, uint32_t id, uint32_t seq)
{
    uint32_t i;
    for (i = 0; i < CS_RECORD_MAX; ++i)
        if (s->records[i].reply.id.session == id && s->records[i].reply.id.sequence == seq)
            return &s->records[i];
    return NULL;
}

static int waiting(SysResult_t z)
{
    return z == SYS_ERR_BUSY || z == SYS_ERR_NO_RESOURCE || z == SYS_ERR_NOT_FOUND;
}

static int output_ok(const CSService *s, const void *p, size_t n)
{
    return p && !V2_Overlap(s, sizeof(*s), p, n);
}

SysResult_t CS_Init(CSService *s, uint32_t boot, const CSCatalog *c, const CSPortOps *o, void *p)
{
    uint16_t i, j;
    if (!s || s->magic || !boot || !c || !c->registry || !c->graph || !c->bindings ||
        !c->binding_count || !c->profiles || !c->profile_count ||
        c->profile_count > GRAPH_PIPELINE_MAX || !c->validate || !o || !o->owner || !o->authorize ||
        !o->snapshot || !o->submit || !o->read || !o->consume)
        return SYS_ERR_INVALID_ARG;
    if (!o->owner(p))
        return SYS_ERR_INVALID_STATE;
    for (i = 0; i < c->profile_count; ++i)
    {
        if (!c->profiles[i].id)
            return SYS_ERR_INVALID_ARG;
        for (j = 0; j < i; ++j)
            if (c->profiles[i].id == c->profiles[j].id)
                return SYS_ERR_ALREADY_EXISTS;
    }
    memset(s, 0, sizeof(*s));
    s->boot = boot;
    s->catalog = c;
    s->ops = o;
    s->port = p;
    s->magic = MAGIC;
    return SYS_OK;
}

SysResult_t CS_Open(CSService *s, const CSPeer *p, uint32_t *out)
{
    uint32_t i, free_index = CS_SESSION_MAX;
    SysResult_t z = enter(s);
    if (z)
        return z;
    if (!trusted_peer(p) || !output_ok(s, out, sizeof(*out)))
        return leave(s, SYS_ERR_INVALID_ARG);
    z = s->ops->authorize(s->port, p, 0U, 0U, 0U, 0U);
    if (z)
        return leave(s, z);
    for (i = 0; i < CS_SESSION_MAX; ++i)
    {
        if (s->sessions[i].id)
        {
            if (peer_equal(p, &s->sessions[i].peer))
            {
                *out = s->sessions[i].id;
                return leave(s, SYS_OK);
            }
        }
        else if (free_index == CS_SESSION_MAX)
            free_index = i;
    }
    if (free_index == CS_SESSION_MAX || s->next_session == UINT32_MAX)
        return leave(s, SYS_ERR_NO_RESOURCE);
    s->sessions[free_index].peer = *p;
    s->sessions[free_index].issued = 0U;
    s->sessions[free_index].id = ++s->next_session;
    *out = s->next_session;
    return leave(s, SYS_OK);
}

SysResult_t CS_Close(CSService *s, const CSPeer *p, uint32_t boot, uint32_t id)
{
    CSSession *c;
    uint32_t i;
    SysResult_t z = enter(s);
    if (z)
        return z;
    c = session(s, p, boot, id);
    if (!c)
        return leave(s, SYS_ERR_PROTOCOL);
    for (i = 0; i < CS_RECORD_MAX; ++i)
        if (s->records[i].reply.id.session == id && !s->records[i].delivered)
            return leave(s, SYS_ERR_BUSY);
    for (i = 0; i < CS_RECORD_MAX; ++i)
        if (s->records[i].reply.id.session == id)
            memset(&s->records[i], 0, sizeof(s->records[i]));
    memset(c, 0, sizeof(*c));
    return leave(s, SYS_OK);
}

static int valid_request(const CSRequest *r)
{
    return r && r->version == CS_VERSION && r->sequence && r->profile && r->timeout &&
           r->timeout <= 0x7fffffffU && r->operation >= CS_VALIDATE && r->operation <= CS_STOP &&
           (r->operation == CS_VALIDATE || r->revision) &&
           (r->operation == CS_STOP ? r->stop_mask && r->stop_mask <= UINT16_MAX && r->cleanup <= 1U
                                    : !r->stop_mask && !r->cleanup);
}

SysResult_t CS_Submit(CSService *s, const CSPeer *p, const CSRequest *r, uint32_t now, CSReply *out)
{
    CSSession *c;
    CSRecord *v;
    uint32_t i, index = CS_RECORD_MAX, oldest = UINT32_MAX;
    SysResult_t z = enter(s);
    if (z)
        return z;
    if (!valid_request(r) || !output_ok(s, out, sizeof(*out)) ||
        V2_Overlap(r, sizeof(*r), out, sizeof(*out)))
        return leave(s, SYS_ERR_INVALID_ARG);
    c = session(s, p, r->boot, r->session);
    if (!c)
        return leave(s, SYS_ERR_PROTOCOL);
    z = s->ops->authorize(s->port, p, r->operation, r->profile, 0U, 0U);
    if (z)
        return leave(s, z);
    v = record(s, r->session, r->sequence);
    if (v)
    {
        if (memcmp(&v->request, r, sizeof(*r)))
            return leave(s, SYS_ERR_PROTOCOL);
        *out = v->reply;
        return leave(s, SYS_OK);
    }
    if (r->sequence <= c->issued)
        return leave(s, SYS_ERR_NOT_FOUND); /* 已退休或跳号，不重新执行。 */
    if (!profile(s, r->profile))
        return leave(s, SYS_ERR_NOT_FOUND);
    if (s->active)
        return leave(s, SYS_ERR_BUSY);
    if (s->blocked && r->operation != CS_VALIDATE)
        return leave(s, SYS_ERR_INVALID_STATE);
    for (i = 0; i < CS_RECORD_MAX; ++i)
    {
        v = &s->records[i];
        if (!v->reply.id.ticket)
        {
            index = i;
            break;
        }
        if (v->delivered && v->reply.id.ticket < oldest)
        {
            index = i;
            oldest = v->reply.id.ticket;
        }
    }
    if (index == CS_RECORD_MAX || s->next_ticket == UINT32_MAX)
        return leave(s, SYS_ERR_NO_RESOURCE);
    v = &s->records[index];
    memset(v, 0, sizeof(*v));
    v->request = *r;
    v->reply.id = (CSIdentity){s->boot, r->session, r->sequence, ++s->next_ticket};
    v->reply.phase = CS_ACCEPTED;
    v->reply.profile = r->profile;
    v->reply.revision = r->revision;
    c->issued = r->sequence;
    s->active = index + 1U;
    s->started = now;
    s->exec_seen = 0U;
    *out = v->reply;
    return leave(s, SYS_OK);
}

static CSRecord *owned_record(CSService *s, const CSPeer *p, const CSIdentity *id)
{
    CSRecord *v;
    if (!id || !session(s, p, id->boot, id->session))
        return NULL;
    v = record(s, id->session, id->sequence);
    return v && CS_IdEqual(id, &v->reply.id) ? v : NULL;
}

SysResult_t CS_Query(CSService *s, const CSPeer *p, const CSIdentity *id, CSReply *out)
{
    CSRecord *v;
    SysResult_t z = enter(s);
    if (z)
        return z;
    if (!output_ok(s, out, sizeof(*out)))
        return leave(s, SYS_ERR_INVALID_ARG);
    v = owned_record(s, p, id);
    if (!v)
        return leave(s, SYS_ERR_NOT_FOUND);
    z = s->ops->authorize(s->port, p, v->request.operation, v->request.profile, 0U, 0U);
    if (!z)
        *out = v->reply;
    return leave(s, z);
}

SysResult_t CS_Confirm(CSService *s, const CSPeer *p, const CSReply *r)
{
    CSRecord *v;
    SysResult_t z = enter(s);
    if (z)
        return z;
    v = owned_record(s, p, r ? &r->id : NULL);
    if (!v || v->reply.phase != CS_FINAL || memcmp(r, &v->reply, sizeof(*r)))
        return leave(s, SYS_ERR_PROTOCOL);
    /* 消费只退休交付凭证，同序号重传仍读旧结果。 */
    v->delivered = 1U;
    return leave(s, SYS_OK);
}

static void first(CSRecord *v, SysResult_t z)
{
    if (z && !v->reply.first_error)
        v->reply.first_error = z;
}

static void finish(CSService *s, CSRecord *v, SysResult_t z)
{
    first(v, z);
    v->reply.actual_result = z;
    v->reply.phase = CS_FINAL;
    s->active = 0U;
}

SysResult_t CS_Cancel(CSService *s, const CSPeer *p, const CSIdentity *id)
{
    CSRecord *v;
    SysResult_t z = enter(s);
    if (z)
        return z;
    v = owned_record(s, p, id);
    if (!v)
        return leave(s, SYS_ERR_NOT_FOUND);
    z = s->ops->authorize(s->port, p, v->request.operation, v->request.profile, 0U, 0U);
    if (z)
        return leave(s, z);
    if (v->reply.phase == CS_FINAL)
        return leave(s, SYS_OK);
    if (v->reply.phase == CS_EXECUTING)
        return leave(s, SYS_ERR_BUSY);
    v->reply.effect = CS_EFFECT_CANCELLED;
    finish(s, v, SYS_ERR_INVALID_STATE);
    return leave(s, SYS_OK);
}

static SysResult_t check_plan(CSService *s, const CSPeer *p, const CSRecord *v)
{
    uint32_t i;
    for (i = 0; i < s->execution.plan.count; ++i)
    {
        const EMStep *step = &s->execution.plan.steps[i];
        SysResult_t z = s->ops->authorize(s->port, p, v->request.operation, v->request.profile,
                                          step->module, step->operation);
        if (!z && v->request.operation == CS_START)
            z = s->ops->authorize(s->port, p, CS_START, v->request.profile, step->module,
                                  step->operation == LC_CONFIGURE ? LC_CLEANUP : LC_DEACTIVATE);
        if (z)
            return z;
    }
    return SYS_OK;
}

static SysResult_t build(CSService *s, CSRecord *v)
{
    const CSProfile *p = profile(s, v->request.profile);
    const CSCatalog *c = s->catalog;
    V2Issue issue = {0};
    GraphIssue graph_issue = {0};
    SysResult_t z =
        V2Composition_Validate(c->registry, c->graph, c->bindings, c->binding_count, &p->desired,
                               c->validate, &s->scratch, &s->candidate, &issue, &graph_issue);
    if (z || v->request.operation == CS_VALIDATE)
        return z;
    memset(&s->execution, 0, sizeof(s->execution));
    z = s->ops->snapshot(s->port, &s->execution.expected);
    if (z)
        return z;
    if (s->execution.expected.revision != v->request.revision ||
        s->execution.expected.prepared_profile != p->id)
        return SYS_ERR_INVALID_STATE; /* 仅执行已经绑定的配置；重配置属于后续阶段。 */
    s->execution.id = v->reply.id;
    s->execution.profile = p->id;
    s->execution.revision = v->request.revision;
    s->execution.step_timeout = v->request.timeout;
    return EM_BuildPlan(c->registry, &s->candidate, s->execution.expected.states,
                        (uint16_t)v->request.stop_mask,
                        v->request.operation == CS_START ? EM_START : EM_STOP, v->request.cleanup,
                        &s->execution.plan);
}

SysResult_t CS_Process(CSService *s, uint32_t now)
{
    CSRecord *v;
    CSSession *c = NULL;
    CSExecResult r;
    uint32_t i;
    SysResult_t z = enter(s);
    if (z)
        return z;
    if (!s->active)
        return leave(s, SYS_OK);
    v = &s->records[s->active - 1U];
    if (!s->exec_seen && (uint32_t)(now - s->started) >= v->request.timeout)
    {
        if (v->reply.phase != CS_EXECUTING)
        {
            finish(s, v, SYS_ERR_TIMEOUT);
            return leave(s, SYS_OK);
        }
        first(v, SYS_ERR_TIMEOUT); /* 继续等待实际完成，不清理活动执行。 */
    }
    if (v->reply.phase != CS_EXECUTING)
    {
        for (i = 0; i < CS_SESSION_MAX; ++i)
            if (s->sessions[i].id == v->reply.id.session)
                c = &s->sessions[i];
        if (!c)
            z = SYS_ERR_PROTOCOL;
        else
            z = s->ops->authorize(s->port, &c->peer, v->request.operation, v->request.profile, 0U,
                                  0U);
        if (z)
        {
            finish(s, v, z);
            return leave(s, SYS_OK);
        }
    }
    if (v->reply.phase == CS_ACCEPTED)
    {
        z = build(s, v);
        if (z || v->request.operation == CS_VALIDATE)
        {
            if (!z)
                v->reply.effect = CS_EFFECT_CANDIDATE;
            finish(s, v, z);
        }
        else
            v->reply.phase = CS_PLANNED;
        return leave(s, SYS_OK);
    }
    if (v->reply.phase == CS_PLANNED)
    {
        z = check_plan(s, &c->peer, v);
        if (z)
            finish(s, v, z);
        else
        {
            z = s->ops->submit(s->port, &s->execution);
            if (!z)
                v->reply.phase = CS_EXECUTING;
            else if (z != SYS_ERR_BUSY && z != SYS_ERR_NO_RESOURCE)
                finish(s, v, z);
        }
        return leave(s, SYS_OK);
    }
    if (!s->exec_seen)
    {
        z = s->ops->read(s->port, &v->reply.id, &r);
        if (z)
        {
            if (!waiting(z))
                first(v, z);
            return leave(s, z);
        }
        if (!CS_IdEqual(&r.id, &v->reply.id) ||
            (r.state != EM_DONE && r.state != EM_BLOCKED && r.state != CS_EXEC_REFUSED) ||
            r.result > 0 || r.recovery > 0 ||
            ((r.state == EM_BLOCKED || r.state == CS_EXEC_REFUSED) && !r.result))
            return leave(s, SYS_ERR_PROTOCOL); /* 丢弃错身份，保留当前事务。 */
        s->exec_result = r;
        s->exec_seen = 1U;
        first(v, r.result); /* 先冻结首错，随后才重试消费。 */
    }
    z = s->ops->consume(s->port, &s->exec_result);
    if (z)
    {
        if (!waiting(z))
            first(v, z);
        return leave(s, z);
    }
    r = s->exec_result;
    if (r.state == EM_BLOCKED)
        s->blocked = 1U;
    if (r.state != CS_EXEC_REFUSED)
        v->reply.effect = CS_EFFECT_EXECUTION;
    v->reply.execution_state = r.state;
    v->reply.recovery_error = r.recovery;
    finish(s, v, r.result);
    return leave(s, SYS_OK);
}
