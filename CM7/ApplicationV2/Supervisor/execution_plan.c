#include "execution_manager.h"
#include <string.h>
static SysResult_t append(EMPlan *p, uint32_t m, uint32_t op, uint32_t from)
{
    if (p->count == EM_STEP_MAX)
        return SYS_ERR_NO_RESOURCE;
    p->steps[p->count].module = m;
    p->steps[p->count].operation = op;
    p->steps[p->count++].expected = from;
    return SYS_OK;
}
SysResult_t EM_BuildPlan(const V2Registry *r, const V2CompositionCandidate *c,
                         const uint32_t *states, uint16_t mask, uint32_t kind, uint32_t cleanup,
                         EMPlan *out)
{
    EMPlan p;
    V2Issue issue;
    V2ResolverScratch scratch;
    V2Resolution verified;
    uint32_t i, j, k, seen = 0, done = 0, ix[V2_MODULE_MAX];
    SysResult_t z;
    if (!r || !c || !states || !out || c->candidate_only != 1 ||
        (kind != EM_START && kind != EM_STOP) || cleanup > 1)
        return SYS_ERR_INVALID_ARG;
    if (V2_Overlap(out, sizeof(*out), c, sizeof(*c)) ||
        V2_Overlap(out, sizeof(*out), r, sizeof(*r)) ||
        V2_Overlap(out, sizeof(*out), states, sizeof(*states) * V2_MODULE_MAX))
        return SYS_ERR_INVALID_ARG;
    z = V2Registry_Validate(r, &issue);
    if (z)
        return z;
    if (!c->modules.count || c->modules.count > r->count || c->modules.selected_mask >> r->count)
        return SYS_ERR_PROTOCOL;
    for (i = 0; i < c->modules.count; i++)
    {
        for (j = 0; j < r->count; j++)
            if (r->modules[j].id.value == c->modules.order[i].value)
                break;
        if (j == r->count || (seen & (1U << j)) || !(c->modules.selected_mask & (1U << j)) ||
            !LC_Stable(states[j]) || (c->modules.dependencies[j] & ~done) ||
            r->modules[j].flags & (V2_DESCRIPTOR_ONLY | V2_MODULE_FUTURE))
            return SYS_ERR_PROTOCOL;
        ix[i] = j;
        seen |= 1U << j;
        done = seen;
    }
    if (seen != c->modules.selected_mask)
        return SYS_ERR_PROTOCOL;
    /* A target candidate cannot hide a live module from stop-dependency checks.
     * Phase8 incremental live/target reconciliation is deliberately absent. */
    for (i = 0; i < r->count; i++)
    {
        if (!LC_Stable(states[i]))
            return SYS_ERR_INVALID_STATE;
        if (!(seen & (1U << i)) && states[i] != LC_UNCONFIGURED)
            return SYS_ERR_BUSY;
    }
    memset(&scratch, 0, sizeof(scratch));
    z = V2Resolver_Resolve(r, c->modules.order, c->modules.count, &scratch, &verified, &issue);
    if (z)
        return z;
    if (memcmp(&verified, &c->modules, sizeof(verified)))
        return SYS_ERR_PROTOCOL;
    if (kind == EM_STOP && (!mask || (mask & ~seen)))
        return SYS_ERR_INVALID_ARG;
    /* Stop dependency closure: a live dependent outside subset pins its provider. */
    if (kind == EM_STOP)
        for (i = 0; i < r->count; i++)
            if ((seen & (1U << i)) && !(mask & (1U << i)) && states[i] != LC_UNCONFIGURED &&
                (c->modules.dependencies[i] & mask))
                return SYS_ERR_BUSY;
    for (i = 0; i < r->count; i++)
        if ((seen & (1U << i)) && states[i] == LC_ACTIVE)
            for (j = 0; j < r->count; j++)
                if ((c->modules.dependencies[i] & (1U << j)) && states[j] != LC_ACTIVE)
                    return SYS_ERR_INVALID_STATE;
    memset(&p, 0, sizeof(p));
    p.kind = kind;
    if (kind == EM_START)
    {
        for (i = 0; i < c->modules.count; i++)
        {
            j = ix[i];
            if (states[j] == LC_ERROR)
                return SYS_ERR_INVALID_STATE;
            if (states[j] == LC_UNCONFIGURED)
            {
                z = append(&p, r->modules[j].id.value, LC_CONFIGURE, LC_UNCONFIGURED);
                if (z)
                    return z;
            }
        }
        for (i = 0; i < c->modules.count; i++)
        {
            j = ix[i];
            if (states[j] != LC_ACTIVE)
            {
                z = append(&p, r->modules[j].id.value, LC_ACTIVATE, LC_INACTIVE);
                if (z)
                    return z;
            }
        }
    }
    else
    {
        for (k = c->modules.count; k > 0; k--)
        {
            j = ix[k - 1];
            if (!(mask & (1U << j)))
                continue;
            if (states[j] == LC_ERROR)
                return SYS_ERR_INVALID_STATE;
            if (states[j] == LC_ACTIVE)
            {
                z = append(&p, r->modules[j].id.value, LC_DEACTIVATE, LC_ACTIVE);
                if (z)
                    return z;
            }
        }
        if (cleanup)
            for (k = c->modules.count; k > 0; k--)
            {
                j = ix[k - 1];
                if ((mask & (1U << j)) && states[j] != LC_UNCONFIGURED)
                {
                    z = append(&p, r->modules[j].id.value, LC_CLEANUP, LC_INACTIVE);
                    if (z)
                        return z;
                }
            }
    }
    *out = p;
    return SYS_OK;
}
