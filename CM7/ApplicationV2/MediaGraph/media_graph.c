#include "media_graph.h"
#include <string.h>
static SysResult_t fail(GraphIssue *e, uint32_t code, SysResult_t r, uint32_t entity,
                        uint32_t field)
{
    memset(e, 0, sizeof(*e));
    e->code = code;
    e->entity.value = (uint16_t)entity;
    e->field = field;
    e->backend_result = r;
    return r;
}
static int power(uint32_t n) { return n && !(n & (n - 1U)); }
static uint32_t gcd(uint32_t a, uint32_t b)
{
    uint32_t n = 0;
    while (b && n++ < 64)
    {
        uint32_t c = a % b;
        a = b;
        b = c;
    }
    return a;
}
const GraphEntityDesc *Graph_GetEntity(const GraphDesc *g, GraphEntityId id)
{
    return g && g->entities && g->entity_count <= 16 && id.value && id.value <= g->entity_count
               ? &g->entities[id.value - 1]
               : NULL;
}
const GraphLinkDesc *Graph_GetLink(const GraphDesc *g, GraphLinkId id)
{
    return g && g->links && g->link_count <= 32 && id.value && id.value <= g->link_count
               ? &g->links[id.value - 1]
               : NULL;
}
const GraphPipelineDesc *Graph_GetPipeline(const GraphDesc *g, GraphPipelineId id)
{
    return g && g->pipelines && g->pipeline_count <= 8 && id.value && id.value <= g->pipeline_count
               ? &g->pipelines[id.value - 1]
               : NULL;
}
static const GraphPadDesc *pad(const GraphDesc *g, GraphEndpoint x)
{
    const GraphEntityDesc *e = Graph_GetEntity(g, x.entity);
    if (!e || x.pad.value >= e->pad_count || e->pad_first + x.pad.value >= g->pad_count)
        return NULL;
    return &g->pads[e->pad_first + x.pad.value];
}
static int bounds(const GraphDesc *g)
{
    return g && g->schema_version == V2_INTERFACE_VERSION && g->check_context &&
           g->check_context->schema_version == V2_INTERFACE_VERSION && !g->reserved &&
           g->entities && g->pads && g->links && g->pipelines && g->caps && g->entity_count &&
           g->entity_count <= 16 && g->pad_count && g->pad_count <= 32 && g->link_count &&
           g->link_count <= 32 && g->pipeline_count && g->pipeline_count <= 8 && g->caps_count &&
           g->caps_count <= 32;
}
static int alias(const GraphDesc *g, const void *p, size_t n)
{
    uint32_t i;
    if (V2_Overlap(p, n, g, sizeof(*g)) ||
        V2_Overlap(p, n, g->entities, g->entity_count * sizeof(*g->entities)) ||
        V2_Overlap(p, n, g->pads, g->pad_count * sizeof(*g->pads)) ||
        V2_Overlap(p, n, g->links, g->link_count * sizeof(*g->links)) ||
        V2_Overlap(p, n, g->caps, g->caps_count * sizeof(*g->caps)) ||
        V2_Overlap(p, n, g->pipelines, g->pipeline_count * sizeof(*g->pipelines)) ||
        V2_Overlap(p, n, g->check_context, sizeof(*g->check_context)))
        return 1;
    for (i = 0; i < g->pipeline_count; i++)
    {
        const GraphPipelineDesc *d = &g->pipelines[i];
        if (d->sink_count > 5)
            return 1;
        if (V2_Overlap(p, n, d->default_format, sizeof(CoreMediaFormat)) ||
            V2_Overlap(p, n, d->default_sink_rates, d->sink_count * sizeof(GraphFrameRate)))
            return 1;
    }
    return 0;
}
static int capvalid(const GraphCaps *c)
{
    return c->media_mask && !(c->media_mask & ~3U) && c->pixel_mask && !(c->pixel_mask & ~15U) &&
           c->backend_mask && !(c->backend_mask & ~3U) && c->width_min &&
           c->width_min <= c->width_max && c->height_min && c->height_min <= c->height_max &&
           c->width_step && c->height_step && c->max_spans && c->max_spans <= 2 &&
           !(c->flags & ~3U) && c->max_fps_den &&
           ((c->max_fps_num != 0) || ((c->flags & GRAPH_CAP_RATE_UNKNOWN) != 0)) &&
           power(c->dma_alignment) && power(c->frame_alignment);
}
static int accepts(const GraphCaps *c, const GraphFormat *f)
{
    return (c->media_mask & f->media_type) && (c->pixel_mask & (1U << (f->pixel_format - 1U))) &&
           (c->backend_mask & (1U << (f->backend - 1U))) && f->width >= c->width_min &&
           f->width <= c->width_max && f->height >= c->height_min && f->height <= c->height_max &&
           (f->width - c->width_min) % c->width_step == 0 &&
           (f->height - c->height_min) % c->height_step == 0 &&
           (!c->max_payload || f->max_payload <= c->max_payload) && f->max_spans <= c->max_spans &&
           f->dma_alignment >= c->dma_alignment && f->frame_alignment >= c->frame_alignment;
}
static SysResult_t cycle(const GraphDesc *g, uint32_t em, uint32_t lm, GraphWorkspace *w,
                         GraphEntityId *order, uint16_t *count)
{
    uint32_t i, j, k, n = 0;
    memset(w->indegree, 0, 16);
    memset(w->ready, 0, 16);
    for (i = 0; i < g->link_count; i++)
        if (lm & (1UL << i))
            w->indegree[g->links[i].sink.entity.value - 1]++;
    for (k = 0; k < g->entity_count; k++)
    {
        for (i = 0; i < g->entity_count; i++)
            if ((em & (1UL << i)) && !w->ready[i] && !w->indegree[i])
                break;
        if (i == g->entity_count)
            break;
        w->ready[i] = 1;
        w->entity_order[n] = (uint8_t)(i + 1);
        if (order)
            order[n].value = (uint16_t)(i + 1);
        n++;
        for (j = 0; j < g->link_count; j++)
            if ((lm & (1UL << j)) && g->links[j].source.entity.value == i + 1)
                w->indegree[g->links[j].sink.entity.value - 1]--;
    }
    for (i = 0; i < g->entity_count; i++)
        if ((em & (1UL << i)) && !w->ready[i])
            return SYS_ERR_INVALID_STATE;
    if (count)
        *count = (uint16_t)n;
    return SYS_OK;
}
static SysResult_t shape(const GraphDesc *g, uint32_t lm, uint32_t em, uint16_t source,
                         const GraphEntityId *sinks, uint16_t ns, GraphWorkspace *w, GraphIssue *e)
{
    uint32_t i, j, k, degree;
    uint8_t degrees[GRAPH_PAD_MAX] = {0};
    for (i = 0; i < g->link_count; i++)
        if (lm & (1UL << i))
        {
            const GraphLinkDesc *l = &g->links[i];
            degrees[g->entities[l->source.entity.value - 1].pad_first + l->source.pad.value]++;
            degrees[g->entities[l->sink.entity.value - 1].pad_first + l->sink.pad.value]++;
        }
    memset(w->forward, 0, 16);
    memset(w->reverse, 0, 16);
    w->forward[source - 1] = 1;
    for (i = 0; i < ns; i++)
    {
        uint16_t id = sinks[i].value;
        if (!id || id > g->entity_count || !(g->entities[id - 1].flags & GRAPH_E_TERMINAL))
            return fail(e, GRAPH_ISSUE_PATH, SYS_ERR_INVALID_ARG, id, 0);
        w->reverse[id - 1] = 1;
    }
    for (k = 0; k < g->entity_count; k++)
        for (j = 0; j < g->link_count; j++)
            if (lm & (1UL << j))
            {
                const GraphLinkDesc *l = &g->links[j];
                if (w->forward[l->source.entity.value - 1])
                    w->forward[l->sink.entity.value - 1] = 1;
            }
    for (k = 0; k < g->entity_count; k++)
        for (j = 0; j < g->link_count; j++)
            if (lm & (1UL << j))
            {
                const GraphLinkDesc *l = &g->links[j];
                if (w->reverse[l->sink.entity.value - 1])
                    w->reverse[l->source.entity.value - 1] = 1;
            }
    for (i = 0; i < g->entity_count; i++)
        if (em & (1UL << i))
        {
            if (!w->forward[i] || !w->reverse[i])
                return fail(e, GRAPH_ISSUE_PATH, SYS_ERR_INVALID_ARG, i + 1, 0);
            for (j = 0; j < g->entities[i].pad_count; j++)
            {
                const GraphPadDesc *p = &g->pads[g->entities[i].pad_first + j];
                degree = degrees[g->entities[i].pad_first + j];
                if (degree > p->max_links ||
                    (p->direction == GRAPH_PAD_SINK && (p->flags & GRAPH_P_REQUIRED) && !degree) ||
                    (i + 1 == source && p->direction == GRAPH_PAD_SINK && degree))
                    return fail(e, GRAPH_ISSUE_DEGREE, SYS_ERR_INVALID_ARG, i + 1, j);
                if (p->direction == GRAPH_PAD_SOURCE && degree)
                    for (k = 0; k < ns; k++)
                        if (sinks[k].value == i + 1)
                            return fail(e, GRAPH_ISSUE_PATH, SYS_ERR_INVALID_ARG, i + 1, j);
            }
            for (j = 0; j < i; j++)
                if ((em & (1UL << j)) && g->entities[i].exclusion_group &&
                    g->entities[j].exclusion_group == g->entities[i].exclusion_group)
                    return fail(e, GRAPH_ISSUE_BACKEND, V2_CONFLICT, i + 1, 0);
        }
    if (cycle(g, em, lm, w, NULL, NULL))
        return fail(e, GRAPH_ISSUE_CYCLE, SYS_ERR_INVALID_STATE, 0, 0);
    return SYS_OK;
}
SysResult_t Graph_ValidateCatalog(const GraphDesc *g, GraphWorkspace *w, GraphIssue *e)
{
    uint32_t i, j, k, covered = 0;
    uint8_t done[8] = {0};
    if (!bounds(g) || !w || !e)
        return SYS_ERR_INVALID_ARG;
    if (alias(g, w, sizeof(*w)) || alias(g, e, sizeof(*e)) ||
        V2_Overlap(w, sizeof(*w), e, sizeof(*e)))
        return SYS_ERR_INVALID_ARG;
    for (i = 0; i < g->caps_count; i++)
        if (!capvalid(&g->caps[i]))
            return fail(e, GRAPH_ISSUE_FORMAT, SYS_ERR_INVALID_ARG, 0, i);
    for (i = 0; i < g->entity_count; i++)
    {
        const GraphEntityDesc *x = &g->entities[i];
        uint32_t ins = 0, outs = 0;
        if (x->id.value != i + 1 || x->kind < 1 || x->kind > 6 || !x->name ||
            x->pad_first != covered || !x->pad_count || covered + x->pad_count > g->pad_count ||
            (x->flags & ~7U) || ((x->flags & 3U) != 1 && (x->flags & 3U) != 2))
            return fail(e, GRAPH_ISSUE_ID, SYS_ERR_INVALID_ARG, i + 1, 0);
        for (j = 0; j < 32 && x->name[j]; j++)
        {
        }
        if (j == 32)
            return fail(e, GRAPH_ISSUE_BOUND, SYS_ERR_INVALID_ARG, i + 1, 0);
        if (x->flags & GRAPH_E_IMPLEMENTED)
            if (!x->ops ||
                ((!(x->flags & GRAPH_E_TERMINAL) || x->kind == GRAPH_ENTITY_PROVIDER) &&
                 !x->ops->validate_format) ||
                ((x->flags & GRAPH_E_TERMINAL) && !x->ops->validate_sink_config))
                return fail(e, GRAPH_ISSUE_ADAPTER, SYS_ERR_INVALID_ARG, i + 1, 0);
        for (j = 0; j < x->pad_count; j++)
        {
            const GraphPadDesc *p = &g->pads[covered + j];
            if (p->id.value != j || p->parent.value != i + 1 || p->reserved || p->caps.value == 0 ||
                p->caps.value > g->caps_count || p->protocol < 1 || p->protocol > 5 ||
                p->direction < 1 || p->direction > 2 || p->flags & ~3U || !p->max_links ||
                p->max_links > 32 ||
                (p->direction == GRAPH_PAD_SINK &&
                 ((p->flags & GRAPH_P_FANOUT) || p->max_links != 1)) ||
                (!(p->flags & GRAPH_P_FANOUT) && p->max_links != 1))
                return fail(e, GRAPH_ISSUE_DIRECTION, SYS_ERR_INVALID_ARG, i + 1, j);
            /* Typed internal routes: no implicit image/protocol conversion. */
            if (!(x->flags & GRAPH_E_FUTURE))
            {
                uint16_t expected = p->protocol;
                if (x->kind == GRAPH_ENTITY_CAPTURE)
                    expected = p->direction == GRAPH_PAD_SINK ? GRAPH_PROTO_SENSOR_BYTES
                                                              : GRAPH_PROTO_DMA_CHUNKS;
                else if (x->kind == GRAPH_ENTITY_PROVIDER)
                    expected = p->direction == GRAPH_PAD_SINK ? GRAPH_PROTO_DMA_CHUNKS
                                                              : GRAPH_PROTO_FRAME_VIEW;
                else if (x->kind == GRAPH_ENTITY_PROCESSOR || x->kind == GRAPH_ENTITY_CONSUMER)
                    expected = GRAPH_PROTO_FRAME_VIEW;
                if (p->protocol != expected)
                    return fail(e, GRAPH_ISSUE_LINK, SYS_ERR_NOT_SUPPORTED, i + 1, j);
            }
            if (p->direction == GRAPH_PAD_SINK)
                ins++;
            else
                outs++;
        }
        if (((x->flags & GRAPH_E_TERMINAL) && x->kind != GRAPH_ENTITY_CONSUMER &&
             x->kind != GRAPH_ENTITY_PROVIDER) ||
            (x->kind == GRAPH_ENTITY_CONSUMER && !(x->flags & GRAPH_E_TERMINAL)) || ins > 1 ||
            (!outs && x->kind != GRAPH_ENTITY_CONSUMER) ||
            (x->kind == GRAPH_ENTITY_CONSUMER && (ins != 1 || outs)) ||
            (x->kind == GRAPH_ENTITY_HW && ins))
            return fail(e, GRAPH_ISSUE_DIRECTION, SYS_ERR_INVALID_ARG, i + 1, 0);
        covered += x->pad_count;
    }
    if (covered != g->pad_count)
        return fail(e, GRAPH_ISSUE_BOUND, SYS_ERR_INVALID_ARG, 0, 0);
    for (i = 0; i < g->link_count; i++)
    {
        const GraphLinkDesc *l = &g->links[i];
        const GraphPadDesc *a = pad(g, l->source), *b = pad(g, l->sink);
        if (l->id.value != i + 1 || l->reserved[0] || l->reserved[1] ||
            (l->flags != GRAPH_L_AVAILABLE && l->flags != GRAPH_L_FUTURE) || !a || !b ||
            a->direction != GRAPH_PAD_SOURCE || b->direction != GRAPH_PAD_SINK ||
            a->protocol != b->protocol || l->source.entity.value == l->sink.entity.value)
            return fail(e, GRAPH_ISSUE_LINK, SYS_ERR_INVALID_ARG, 0, i + 1);
        if (!(g->caps[a->caps.value - 1].media_mask & g->caps[b->caps.value - 1].media_mask) ||
            !(g->caps[a->caps.value - 1].pixel_mask & g->caps[b->caps.value - 1].pixel_mask) ||
            !(g->caps[a->caps.value - 1].backend_mask & g->caps[b->caps.value - 1].backend_mask))
            return fail(e, GRAPH_ISSUE_FORMAT, SYS_ERR_NOT_SUPPORTED, 0, i + 1);
        for (j = 0; j < i; j++)
            if (!memcmp(&l->source, &g->links[j].source, sizeof(l->source)) &&
                !memcmp(&l->sink, &g->links[j].sink, sizeof(l->sink)))
                return fail(e, GRAPH_ISSUE_LINK, V2_CONFLICT, 0, i + 1);
    }
    if (cycle(g, (1UL << g->entity_count) - 1U,
              g->link_count == 32 ? UINT32_MAX : (1UL << g->link_count) - 1U, w, NULL, NULL))
        return fail(e, GRAPH_ISSUE_CYCLE, SYS_ERR_INVALID_STATE, 0, 0);
    for (i = 0; i < g->pipeline_count; i++)
    {
        const GraphPipelineDesc *p = &g->pipelines[i];
        uint32_t lm = 0, em = 0;
        if (p->id.value != i + 1 || p->flags & ~1U || !p->name || !p->source.value ||
            p->source.value > g->entity_count || !p->sink_count || p->sink_count > 5 ||
            !p->link_count || p->link_count > 32 || p->dependency_count > 4 || !p->default_format ||
            !p->default_sink_rates || CoreMedia_Validate(p->default_format) ||
            p->backend != p->default_format->backend)
            return fail(e, GRAPH_ISSUE_BOUND, SYS_ERR_INVALID_ARG, 0, i + 1);
        for (j = 0; j < 32 && p->name[j]; j++)
        {
        }
        if (j == 32)
            return fail(e, GRAPH_ISSUE_BOUND, SYS_ERR_INVALID_ARG, 0, i + 1);
        for (j = 0; j < 32; j++)
            if (j < p->link_count)
            {
                uint16_t id = p->links[j].value;
                if (!id || id > g->link_count || (lm & (1UL << (id - 1))))
                    return fail(e, GRAPH_ISSUE_LINK, SYS_ERR_INVALID_ARG, 0, i + 1);
                lm |= 1UL << (id - 1);
                em |= 1UL << (g->links[id - 1].source.entity.value - 1);
                em |= 1UL << (g->links[id - 1].sink.entity.value - 1);
                if (g->links[id - 1].flags == GRAPH_L_FUTURE && !(p->flags & GRAPH_PIPE_TEMPLATE))
                    return fail(e, GRAPH_ISSUE_FUTURE, SYS_ERR_INVALID_ARG, 0, i + 1);
            }
            else if (p->links[j].value)
                return fail(e, GRAPH_ISSUE_BOUND, SYS_ERR_INVALID_ARG, 0, i + 1);
        for (j = 0; j < 5; j++)
            if (j < p->sink_count)
            {
                if (CoreRate_Validate((const CoreFrameRate *)&p->default_sink_rates[j]))
                    return fail(e, GRAPH_ISSUE_POLICY, SYS_ERR_INVALID_ARG, 0, i + 1);
                for (k = 0; k < j; k++)
                    if (p->sinks[j].value == p->sinks[k].value)
                        return fail(e, GRAPH_ISSUE_POLICY, V2_CONFLICT, 0, i + 1);
            }
            else if (p->sinks[j].value)
                return fail(e, GRAPH_ISSUE_BOUND, SYS_ERR_INVALID_ARG, 0, i + 1);
        for (j = 0; j < 4; j++)
            if (j < p->dependency_count)
            {
                uint16_t id = p->dependencies[j].value;
                if (!id || id > g->pipeline_count || id == i + 1)
                    return fail(e, GRAPH_ISSUE_DEPENDENCY, SYS_ERR_INVALID_ARG, 0, i + 1);
                for (k = 0; k < j; k++)
                    if (id == p->dependencies[k].value)
                        return fail(e, GRAPH_ISSUE_DEPENDENCY, V2_CONFLICT, 0, i + 1);
            }
            else if (p->dependencies[j].value)
                return fail(e, GRAPH_ISSUE_DEPENDENCY, SYS_ERR_INVALID_ARG, 0, i + 1);
        for (j = 0; j < g->entity_count; j++)
            if ((em & (1UL << j)) && (g->entities[j].flags & GRAPH_E_FUTURE) &&
                !(p->flags & GRAPH_PIPE_TEMPLATE))
                return fail(e, GRAPH_ISSUE_FUTURE, SYS_ERR_INVALID_ARG, j + 1, i + 1);
        if (shape(g, lm, em, p->source.value, p->sinks, p->sink_count, w, e))
            return e->backend_result;
        for (j = 0; j < p->sink_count; j++)
        {
            const GraphEntityDesc *sink = &g->entities[p->sinks[j].value - 1];
            const GraphCaps *c = &g->caps[g->pads[sink->pad_first].caps.value - 1];
            const GraphFrameRate *r = &p->default_sink_rates[j];
            if (c->max_fps_num &&
                (uint64_t)r->num * c->max_fps_den > (uint64_t)c->max_fps_num * r->den)
                return fail(e, GRAPH_ISSUE_POLICY, SYS_ERR_NOT_SUPPORTED, sink->id.value, i + 1);
        }
        for (j = 0; j < g->pad_count; j++)
            if (em & (1UL << (g->pads[j].parent.value - 1)))
                if (!accepts(&g->caps[g->pads[j].caps.value - 1], p->default_format))
                    return fail(e, GRAPH_ISSUE_FORMAT, SYS_ERR_NOT_SUPPORTED, 0, i + 1);
    }
    for (k = 0; k < g->pipeline_count; k++)
        for (i = 0; i < g->pipeline_count; i++)
            if (!done[i])
            {
                const GraphPipelineDesc *p = &g->pipelines[i];
                int ready = 1;
                for (j = 0; j < p->dependency_count; j++)
                    if (!done[p->dependencies[j].value - 1])
                        ready = 0;
                if (ready)
                    done[i] = 1;
            }
    for (i = 0; i < g->pipeline_count; i++)
        if (!done[i])
            return fail(e, GRAPH_ISSUE_CYCLE, SYS_ERR_INVALID_STATE, 0, i + 1);
    memset(e, 0, sizeof(*e));
    return SYS_OK;
}
static SysResult_t validate_set(const GraphDesc *g, const GraphSelection *sel, uint16_t n,
                                GraphScratch *s, GraphValidatedView *out, GraphIssue *e)
{
    uint32_t i, j, k, source = 0, pm = 0;
    SysResult_t z;
    GraphValidatedView *v;
    if (!bounds(g) || !sel || !n || n > 8 || !s || !out || !e)
        return SYS_ERR_INVALID_ARG;
    if (alias(g, s, sizeof(*s)) || alias(g, out, sizeof(*out)) || alias(g, e, sizeof(*e)) ||
        V2_Overlap(s, sizeof(*s), out, sizeof(*out)) || V2_Overlap(s, sizeof(*s), e, sizeof(*e)) ||
        V2_Overlap(out, sizeof(*out), e, sizeof(*e)) ||
        V2_Overlap(sel, n * sizeof(*sel), out, sizeof(*out)) ||
        V2_Overlap(sel, n * sizeof(*sel), s, sizeof(*s)) ||
        V2_Overlap(sel, n * sizeof(*sel), e, sizeof(*e)))
        return SYS_ERR_INVALID_ARG;
    z = Graph_ValidateCatalog(g, &s->work, e);
    if (z)
        return z;
    memset(&s->candidate, 0, sizeof(s->candidate));
    v = &s->candidate;
    v->graph = g;
    for (i = 0; i < n; i++)
    {
        const GraphPipelineDesc *p = Graph_GetPipeline(g, sel[i].pipeline);
        uint32_t lm = 0, em = 0;
        if (!p || sel[i].reserved || sel[i].reserved_tail ||
            sel[i].sink_policy_count != p->sink_count || CoreMedia_Validate(&sel[i].format))
            return fail(e, GRAPH_ISSUE_FORMAT, SYS_ERR_INVALID_ARG, 0, i);
        if (pm & (1UL << (p->id.value - 1)))
            return fail(e, GRAPH_ISSUE_ID, V2_CONFLICT, 0, i);
        pm |= 1UL << (p->id.value - 1);
        if (p->flags & GRAPH_PIPE_TEMPLATE)
            return fail(e, GRAPH_ISSUE_FUTURE, SYS_ERR_NOT_SUPPORTED, 0, i);
        if (i &&
            (source != p->source.value || memcmp(&v->format, &sel[i].format, sizeof(v->format))))
            return fail(e, GRAPH_ISSUE_FORMAT, V2_CONFLICT, 0, i);
        if (sel[i].format.backend != p->backend)
            return fail(e, GRAPH_ISSUE_BACKEND, V2_CONFLICT, 0, i);
        source = p->source.value;
        v->format = sel[i].format;
        for (j = 0; j < p->link_count; j++)
        {
            uint16_t id = p->links[j].value;
            lm |= 1UL << (id - 1);
            em |= 1UL << (g->links[id - 1].source.entity.value - 1);
            em |= 1UL << (g->links[id - 1].sink.entity.value - 1);
        }
        z = shape(g, lm, em, (uint16_t)source, p->sinks, p->sink_count, &s->work, e);
        if (z)
            return z;
        v->link_mask |= lm;
        v->entity_mask |= em;
        for (j = 0; j < p->sink_count; j++)
        {
            const GraphSinkPolicy *q = NULL;
            GraphFrameRate rate;
            uint32_t d;
            for (k = 0; k < sel[i].sink_policy_count; k++)
                if (sel[i].sink_policies[k].sink.value == p->sinks[j].value)
                {
                    if (q)
                        return fail(e, GRAPH_ISSUE_POLICY, V2_CONFLICT, 0, i);
                    q = &sel[i].sink_policies[k];
                }
            if (!q || q->reserved || !q->rate.num || !q->rate.den)
                return fail(e, GRAPH_ISSUE_POLICY, SYS_ERR_INVALID_ARG, 0, i);
            rate = q->rate;
            d = gcd(rate.num, rate.den);
            rate.num /= d;
            rate.den /= d;
            for (k = 0; k < v->sink_count; k++)
                if (v->sinks[k].sink.value == q->sink.value)
                    break;
            if (k < v->sink_count)
            {
                if (v->sinks[k].rate.num != rate.num || v->sinks[k].rate.den != rate.den)
                    return fail(e, GRAPH_ISSUE_POLICY, V2_CONFLICT, 0, i);
            }
            else
            {
                if (k == 5)
                    return fail(e, GRAPH_ISSUE_BOUND, SYS_ERR_NO_RESOURCE, 0, i);
                v->sinks[k] = *q;
                v->sinks[k].rate = rate;
                v->sink_count++;
            }
        }
    }
    for (i = 0; i < n; i++)
    {
        const GraphPipelineDesc *p = Graph_GetPipeline(g, sel[i].pipeline);
        for (j = 0; j < p->dependency_count; j++)
            if (!(pm & (1UL << (p->dependencies[j].value - 1))))
                return fail(e, GRAPH_ISSUE_DEPENDENCY, SYS_ERR_INVALID_ARG, 0, i);
    }
    {
        GraphEntityId sinks[5];
        for (i = 0; i < v->sink_count; i++)
            sinks[i] = v->sinks[i].sink;
        z = shape(g, v->link_mask, v->entity_mask, (uint16_t)source, sinks, v->sink_count, &s->work,
                  e);
        if (z)
            return z;
    }
    for (i = 0; i < g->entity_count; i++)
        if (v->entity_mask & (1UL << i))
        {
            const GraphEntityDesc *x = &g->entities[i];
            if (x->flags & GRAPH_E_FUTURE)
                return fail(e, GRAPH_ISSUE_FUTURE, SYS_ERR_NOT_SUPPORTED, i + 1, 0);
            for (j = 0; j < x->pad_count; j++)
                if (!accepts(&g->caps[g->pads[x->pad_first + j].caps.value - 1], &v->format))
                    return fail(e, GRAPH_ISSUE_FORMAT, SYS_ERR_NOT_SUPPORTED, i + 1, j);
            if (x->ops->validate_format)
            {
                memset(e, 0, sizeof(*e));
                z = x->ops->validate_format(x, &v->format, g->check_context, e);
                if (z)
                {
                    if (!e->code)
                        e->code = GRAPH_ISSUE_ADAPTER;
                    e->entity.value = (uint16_t)(i + 1);
                    e->backend_result = z;
                    return z;
                }
            }
        }
    /* Sink order is stable regardless of request order. */
    for (i = 0; i < v->sink_count; i++)
        for (j = i + 1; j < v->sink_count; j++)
            if (v->sinks[j].sink.value < v->sinks[i].sink.value)
            {
                GraphSinkPolicy q = v->sinks[i];
                v->sinks[i] = v->sinks[j];
                v->sinks[j] = q;
            }
    for (i = 0; i < v->sink_count; i++)
    {
        const GraphEntityDesc *x = &g->entities[v->sinks[i].sink.value - 1];
        const GraphCaps *c = &g->caps[g->pads[x->pad_first].caps.value - 1];
        GraphFrameRate *r = &v->sinks[i].rate;
        if (c->max_fps_num && (uint64_t)r->num * c->max_fps_den > (uint64_t)c->max_fps_num * r->den)
            return fail(e, GRAPH_ISSUE_POLICY, SYS_ERR_NOT_SUPPORTED, x->id.value, 0);
        memset(e, 0, sizeof(*e));
        z = x->ops->validate_sink_config(x, &v->format, r, g->check_context, e);
        if (z)
        {
            if (!e->code)
                e->code = GRAPH_ISSUE_ADAPTER;
            e->entity = x->id;
            e->backend_result = z;
            return z;
        }
    }
    /* shape already performed the merged Kahn traversal. Reuse its order;
     * do not add a nineteenth traversal beyond the reviewed helper budget. */
    for (i = 0; i < g->entity_count; i++)
        if (v->entity_mask & (1UL << i))
            v->topo_count++;
    for (i = 0; i < v->topo_count; i++)
        v->topo[i].value = s->work.entity_order[i];
    v->pipeline_mask = pm;
    *out = *v;
    memset(e, 0, sizeof(*e));
    return SYS_OK;
}

SysResult_t Graph_ValidateSet(const GraphDesc *g, const GraphSelection *sel, uint16_t n,
                              GraphScratch *s, GraphValidatedView *out, GraphIssue *e)
{
    SysResult_t z;
    if (!bounds(g) || !sel || !n || n > 8 || !s || !out || !e)
        return SYS_ERR_INVALID_ARG;
    if (alias(g, s, sizeof(*s)) || V2_Overlap(s, sizeof(*s), out, sizeof(*out)) ||
        V2_Overlap(s, sizeof(*s), e, sizeof(*e)) ||
        V2_Overlap(sel, n * sizeof(*sel), s, sizeof(*s)))
        return SYS_ERR_INVALID_ARG;
    if (s->work.reserved[15])
        return SYS_ERR_BUSY;
    s->work.reserved[15] = 1;
    z = validate_set(g, sel, n, s, out, e);
    s->work.reserved[15] = 0;
    return z;
}
