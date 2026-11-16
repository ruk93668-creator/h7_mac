#include "v2_composition.h"
#include <string.h>
static SysResult_t binding_error(V2Issue *issue, SysResult_t result, uint32_t module,
                                 uint32_t entity)
{
    memset(issue, 0, sizeof(*issue));
    issue->code = (uint32_t)(-result);
    issue->result = result;
    issue->module = module;
    issue->related = entity;
    return result;
}
static SysResult_t validate_composition(const V2Registry *r, const GraphDesc *g,
                                        const V2EntityBinding *bindings, uint16_t count,
                                        const V2Desired *d, V2ResourceValidator validate,
                                        V2CompositionScratch *s, V2CompositionCandidate *out,
                                        V2Issue *issue, GraphIssue *ge)
{
    uint32_t i, j;
    SysResult_t z;
    if (!r || !g || !bindings || !count || count > 16 || !d || !s || !out || !issue || !ge)
        return SYS_ERR_INVALID_ARG;
    if (V2_Overlap(s, sizeof(*s), out, sizeof(*out)) ||
        V2_Overlap(issue, sizeof(*issue), out, sizeof(*out)) ||
        V2_Overlap(ge, sizeof(*ge), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    z = V2Resolver_Resolve(r, d->roots, d->root_count, &s->resolver, &s->candidate.modules, issue);
    if (z)
        return z;
    z = Graph_ValidateSet(g, d->selections, d->selection_count, &s->graph, &s->candidate.media, ge);
    if (z)
        return z;
    for (i = 0; i < count; i++)
    {
        if (bindings[i].reserved || !Graph_GetEntity(g, bindings[i].entity))
            return binding_error(issue, SYS_ERR_INVALID_ARG, bindings[i].module.value,
                                 bindings[i].entity.value);
        for (j = 0; j < i; j++)
            if (bindings[j].entity.value == bindings[i].entity.value)
                return binding_error(issue, V2_CONFLICT, bindings[i].module.value,
                                     bindings[i].entity.value);
    }
    for (i = 0; i < g->entity_count; i++)
        if (s->candidate.media.entity_mask & (1UL << i))
        {
            const V2ModuleDesc *m;
            for (j = 0; j < count; j++)
                if (bindings[j].entity.value == i + 1)
                    break;
            if (j == count)
                return binding_error(issue, SYS_ERR_NOT_FOUND, 0, i + 1);
            if (!bindings[j].module.value)
            {
                if (g->entities[i].kind != GRAPH_ENTITY_PROCESSOR)
                    return binding_error(issue, SYS_ERR_INVALID_ARG, 0, i + 1);
                continue;
            }
            m = V2Registry_Get(r, bindings[j].module);
            if (!m || !(s->candidate.modules.selected_mask & (1UL << (m - r->modules))))
                return binding_error(issue, V2_CONFLICT, bindings[j].module.value, i + 1);
        }
    if (!validate)
        return binding_error(issue, SYS_ERR_NOT_SUPPORTED, 0, 0);
    z = validate(&s->candidate.media, &s->candidate.modules, issue);
    if (z)
    {
        if (!issue->result)
            issue->result = z;
        return z;
    }
    s->candidate.candidate_only = 1;
    *out = s->candidate;
    memset(issue, 0, sizeof(*issue));
    return SYS_OK;
}

SysResult_t V2Composition_Validate(const V2Registry *r, const GraphDesc *g,
                                   const V2EntityBinding *b, uint16_t count, const V2Desired *d,
                                   V2ResourceValidator validate, V2CompositionScratch *s,
                                   V2CompositionCandidate *out, V2Issue *issue, GraphIssue *ge)
{
    SysResult_t z;
    if (!s || !out || !issue || !ge || !d || !b || count > 16)
        return SYS_ERR_INVALID_ARG;
    if (V2_Overlap(s, sizeof(*s), out, sizeof(*out)) ||
        V2_Overlap(s, sizeof(*s), issue, sizeof(*issue)) ||
        V2_Overlap(s, sizeof(*s), ge, sizeof(*ge)) || V2_Overlap(s, sizeof(*s), d, sizeof(*d)) ||
        V2_Overlap(s, sizeof(*s), b, count * sizeof(*b)))
        return SYS_ERR_INVALID_ARG;
    if (s->busy)
        return SYS_ERR_BUSY;
    s->busy = 1;
    z = validate_composition(r, g, b, count, d, validate, s, out, issue, ge);
    s->busy = 0;
    return z;
}
