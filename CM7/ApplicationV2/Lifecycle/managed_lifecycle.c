#include "managed_lifecycle.h"
#include <string.h>
#define MAGIC 0x4C435633U
int LC_IdEqual(const V2ExecId *a, const V2ExecId *b) { return a && b && !memcmp(a, b, sizeof(*a)); }
int LC_Stable(uint32_t s) { return s >= LC_UNCONFIGURED && s <= LC_ERROR; }
uint32_t LC_Target(uint32_t op)
{
    switch (op)
    {
    case LC_CONFIGURE:
    case LC_DEACTIVATE:
        return LC_INACTIVE;
    case LC_ACTIVATE:
        return LC_ACTIVE;
    case LC_CLEANUP:
    case LC_ERROR_PROCESS:
        return LC_UNCONFIGURED;
    default:
        return 0;
    }
}
static int legal(uint32_t s, uint32_t op)
{
    return (op == LC_CONFIGURE && s == LC_UNCONFIGURED) ||
           (op == LC_ACTIVATE && s == LC_INACTIVE) || (op == LC_DEACTIVATE && s == LC_ACTIVE) ||
           (op == LC_CLEANUP && s == LC_INACTIVE) || (op == LC_ERROR_PROCESS && s == LC_ERROR);
}
static SysResult_t enter(LCContext *c)
{
    if (!c || c->magic != MAGIC || !c->ops->is_owner(c->port))
        return SYS_ERR_INVALID_STATE;
    if (c->busy)
        return SYS_ERR_BUSY;
    c->busy = 1;
    return SYS_OK;
}
static SysResult_t leave(LCContext *c, SysResult_t z)
{
    c->busy = 0;
    return z;
}
static void error(LCContext *c, SysResult_t z)
{
    if (z && !c->first_error)
        c->first_error = z;
}
SysResult_t LC_Init(LCContext *c, uint32_t module, uint32_t session,
                    const struct V2LifecycleOps *ops, void *port)
{
    if (!c || c->magic || !module || !session || !ops || !ops->is_owner || !ops->can_begin ||
        !ops->begin || !ops->prove)
        return SYS_ERR_INVALID_ARG;
    if (!ops->is_owner(port))
        return SYS_ERR_INVALID_STATE;
    memset(c, 0, sizeof(*c));
    c->module = module;
    c->session = session;
    c->state = LC_UNCONFIGURED;
    c->ops = ops;
    c->port = port;
    c->magic = MAGIC;
    return SYS_OK;
}
SysResult_t LC_Request(LCContext *c, const LCRequest *r, uint32_t now)
{
    SysResult_t z;
    if (!r || !r->id.request || !LC_Target(r->id.operation) || !r->timeout ||
        r->timeout > 0x7FFFFFFFU)
        return SYS_ERR_INVALID_ARG;
    z = enter(c);
    if (z)
        return z;
    if (r->id.module != c->module || r->id.session != c->session)
        return leave(c, SYS_ERR_PROTOCOL);
    if (r->id.request == c->request.id.request)
        return leave(c, !memcmp(r, &c->request, sizeof(*r)) ? SYS_OK : SYS_ERR_PROTOCOL);
    if (r->id.request < c->request.id.request)
        return leave(c, SYS_ERR_PROTOCOL);
    if (c->pending)
        return leave(c, SYS_ERR_BUSY);
    if ((c->sealed && (r->id.operation == LC_ACTIVATE || r->id.operation == LC_CONFIGURE)) ||
        r->expected != c->state || !legal(c->state, r->id.operation))
        return leave(c, SYS_ERR_INVALID_STATE);
    c->request = *r;
    c->started = now;
    c->pending = 1;
    c->dispatched = 0;
    c->decided = 0;
    c->terminal = 0;
    c->first_error = 0;
    c->guard_error = 0;
    memset(&c->completion, 0, sizeof(c->completion));
    c->completion.id = r->id;
    c->state = LC_TRANSITION_BASE + r->id.operation;
    return leave(c, SYS_OK);
}
SysResult_t LC_Complete(LCContext *c, const V2ExecId *id, SysResult_t actual, uint32_t fact)
{
    SysResult_t z = enter(c);
    if (z)
        return z;
    if (actual > 0 || !LC_Stable(fact) || !LC_IdEqual(id, &c->request.id))
        return leave(c, SYS_ERR_PROTOCOL);
    if (c->decided)
        return leave(c, c->completion.actual_result == actual && c->completion.fact == fact
                            ? SYS_OK
                            : SYS_ERR_PROTOCOL);
    if (!c->pending || !c->dispatched)
        return leave(c, SYS_ERR_INVALID_STATE);
    if ((!actual && fact != LC_Target(id->operation)) ||
        (actual && fact != c->request.expected && fact != LC_Target(id->operation) &&
         fact != LC_ERROR))
        return leave(c, SYS_ERR_PROTOCOL);
    c->completion.actual_result = actual;
    c->completion.fact = fact;
    c->decided = 1;
    error(c, actual);
    return leave(c, SYS_OK);
}
SysResult_t LC_Process(LCContext *c, uint32_t now)
{
    SysResult_t z = enter(c);
    if (z)
        return z;
    if (!c->pending || c->terminal)
        return leave(c, SYS_OK);
    if ((uint32_t)(now - c->started) >= c->request.timeout)
        error(c, SYS_ERR_TIMEOUT);
    if (!c->dispatched && !c->decided)
    {
        if (c->first_error == SYS_ERR_TIMEOUT)
        {
            c->completion.actual_result = SYS_ERR_TIMEOUT;
            c->completion.fact = c->request.expected;
            c->decided = 1;
        }
        else
        {
            z = c->ops->can_begin(c->port, &c->request);
            if (!z)
                z = c->ops->begin(c->port, &c->request);
            if (z == SYS_ERR_BUSY || z == SYS_ERR_NO_RESOURCE)
                return leave(c, z);
            if (!z)
            {
                c->dispatched = 1;
                return leave(c, SYS_OK);
            }
            error(c, z);
            c->completion.actual_result = z;
            c->completion.fact = c->request.expected;
            c->decided = 1;
        }
    }
    if (!c->decided)
        return leave(c, c->first_error ? c->first_error : SYS_ERR_BUSY);
    /* A failed/unknown operation never invents a drain proof. ERROR remains ERROR.
     * Stable non-error facts require authoritative module/resource guards. */
    if (c->completion.fact != LC_ERROR)
    {
        z = c->ops->prove(c->port, &c->request, c->completion.fact);
        if (z)
        {
            c->guard_error = z;
            if (z != SYS_ERR_BUSY && z != SYS_ERR_NO_RESOURCE)
                error(c, z);
            return leave(c, z);
        }
    }
    c->state = c->completion.fact;
    c->completion.result = c->first_error;
    c->terminal = 1;
    return leave(c, SYS_OK);
}
SysResult_t LC_Read(LCContext *c, const V2ExecId *id, LCCompletion *out)
{
    SysResult_t z;
    if (!out || !c || V2_Overlap(c, sizeof(*c), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    z = enter(c);
    if (z)
        return z;
    if (!LC_IdEqual(id, &c->request.id) || !c->terminal)
        return leave(c, SYS_ERR_NOT_FOUND);
    *out = c->completion;
    return leave(c, SYS_OK);
}
SysResult_t LC_Confirm(LCContext *c, const LCCompletion *v)
{
    SysResult_t z = enter(c);
    if (z)
        return z;
    if (!v || !c->terminal || memcmp(v, &c->completion, sizeof(*v)))
        return leave(c, SYS_ERR_PROTOCOL);
    c->pending = 0;
    return leave(c, SYS_OK);
}
SysResult_t LC_Seal(LCContext *c)
{
    SysResult_t z = enter(c);
    if (z)
        return z;
    c->sealed = 1;
    return leave(c, SYS_OK);
}
SysResult_t LC_Status(LCContext *c, uint32_t *state, SysResult_t *first)
{
    SysResult_t z;
    if (!state || !first || !c || V2_Overlap(c, sizeof(*c), state, sizeof(*state)) ||
        V2_Overlap(c, sizeof(*c), first, sizeof(*first)))
        return SYS_ERR_INVALID_ARG;
    z = enter(c);
    if (z)
        return z;
    *state = c->state;
    *first = c->first_error;
    return leave(c, SYS_OK);
}
