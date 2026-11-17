#include "execution_manager.h"
#include <string.h>
#define MAGIC 0x454D5633U
static SysResult_t enter(EMContext *c)
{
    if (!c || c->magic != MAGIC || !c->is_owner(c->owner))
        return SYS_ERR_INVALID_STATE;
    if (c->busy)
        return SYS_ERR_BUSY;
    c->busy = 1;
    return SYS_OK;
}
static SysResult_t leave(EMContext *c, SysResult_t z)
{
    c->busy = 0;
    return z;
}
static void first(EMContext *c, SysResult_t z)
{
    if (z && !c->first_error)
        c->first_error = z;
}
static EMEndpoint *port(EMContext *c, uint32_t m)
{
    uint32_t i;
    for (i = 0; i < c->endpoint_count; i++)
        if (c->endpoints[i].module == m)
            return &c->endpoints[i];
    return NULL;
}
static void fail(EMContext *c, SysResult_t z)
{
    first(c, z);
    if (c->state == EM_COMPENSATING)
    {
        if (!c->recovery_error)
            c->recovery_error = z;
        c->state = EM_BLOCKED;
    }
    else if (c->plan.kind == EM_START && !c->blocked_unknown)
    {
        c->state = EM_COMPENSATING;
        c->stage = 0;
    }
    else
        c->state = EM_BLOCKED;
}
SysResult_t EM_Init(EMContext *c, uint32_t session, const EMEndpoint *e, uint32_t n,
                    int (*owner)(void *), void *ctx)
{
    uint32_t i, j;
    if (!c || c->magic || !session || !e || !n || n > V2_MODULE_MAX || !owner)
        return SYS_ERR_INVALID_ARG;
    if (V2_Overlap(c, sizeof(*c), e, sizeof(*e) * n))
        return SYS_ERR_INVALID_ARG;
    if (!owner(ctx))
        return SYS_ERR_INVALID_STATE;
    for (i = 0; i < n; i++)
    {
        if (!e[i].module || !e[i].submit || !e[i].read || !e[i].consume)
            return SYS_ERR_INVALID_ARG;
        for (j = 0; j < i; j++)
            if (e[i].module == e[j].module)
                return SYS_ERR_ALREADY_EXISTS;
    }
    memset(c, 0, sizeof(*c));
    c->session = session;
    c->endpoint_count = n;
    memcpy(c->endpoints, e, n * sizeof(*e));
    c->is_owner = owner;
    c->owner = ctx;
    c->magic = MAGIC;
    return SYS_OK;
}
SysResult_t EM_Load(EMContext *c, const EMPlan *p, uint32_t timeout)
{
    uint32_t i;
    SysResult_t z;
    if (!p || p->count > EM_STEP_MAX || (p->kind != EM_START && p->kind != EM_STOP) || !timeout ||
        timeout > 0x7FFFFFFFU || !c || V2_Overlap(c, sizeof(*c), p, sizeof(*p)))
        return SYS_ERR_INVALID_ARG;
    z = enter(c);
    if (z)
        return z;
    if (c->state != EM_IDLE && c->state != EM_DONE)
        return leave(c, SYS_ERR_BUSY);
    for (i = 0; i < p->count; i++)
        if (!port(c, p->steps[i].module) || !LC_Target(p->steps[i].operation) ||
            !LC_Stable(p->steps[i].expected) ||
            (p->kind == EM_START && p->steps[i].operation != LC_CONFIGURE &&
             p->steps[i].operation != LC_ACTIVATE) ||
            (p->kind == EM_STOP && p->steps[i].operation != LC_DEACTIVATE &&
             p->steps[i].operation != LC_CLEANUP) ||
            (p->steps[i].operation == LC_CONFIGURE && p->steps[i].expected != LC_UNCONFIGURED) ||
            (p->steps[i].operation == LC_ACTIVATE && p->steps[i].expected != LC_INACTIVE) ||
            (p->steps[i].operation == LC_DEACTIVATE && p->steps[i].expected != LC_ACTIVE) ||
            (p->steps[i].operation == LC_CLEANUP && p->steps[i].expected != LC_INACTIVE))
            return leave(c, SYS_ERR_INVALID_ARG);
    c->plan = *p;
    c->timeout = timeout;
    c->index = 0;
    c->stage = 0;
    c->undo_count = 0;
    c->first_error = 0;
    c->recovery_error = 0;
    c->blocked_unknown = 0;
    c->state = EM_RUNNING;
    return leave(c, SYS_OK);
}
static void finish(EMContext *c)
{
    EMStep *s =
        c->state == EM_COMPENSATING ? &c->undo[c->undo_count - 1] : &c->plan.steps[c->index];
    SysResult_t z = c->completion.result;
    uint32_t fact = c->completion.fact;
    if (c->state == EM_RUNNING && c->plan.kind == EM_START && fact == LC_Target(s->operation) &&
        fact != s->expected)
    {
        EMStep *u = &c->undo[c->undo_count++];
        u->module = s->module;
        u->expected = fact;
        u->operation = s->operation == LC_ACTIVATE ? LC_DEACTIVATE : LC_CLEANUP;
    }
    if (fact == LC_ERROR)
        c->blocked_unknown = 1;
    if (z)
    {
        fail(c, z);
        return;
    }
    if (fact != LC_Target(s->operation))
    {
        fail(c, SYS_ERR_PROTOCOL);
        return;
    }
    if (c->state == EM_COMPENSATING)
        c->undo_count--;
    else
        c->index++;
    c->stage = 0;
    if (c->first_error && c->state == EM_RUNNING)
        fail(c, c->first_error);
}
SysResult_t EM_Process(EMContext *c, uint32_t now)
{
    SysResult_t z = enter(c);
    EMEndpoint *e;
    EMStep *s;
    if (z)
        return z;
    if (c->state == EM_IDLE || c->state == EM_DONE)
        return leave(c, SYS_OK);
    if (c->state == EM_BLOCKED)
        return leave(c, c->first_error ? c->first_error : SYS_ERR_INVALID_STATE);
    if (!c->stage)
    {
        if ((c->state == EM_RUNNING && c->index == c->plan.count) ||
            (c->state == EM_COMPENSATING && !c->undo_count))
        {
            c->state = EM_DONE;
            return leave(c, SYS_OK);
        }
        if (c->issued == UINT32_MAX)
        {
            fail(c, SYS_ERR_NO_RESOURCE);
            return leave(c, SYS_ERR_NO_RESOURCE);
        }
        s = c->state == EM_COMPENSATING ? &c->undo[c->undo_count - 1] : &c->plan.steps[c->index];
        memset(&c->current, 0, sizeof(c->current));
        c->current.id.session = c->session;
        c->current.id.request = ++c->issued;
        c->current.id.module = s->module;
        c->current.id.operation = s->operation;
        c->current.expected = s->expected;
        c->current.timeout = c->timeout;
        c->started = now;
        c->stage = 1;
    }
    e = port(c, c->current.id.module);
    if ((uint32_t)(now - c->started) >= c->timeout)
    {
        first(c, SYS_ERR_TIMEOUT);
        if (c->stage == 1)
        {
            c->stage = 0;
            fail(c, SYS_ERR_TIMEOUT);
            return leave(c, SYS_ERR_TIMEOUT);
        }
    }
    if (c->stage == 1)
    {
        z = e->submit(e->port, &c->current);
        if (z == SYS_ERR_BUSY || z == SYS_ERR_NO_RESOURCE)
            return leave(c, z);
        if (z)
        {
            c->stage = 0;
            fail(c, z);
            return leave(c, z);
        }
        c->stage = 2;
        return leave(c, SYS_OK);
    }
    if (c->stage == 2)
    {
        LCCompletion v;
        z = e->read(e->port, &c->current.id, &v);
        if (z == SYS_ERR_NOT_FOUND || z == SYS_ERR_BUSY || z == SYS_ERR_NO_RESOURCE)
            return leave(c, z);
        if (z)
        {
            first(c, z);
            c->state = EM_BLOCKED;
            return leave(c, z);
        }
        if (!LC_IdEqual(&v.id, &c->current.id) || v.result > 0 || v.actual_result > 0 ||
            !LC_Stable(v.fact) ||
            (!v.actual_result && v.fact != LC_Target(c->current.id.operation)) ||
            (v.actual_result && v.fact != c->current.expected &&
             v.fact != LC_Target(c->current.id.operation) && v.fact != LC_ERROR))
        {
            first(c, SYS_ERR_PROTOCOL);
            c->state = EM_BLOCKED;
            return leave(c, SYS_ERR_PROTOCOL);
        }
        c->completion = v;
        c->stage = 3;
        return leave(c, SYS_OK);
    }
    z = e->consume(e->port, &c->completion);
    if (z)
    {
        if (z != SYS_ERR_BUSY && z != SYS_ERR_NO_RESOURCE)
        {
            first(c, z);
            c->state = EM_BLOCKED;
        }
        return leave(c, z);
    }
    finish(c);
    return leave(c, SYS_OK);
}
SysResult_t EM_Status(EMContext *c, uint32_t *state, SysResult_t *err, SysResult_t *recovery)
{
    SysResult_t z;
    if (!c || !state || !err || !recovery || V2_Overlap(c, sizeof(*c), state, sizeof(*state)) ||
        V2_Overlap(c, sizeof(*c), err, sizeof(*err)) ||
        V2_Overlap(c, sizeof(*c), recovery, sizeof(*recovery)))
        return SYS_ERR_INVALID_ARG;
    z = enter(c);
    if (z)
        return z;
    *state = c->state;
    *err = c->first_error;
    *recovery = c->recovery_error;
    return leave(c, SYS_OK);
}
