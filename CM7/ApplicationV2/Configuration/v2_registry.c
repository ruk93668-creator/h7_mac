#include "v2_registry.h"
#include <string.h>
static SysResult_t issue(V2Issue *e, SysResult_t r, uint32_t m, uint32_t c, uint32_t x)
{
    if (e)
    {
        e->code = (uint32_t)(-r);
        e->result = r;
        e->module = m;
        e->capability = c;
        e->related = x;
    }
    return r;
}
static uint32_t version(uint16_t a, uint16_t b) { return ((uint32_t)a << 16) | b; }
static int requirement(const V2Requirement *r, int optional)
{
    return r->id.value &&
           version(r->min_major, r->min_minor) <= version(r->max_major, r->max_minor) &&
           !(r->flags & ~(optional ? V2_OPTIONAL_AUTO : 0U));
}
SysResult_t V2Registry_Validate(const V2Registry *r, V2Issue *e)
{
    uint32_t i, j, k, p = 0, q = 0, o = 0, c = 0;
    if (!r || !e || !r->modules || !r->count || r->reserved)
        return SYS_ERR_INVALID_ARG;
    if (r->count > V2_MODULE_MAX)
        return issue(e, SYS_ERR_NO_RESOURCE, 0, 0, 0);
    for (i = 0; i < r->count; i++)
    {
        const V2ModuleDesc *m = &r->modules[i];
        if (!m->id.value || !m->major || m->flags & ~(V2_DESCRIPTOR_ONLY | V2_MODULE_FUTURE) ||
            (m->provided_count && !m->provided) || (m->required_count && !m->required) ||
            (m->optional_count && !m->optional) || (m->conflict_count && !m->conflicts))
            return issue(e, SYS_ERR_INVALID_ARG, m->id.value, 0, 0);
        p += m->provided_count;
        q += m->required_count;
        o += m->optional_count;
        c += m->conflict_count;
        if (p > 32 || q > 64 || o > 32 || c > 32)
            return issue(e, SYS_ERR_NO_RESOURCE, m->id.value, 0, 0);
        for (j = 0; j < i; j++)
            if (m->id.value == r->modules[j].id.value)
                return issue(e, V2_CONFLICT, m->id.value, 0, 0);
        for (j = 0; j < m->provided_count; j++)
        {
            if (!m->provided[j].id.value || !m->provided[j].major)
                return issue(e, SYS_ERR_INVALID_ARG, m->id.value, 0, 0);
            for (k = 0; k < j; k++)
                if (m->provided[k].id.value == m->provided[j].id.value)
                    return issue(e, V2_CONFLICT, m->id.value, m->provided[j].id.value, 0);
        }
        for (j = 0; j < m->required_count; j++)
            if (!requirement(&m->required[j], 0))
                return issue(e, SYS_ERR_INVALID_ARG, m->id.value, 0, 0);
        for (j = 0; j < m->optional_count; j++)
            if (!requirement(&m->optional[j], 1))
                return issue(e, SYS_ERR_INVALID_ARG, m->id.value, 0, 0);
        for (j = 0; j < m->conflict_count; j++)
            if (!m->conflicts[j].value)
                return issue(e, SYS_ERR_INVALID_ARG, m->id.value, 0, 0);
    }
    memset(e, 0, sizeof(*e));
    return SYS_OK;
}
const V2ModuleDesc *V2Registry_Get(const V2Registry *r, V2ModuleId id)
{
    uint32_t i;
    if (!r || !r->modules || r->count > 16 || !id.value)
        return NULL;
    for (i = 0; i < r->count; i++)
        if (r->modules[i].id.value == id.value)
            return &r->modules[i];
    return NULL;
}
static SysResult_t providers(const V2Registry *r, const V2Requirement *q, uint16_t *mask)
{
    uint32_t i, j;
    uint16_t v = 0;
    if (!q || !mask || !requirement(q, 1))
        return SYS_ERR_INVALID_ARG;
    for (i = 0; i < r->count; i++)
        for (j = 0; j < r->modules[i].provided_count; j++)
        {
            const V2Provided *p = &r->modules[i].provided[j];
            uint32_t ver = version(p->major, p->minor);
            if (p->id.value == q->id.value && ver >= version(q->min_major, q->min_minor) &&
                ver <= version(q->max_major, q->max_minor) &&
                !(r->modules[i].flags & V2_MODULE_FUTURE))
                v |= (uint16_t)(1U << i);
        }
    *mask = v;
    return SYS_OK;
}
SysResult_t V2Registry_Providers(const V2Registry *r, const V2Requirement *q, uint16_t *mask)
{
    V2Issue e;
    SysResult_t z = V2Registry_Validate(r, &e);
    if (z)
        return z;
    return providers(r, q, mask);
}
static SysResult_t choose(const V2Registry *r, const V2Requirement *q, uint16_t *provider)
{
    uint16_t mask;
    uint32_t i, n = 0;
    SysResult_t z = providers(r, q, &mask);
    if (z)
        return z;
    if (q->preferred.value)
        for (i = 0; i < r->count; i++)
            if (r->modules[i].id.value == q->preferred.value)
            {
                mask &= (uint16_t)(1U << i);
                break;
            }
    if (q->preferred.value && i == r->count)
        return SYS_ERR_NOT_FOUND;
    for (i = 0; i < r->count; i++)
        if (mask & (1U << i))
        {
            *provider = (uint16_t)i;
            n++;
        }
    return n == 1 ? SYS_OK : n ? V2_CONFLICT : SYS_ERR_NOT_FOUND;
}
SysResult_t V2Resolver_Resolve(const V2Registry *r, const V2ModuleId *roots, uint16_t n,
                               V2ResolverScratch *s, V2Resolution *out, V2Issue *e)
{
    uint32_t i, j, k, round, changed;
    uint16_t p;
    SysResult_t z;
    if (!s || !out || !e || !roots || !n)
        return SYS_ERR_INVALID_ARG;
    if (n > 16)
        return issue(e, SYS_ERR_NO_RESOURCE, 0, 0, 0);
    if (V2_Overlap(s, sizeof(*s), out, sizeof(*out)) ||
        V2_Overlap(e, sizeof(*e), out, sizeof(*out)) || V2_Overlap(e, sizeof(*e), s, sizeof(*s)) ||
        V2_Overlap(roots, n * sizeof(*roots), s, sizeof(*s)) ||
        V2_Overlap(roots, n * sizeof(*roots), out, sizeof(*out)) ||
        V2_Overlap(roots, n * sizeof(*roots), e, sizeof(*e)))
        return SYS_ERR_INVALID_ARG;
    z = V2Registry_Validate(r, e);
    if (z)
        return z;
    memset(s, 0, sizeof(*s));
    for (i = 0; i < n; i++)
    {
        const V2ModuleDesc *m = V2Registry_Get(r, roots[i]);
        if (!m)
            return issue(e, SYS_ERR_NOT_FOUND, roots[i].value, 0, 0);
        if (m->flags & V2_MODULE_FUTURE)
            return issue(e, SYS_ERR_NOT_SUPPORTED, m->id.value, 0, 0);
        for (j = 0; j < i; j++)
            if (roots[j].value == roots[i].value)
                return issue(e, V2_CONFLICT, roots[i].value, 0, 0);
        s->selected[m - r->modules] = 1;
    }
    for (round = 0; round < 16; round++)
    {
        changed = 0;
        for (i = 0; i < r->count; i++)
            if (s->selected[i])
            {
                const V2ModuleDesc *m = &r->modules[i];
                for (j = 0; j < (uint32_t)m->required_count + m->optional_count; j++)
                {
                    int opt = j >= m->required_count;
                    const V2Requirement *q =
                        opt ? &m->optional[j - m->required_count] : &m->required[j];
                    if (opt && !(q->flags & V2_OPTIONAL_AUTO))
                        continue;
                    z = choose(r, q, &p);
                    if (z == SYS_ERR_NOT_FOUND && opt)
                        continue;
                    if (z)
                        return issue(e, z, m->id.value, q->id.value, 0);
                    s->candidate.dependencies[i] |= (uint16_t)(1U << p);
                    if (!s->selected[p])
                    {
                        s->selected[p] = 1;
                        changed = 1;
                    }
                }
            }
        if (!changed)
            break;
    }
    for (i = 0; i < r->count; i++)
        if (s->selected[i])
        {
            const V2ModuleDesc *m = &r->modules[i];
            s->candidate.selected_mask |= (uint16_t)(1U << i);
            for (j = 0; j < m->optional_count; j++)
                if (m->optional[j].flags & V2_OPTIONAL_AUTO)
                {
                    z = choose(r, &m->optional[j], &p);
                    if (z == SYS_ERR_NOT_FOUND)
                        s->candidate.optional_missing++;
                    else if (z)
                        return issue(e, z, m->id.value, m->optional[j].id.value, 0);
                }
                else
                {
                    uint16_t mask;
                    z = providers(r, &m->optional[j], &mask);
                    if (z)
                        return z;
                    for (k = 0; k < r->count; k++)
                        if (!s->selected[k])
                            mask &= (uint16_t)~(1U << k);
                    if (m->optional[j].preferred.value)
                    {
                        uint32_t x;
                        for (x = 0; x < r->count; x++)
                            if (r->modules[x].id.value == m->optional[j].preferred.value)
                                break;
                        mask = x == r->count ? 0U : (uint16_t)(mask & (1U << x));
                    }
                    if (!mask)
                        s->candidate.optional_missing++;
                    else if (mask & (mask - 1U))
                        return issue(e, V2_CONFLICT, m->id.value, m->optional[j].id.value, 0);
                    else
                        s->candidate.dependencies[i] |= mask;
                }
            for (j = 0; j < m->conflict_count; j++)
                for (k = 0; k < r->count; k++)
                    if (k != i && s->selected[k])
                    {
                        uint32_t x;
                        for (x = 0; x < r->modules[k].provided_count; x++)
                            if (m->conflicts[j].value == r->modules[k].provided[x].id.value)
                                return issue(e, V2_CONFLICT, m->id.value, m->conflicts[j].value,
                                             r->modules[k].id.value);
                    }
        }
    for (round = 0; round < r->count; round++)
    {
        uint32_t best = r->count;
        for (i = 0; i < r->count; i++)
            if (s->selected[i] && !s->done[i])
            {
                int ready = 1;
                for (j = 0; j < r->count; j++)
                    if ((s->candidate.dependencies[i] & (1U << j)) && !s->done[j])
                        ready = 0;
                if (ready &&
                    (best == r->count || r->modules[i].id.value < r->modules[best].id.value))
                    best = i;
            }
        if (best == r->count)
            break;
        s->done[best] = 1;
        s->candidate.order[s->candidate.count++] = r->modules[best].id;
    }
    for (i = 0; i < r->count; i++)
        if (s->selected[i] && !s->done[i])
            return issue(e, SYS_ERR_INVALID_STATE, r->modules[i].id.value, 0, 0);
    *out = s->candidate;
    memset(e, 0, sizeof(*e));
    return SYS_OK;
}
