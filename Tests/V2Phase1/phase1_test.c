#include "../../CM7/ApplicationV2/Configuration/v2_composition.h"
#include "app_messages.h"
#include "app_camera_lifecycle.h"
#include <string.h>
volatile uint32_t g_failure;
volatile uint32_t g_sizes[32];
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            g_failure = __LINE__;                                                                  \
            return __LINE__;                                                                       \
        }                                                                                          \
    } while (0)
static uint32_t veto, calls;
static SysResult_t format_ok(const GraphEntityDesc *e, const GraphFormat *f,
                             const GraphCheckContext *c, GraphIssue *i)
{
    (void)e;
    (void)f;
    (void)c;
    (void)i;
    calls++;
    if (veto)
    {
        i->expected = 123;
        return veto == 2 ? -123 : SYS_ERR_IO;
    }
    return SYS_OK;
}
static SysResult_t sink_ok(const GraphEntityDesc *e, const GraphFormat *f, const GraphFrameRate *r,
                           const GraphCheckContext *c, GraphIssue *i)
{
    (void)c;
    (void)i;
    calls++;
    if (veto)
        return SYS_ERR_IO;
    if (e->id.value == 6 && f->pixel_format != CORE_PIXEL_JPEG)
        return SYS_ERR_NOT_SUPPORTED;
    return r->den == 1 && r->num <= 60 ? SYS_OK : SYS_ERR_NOT_SUPPORTED;
}
static const GraphEntityOps ops = {format_ok, sink_ok};
static const GraphCheckContext context = {1, 1};
static GraphEntityDesc entities[16];
static GraphPadDesc pads[32];
static GraphLinkDesc links[32];
static GraphPipelineDesc pipes[8];
static GraphCaps caps[32];
static GraphFormat jpeg, raw;
static GraphFrameRate rates[1];
static GraphDesc graph;
static GraphSelection selections[8];
static V2Provided provided[32];
static V2Requirement requirements[64];
static V2ModuleDesc modules[16];
static V2Registry registry;
static V2Issue issue;
static GraphIssue gi;
static V2ResolverScratch rs;
static V2Resolution resolution, oldres;
static GraphScratch gs;
static GraphValidatedView view, oldview;
static V2CompositionScratch cs;
static V2CompositionCandidate result, oldresult;
static V2ModuleId roots[16];
static V2Desired desired;
static V2EntityBinding bindings[7];
static uint32_t deny;
static SysResult_t resource(const GraphValidatedView *v, const V2Resolution *r, V2Issue *e)
{
    (void)v;
    (void)r;
    (void)e;
    return deny ? SYS_ERR_BUSY : SYS_OK;
}
static void init(void)
{
    uint32_t i, j, first = 0;
    uint16_t src[] = {1, 2, 3, 4, 4, 2, 7}, dst[] = {2, 3, 4, 5, 6, 7, 4};
    uint16_t p1[] = {1, 2, 3, 4}, p2[] = {1, 2, 3, 5}, p3[] = {1, 6, 7, 4};
    memset(entities, 0, sizeof(entities));
    memset(pads, 0, sizeof(pads));
    memset(links, 0, sizeof(links));
    memset(pipes, 0, sizeof(pipes));
    memset(modules, 0, sizeof(modules));
    memset(requirements, 0, sizeof(requirements));
    memset(selections, 0, sizeof(selections));
    veto = deny = calls = 0;
    rates[0].num = 10;
    rates[0].den = 1;
    jpeg.media_type = 1;
    jpeg.pixel_format = 4;
    jpeg.stream_kind = 2;
    jpeg.width = 320;
    jpeg.height = 240;
    jpeg.bytes_per_line = 0;
    jpeg.size_image = 40000;
    jpeg.max_payload = 40000;
    jpeg.plane_count = 1;
    jpeg.max_spans = 2;
    jpeg.backend = 2;
    jpeg.dma_alignment = 32;
    jpeg.frame_alignment = 1;
    jpeg.flags = 0;
    raw = jpeg;
    raw.pixel_format = 3;
    raw.stream_kind = 1;
    raw.width = 160;
    raw.height = 120;
    raw.bytes_per_line = 320;
    raw.size_image = raw.max_payload = 38400;
    raw.max_spans = 1;
    raw.backend = 1;
    raw.frame_alignment = 32;
    memset(caps, 0, sizeof(caps));
    caps[0].media_mask = 1;
    caps[0].pixel_mask = 15;
    caps[0].backend_mask = 3;
    caps[0].width_min = caps[0].height_min = 1;
    caps[0].width_max = 640;
    caps[0].height_max = 480;
    caps[0].width_step = caps[0].height_step = 1;
    caps[0].max_spans = 2;
    caps[0].flags = 3;
    caps[0].max_fps_den = 1;
    caps[0].dma_alignment = caps[0].frame_alignment = 1;
    caps[1] = caps[0];
    caps[1].pixel_mask = 8;
    caps[1].backend_mask = 2;
    caps[2] = caps[0];
    caps[2].pixel_mask = 7;
    caps[2].backend_mask = 1;
    for (i = 0; i < 7; i++)
    {
        GraphEntityDesc *e = &entities[i];
        e->id.value = (uint16_t)(i + 1);
        e->kind = (uint16_t)(i == 0 ? 1 : i == 1 ? 2 : (i == 2 || i == 6) ? 4 : i == 3 ? 5 : 6);
        e->name = "mock";
        e->pad_first = (uint16_t)first;
        e->pad_count = (uint16_t)(i == 0 || i == 4 || i == 5 ? 1 : 2);
        e->flags = (uint16_t)(1 | (i == 4 || i == 5 ? 4 : 0));
        e->ops = &ops;
        e->exclusion_group = (uint16_t)(i == 2 || i == 6 ? 1 : 0);
        for (j = 0; j < e->pad_count; j++)
        {
            GraphPadDesc *p = &pads[first + j];
            p->id.value = (uint16_t)j;
            p->parent = e->id;
            p->direction = (uint16_t)(i == 0 ? 2 : i == 4 || i == 5 ? 1 : j == 0 ? 1 : 2);
            p->flags = (uint16_t)(p->direction == 1 ? 2 : i == 3 ? 1 : 0);
            p->max_links = (uint16_t)(i == 3 && j == 1 ? 2 : 1);
            p->caps.value = (uint16_t)(i == 2 || i == 5 ? 2 : i == 6 ? 3 : 1);
            p->protocol = (uint16_t)(i == 0 || i == 1 && j == 0                           ? 1
                                     : i == 1 && j == 1 || ((i == 2 || i == 6) && j == 0) ? 3
                                                                                          : 4);
        }
        first += e->pad_count;
    }
    for (i = 0; i < 7; i++)
    {
        links[i].id.value = (uint16_t)(i + 1);
        links[i].flags = 1;
        links[i].source.entity.value = src[i];
        links[i].source.pad.value = (uint16_t)(src[i] == 1 ? 0 : 1);
        links[i].sink.entity.value = dst[i];
    }
    for (i = 0; i < 3; i++)
    {
        GraphPipelineDesc *p = &pipes[i];
        p->id.value = (uint16_t)(i + 1);
        p->name = "pipe";
        p->source.value = 1;
        p->backend = (uint16_t)(i == 2 ? 1 : 2);
        p->sinks[0].value = (uint16_t)(i == 1 ? 6 : 5);
        p->sink_count = 1;
        p->link_count = 4;
        p->default_format = i == 2 ? &raw : &jpeg;
        p->default_sink_rates = rates;
        for (j = 0; j < 4; j++)
            p->links[j].value = i == 0 ? p1[j] : i == 1 ? p2[j] : p3[j];
    }
    graph.schema_version = 1;
    graph.entities = entities;
    graph.pads = pads;
    graph.links = links;
    graph.pipelines = pipes;
    graph.caps = caps;
    graph.entity_count = 7;
    graph.pad_count = 11;
    graph.link_count = 7;
    graph.pipeline_count = 3;
    graph.caps_count = 3;
    graph.reserved = 0;
    graph.check_context = &context;
    for (i = 0; i < 2; i++)
    {
        selections[i].pipeline.value = (uint16_t)(i + 1);
        selections[i].format = jpeg;
        selections[i].sink_policy_count = 1;
        selections[i].sink_policies[0].sink.value = (uint16_t)(5 + i);
        selections[i].sink_policies[0].rate.num = i == 0 ? 10 : 25;
        selections[i].sink_policies[0].rate.den = 1;
    }
    for (i = 0; i < 4; i++)
    {
        provided[i].id.value = 100 + i * 100;
        provided[i].major = 1;
        provided[i].minor = 0;
        modules[i].id.value = 10 + i * 10;
        modules[i].major = 1;
        modules[i].provided = &provided[i];
        modules[i].provided_count = 1;
        modules[i].flags = V2_DESCRIPTOR_ONLY;
        requirements[i].id.value = 100;
        requirements[i].min_major = 1;
        requirements[i].max_major = 1;
        requirements[i].max_minor = 9;
    }
    modules[1].required = &requirements[1];
    modules[1].required_count = 1;
    modules[2].required = &requirements[2];
    modules[2].required_count = 1;
    registry.modules = modules;
    registry.count = 3;
    registry.reserved = 0;
    for (i = 0; i < 7; i++)
    {
        bindings[i].entity.value = (uint16_t)(i + 1);
        bindings[i].reserved = 0;
        bindings[i].module.value = i == 3 ? 0 : i == 4 ? 20 : i == 5 ? 30 : 10;
    }
    roots[0].value = 20;
    roots[1].value = 30;
    roots[2].value = 10;
    desired.roots = roots;
    desired.root_count = 2;
    desired.selections = selections;
    desired.selection_count = 2;
    memset(&view, 0xA5, sizeof(view));
    oldview = view;
    memset(&resolution, 0xA5, sizeof(resolution));
    oldres = resolution;
    memset(&result, 0xA5, sizeof(result));
    oldresult = result;
}
int V2_Phase1Run(uint32_t id)
{
    SysResult_t z;
    uint16_t mask;
    uint32_t i;
    V2CapabilityId conflict = {300};
    CoreFrameRate rate = {10, 1};
    init();
    g_failure = 0;
    if (id == 0)
    {
        g_sizes[0] = sizeof(CoreMediaFormat);
        g_sizes[1] = sizeof(GraphPipelineDesc);
        g_sizes[2] = sizeof(GraphSelection);
        g_sizes[3] = sizeof(GraphValidatedView);
        g_sizes[4] = sizeof(GraphScratch);
        g_sizes[5] = sizeof(V2ModuleDesc);
        g_sizes[6] = sizeof(V2Resolution);
        g_sizes[7] = sizeof(V2ResolverScratch);
        g_sizes[8] = sizeof(V2CompositionCandidate);
        g_sizes[9] = sizeof(V2CompositionScratch);
        g_sizes[10] = sizeof(GraphCaps);
        g_sizes[11] = sizeof(GraphEntityDesc);
        g_sizes[12] = sizeof(GraphPadDesc);
        g_sizes[13] = sizeof(GraphLinkDesc);
        g_sizes[14] = sizeof(GraphDesc);
        g_sizes[15] = sizeof(GraphWorkspace);
        g_sizes[16] = sizeof(V2Provided);
        g_sizes[17] = sizeof(V2Requirement);
        g_sizes[18] = sizeof(V2Registry);
        g_sizes[19] = sizeof(V2Issue);
        g_sizes[20] = sizeof(GraphIssue);
        g_sizes[21] = sizeof(V2EntityBinding);
        g_sizes[22] = sizeof(V2Desired);
        g_sizes[23] = sizeof(GraphEntityOps);
        g_sizes[24] = sizeof(GraphFrameRate);
        g_sizes[25] = sizeof(GraphSinkPolicy);
        g_sizes[26] = sizeof(AppMessage_t);
        g_sizes[27] = sizeof(AppLifecycleMessage_t);
        g_sizes[28] = sizeof(CameraFrame_t);
        g_sizes[29] = sizeof(V2Status);
        CHECK(g_sizes[26] == 52 && g_sizes[27] == 56 && g_sizes[29] == 4);
        CHECK(offsetof(AppMessage_t, frame) == 28);
        CHECK(offsetof(AppLifecycleMessage_t, identity) == 20);
        CHECK(g_sizes[0] == 56);
        CHECK(g_sizes[1] == 108);
        CHECK(g_sizes[2] == 124);
        CHECK(g_sizes[3] == 168);
        CHECK(g_sizes[4] == 280);
        return 0;
    }
    if (id <= 18)
    {
        switch (id)
        {
        case 1:
            CHECK(V2_IdValid(1) && !V2_IdValid(0));
            return 0;
        case 2:
            CHECK(V2_SUCCESS == 0 && V2_CONFLICT < 0 && V2_INTERNAL_ERROR < 0);
            return 0;
        case 3:
            CHECK(CoreRate_Validate(&rate) == 0);
            rate.den = 0;
            CHECK(CoreRate_Validate(&rate) != 0);
            return 0;
        case 4:
            CHECK(CoreMedia_Validate(&jpeg) == 0);
            return 0;
        case 5:
            CHECK(CoreMedia_Validate(&raw) == 0);
            return 0;
        case 6:
            raw.pixel_format = 1;
            raw.bytes_per_line = 160;
            raw.size_image = raw.max_payload = 19200;
            CHECK(CoreMedia_Validate(&raw) == 0);
            return 0;
        case 7:
            raw.pixel_format = 2;
            CHECK(CoreMedia_Validate(&raw) == 0);
            return 0;
        case 8:
            raw.width = 159;
            break;
        case 9:
            raw.size_image++;
            break;
        case 10:
            raw.plane_count = 2;
            break;
        case 11:
            raw.backend = 2;
            break;
        case 12:
            raw.dma_alignment = 3;
            break;
        case 13:
            raw.width = UINT32_MAX;
            break;
        case 14:
            raw.pixel_format = 99;
            break;
        case 15:
            raw.max_spans = 2;
            break;
        case 16:
            raw.flags = 1;
            break;
        case 17:
            CHECK(CoreMedia_Validate(NULL) != 0);
            return 0;
        case 18:
            jpeg.max_payload--;
            CHECK(CoreMedia_Validate(&jpeg) != 0);
            return 0;
        }
        CHECK(CoreMedia_Validate(&raw) != 0);
        return 0;
    }
    if (id <= 35)
    {
        switch (id)
        {
        case 19:
            CHECK(V2Registry_Validate(&registry, &issue) == 0);
            return 0;
        case 20:
            CHECK(V2Registry_Get(&registry, roots[0]) == &modules[1]);
            return 0;
        case 21:
            roots[0].value = 999;
            CHECK(V2Registry_Get(&registry, roots[0]) == NULL);
            return 0;
        case 22:
            CHECK(V2Registry_Providers(&registry, &requirements[1], &mask) == 0 && mask == 1);
            return 0;
        case 23:
            modules[1].id = modules[0].id;
            break;
        case 24:
            registry.count = 17;
            break;
        case 25:
            modules[0].id.value = 0;
            break;
        case 26:
            modules[0].provided_count = 33;
            break;
        case 27:
            modules[0].provided = NULL;
            break;
        case 28:
            requirements[1].min_major = 2;
            break;
        case 29:
            provided[0].id.value = 0;
            break;
        case 30:
            registry.reserved = 1;
            break;
        case 31:
            modules[0].flags = 128;
            break;
        case 32:
            modules[0].required_count = 65;
            modules[0].required = requirements;
            break;
        case 33:
            modules[0].optional_count = 33;
            modules[0].optional = requirements;
            break;
        case 34:
            modules[0].conflict_count = 33;
            modules[0].conflicts = &conflict;
            break;
        case 35:
            CHECK(V2Registry_Validate(NULL, &issue) != 0);
            return 0;
        }
        CHECK(V2Registry_Validate(&registry, &issue) != 0);
        return 0;
    }
    if (id <= 52)
    {
        switch (id)
        {
        case 36:
            CHECK(V2Resolver_Resolve(&registry, roots, 1, &rs, &resolution, &issue) == 0 &&
                  resolution.count == 2);
            return 0;
        case 37:
            roots[0].value = 30;
            CHECK(V2Resolver_Resolve(&registry, roots, 1, &rs, &resolution, &issue) == 0 &&
                  resolution.count == 2);
            return 0;
        case 38:
            CHECK(V2Resolver_Resolve(&registry, roots, 2, &rs, &resolution, &issue) == 0 &&
                  resolution.count == 3 && resolution.order[0].value == 10 &&
                  resolution.order[1].value == 20);
            return 0;
        case 39:
            requirements[1].id.value = 999;
            break;
        case 40:
            modules[1].conflicts = &conflict;
            modules[1].conflict_count = 1;
            break;
        case 41:
            modules[0].required = &requirements[0];
            modules[0].required_count = 1;
            requirements[0].id.value = 200;
            break;
        case 42:
            registry.count = 4;
            provided[3] = provided[0];
            break;
        case 43:
            registry.count = 4;
            provided[3] = provided[0];
            requirements[1].preferred.value = 10;
            requirements[2].preferred.value = 10;
            CHECK(V2Resolver_Resolve(&registry, roots, 2, &rs, &resolution, &issue) == 0);
            return 0;
        case 44:
            requirements[1].max_major = 2;
            requirements[1].min_major = 2;
            break;
        case 45:
            modules[1].required_count = 0;
            modules[1].optional = &requirements[1];
            modules[1].optional_count = 1;
            requirements[1].id.value = 999;
            CHECK(V2Resolver_Resolve(&registry, roots, 1, &rs, &resolution, &issue) == 0 &&
                  resolution.optional_missing == 1);
            return 0;
        case 46:
            modules[1].required_count = 0;
            modules[1].optional = &requirements[1];
            modules[1].optional_count = 1;
            requirements[1].flags = 1;
            CHECK(V2Resolver_Resolve(&registry, roots, 1, &rs, &resolution, &issue) == 0 &&
                  resolution.count == 2);
            return 0;
        case 47:
            roots[1] = roots[0];
            break;
        case 48:
            roots[0].value = 999;
            break;
        case 49:
            modules[0].flags |= V2_MODULE_FUTURE;
            break;
        case 50:
            CHECK(V2Resolver_Resolve(&registry, roots, 17, &rs, &resolution, &issue) != 0);
            CHECK(!memcmp(&resolution, &oldres, sizeof(resolution)));
            return 0;
        case 51:
            for (i = 0; i < 100; i++)
                CHECK(V2Resolver_Resolve(&registry, roots, 2, &rs, &resolution, &issue) == 0);
            return 0;
        case 52:
            CHECK(V2Resolver_Resolve(&registry, roots, 2, &rs, (V2Resolution *)&rs, &issue) != 0);
            return 0;
        }
        CHECK(V2Resolver_Resolve(&registry, roots, 2, &rs, &resolution, &issue) != 0);
        CHECK(!memcmp(&resolution, &oldres, sizeof(resolution)));
        return 0;
    }
    if (id <= 88)
    {
        switch (id)
        {
        case 53:
            CHECK(Graph_ValidateCatalog(&graph, &gs.work, &gi) == 0);
            return 0;
        case 54:
            CHECK(Graph_ValidateSet(&graph, selections, 1, &gs, &view, &gi) == 0 &&
                  view.sink_count == 1);
            return 0;
        case 55:
            CHECK(Graph_ValidateSet(&graph, selections, 2, &gs, &view, &gi) == 0 &&
                  view.sink_count == 2);
            return 0;
        case 56:
            selections[0].pipeline.value = 3;
            selections[0].format = raw;
            CHECK(Graph_ValidateSet(&graph, selections, 1, &gs, &view, &gi) == 0);
            return 0;
        case 57:
            entities[0].id.value = 0;
            break;
        case 58:
            pads[0].direction = 1;
            break;
        case 59:
            links[0].source.pad.value = 99;
            break;
        case 60:
            links[0].id.value = 0;
            break;
        case 61:
            pads[1].protocol = 4;
            break;
        case 62:
            pads[6].flags = 0;
            pads[6].max_links = 1;
            break;
        case 63:
            selections[1].format.max_payload++;
            break;
        case 64:
            selections[1].pipeline = selections[0].pipeline;
            break;
        case 65:
            selections[0].sink_policies[0].rate.den = 0;
            break;
        case 66:
            selections[0].sink_policies[0].sink.value = 6;
            break;
        case 67:
            selections[0].sink_policies[0].rate.num = 61;
            break;
        case 68:
            veto = 1;
            break;
        case 69:
            pipes[0].flags = GRAPH_PIPE_TEMPLATE;
            break;
        case 70:
            entities[0].flags = GRAPH_E_FUTURE;
            break;
        case 71:
            caps[0].width_step = 0;
            break;
        case 72:
            pipes[0].source.value = 99;
            break;
        case 73:
            pipes[0].links[0].value = 0;
            break;
        case 74:
            pipes[0].links[3].value = 0;
            break;
        case 75:
            pipes[0].dependencies[0].value = 2;
            pipes[0].dependency_count = 1;
            CHECK(Graph_ValidateSet(&graph, selections, 1, &gs, &view, &gi) != 0);
            return 0;
        case 76:
            pipes[0].dependencies[0].value = 2;
            pipes[0].dependency_count = 1;
            pipes[1].dependencies[0].value = 1;
            pipes[1].dependency_count = 1;
            break;
        case 77:
            graph.entity_count = 17;
            break;
        case 78:
            graph.pad_count = 33;
            break;
        case 79:
            graph.link_count = 33;
            break;
        case 80:
            graph.pipeline_count = 9;
            break;
        case 81:
            graph.caps_count = 33;
            break;
        case 82:
            pads[6].max_links = 0;
            break;
        case 83:
            pads[8].caps.value = 3;
            break;
        case 84:
            selections[0].format = raw;
            break;
        case 85:
            selections[0].reserved = 1;
            break;
        case 86:
            selections[0].sink_policies[0].reserved = 1;
            break;
        case 87:
            CHECK(Graph_ValidateSet(&graph, selections, 2, &gs, (GraphValidatedView *)&gs, &gi) !=
                  0);
            return 0;
        case 88:
            for (i = 0; i < 25; i++)
                CHECK(Graph_ValidateSet(&graph, selections, 2, &gs, &view, &gi) == 0);
            return 0;
        }
        z = Graph_ValidateSet(&graph, selections, 2, &gs, &view, &gi);
        CHECK(z != 0);
        CHECK(!memcmp(&view, &oldview, sizeof(view)));
        return 0;
    }
    if (id == 108)
    {
        gs.work.reserved[15] = 1;
        CHECK(Graph_ValidateSet(&graph, selections, 2, &gs, &view, &gi) == SYS_ERR_BUSY &&
              !memcmp(&view, &oldview, sizeof(view)));
        gs.work.reserved[15] = 0;
        return 0;
    }
    if (id == 109)
    {
        cs.busy = 1;
        CHECK(V2Composition_Validate(&registry, &graph, bindings, 7, &desired, resource, &cs,
                                     &result, &issue, &gi) == SYS_ERR_BUSY &&
              !memcmp(&result, &oldresult, sizeof(result)));
        cs.busy = 0;
        return 0;
    }
    if (id == 110)
    {
        modules[1].optional = &requirements[3];
        modules[1].optional_count = 1;
        requirements[3].id.value = 300;
        CHECK(V2Resolver_Resolve(&registry, roots, 2, &rs, &resolution, &issue) == 0 &&
              resolution.order[1].value == 30 && resolution.order[2].value == 20);
        return 0;
    }
    if (id == 111)
    {
        V2ModuleId m = {1};
        V2CapabilityId c = {0};
        V2ConsumerId u = {1};
        V2RequestId q = {UINT32_MAX};
        CHECK(V2_ModuleIdValid(m) && !V2_CapabilityIdValid(c) && V2_ConsumerIdValid(u) &&
              V2_RequestIdValid(q));
        return 0;
    }
    if (id == 112)
    {
        CHECK(V2_StatusValid(V2_STATUS_PENDING) && !V2_StatusValid((V2Status)99));
        return 0;
    }
    if (id == 113)
    {
        veto = 2;
        CHECK(Graph_ValidateSet(&graph, selections, 1, &gs, &view, &gi) == -123 &&
              gi.backend_result == -123 && gi.expected == 123 &&
              !memcmp(&view, &oldview, sizeof(view)));
        return 0;
    }
    if (id >= 114 && id <= 117)
    {
        uint32_t index = id == 114 ? 2 : id == 115 ? 3 : id == 116 ? 5 : 7;
        pads[index].protocol = GRAPH_PROTO_DCMI_WORDS;
        CHECK(Graph_ValidateCatalog(&graph, &gs.work, &gi) == SYS_ERR_NOT_SUPPORTED);
        CHECK(gi.code == GRAPH_ISSUE_LINK);
        return 0;
    }
    if (id == 118)
    {
        entities[2].flags |= GRAPH_E_TERMINAL;
        pipes[0].sinks[0].value = 3;
        pipes[0].links[2].value = pipes[0].links[3].value = 0;
        pipes[0].link_count = 2;
        selections[0].sink_policies[0].sink.value = 3;
        CHECK(Graph_ValidateSet(&graph, selections, 1, &gs, &view, &gi) == SYS_OK);
        CHECK(view.topo_count == 3 && view.sinks[0].sink.value == 3);
        return 0;
    }
    if (id == 119)
    {
        bindings[4].module.value = 999;
        CHECK(V2Composition_Validate(&registry, &graph, bindings, 7, &desired, resource, &cs,
                                     &result, &issue, &gi) == V2_CONFLICT);
        CHECK(issue.result == V2_CONFLICT && issue.module == 999 && issue.related == 5);
        CHECK(!memcmp(&result, &oldresult, sizeof(result)));
        return 0;
    }
    if (id == 120)
    {
        modules[1].required_count = 0;
        modules[1].optional = &requirements[1];
        modules[1].optional_count = 1;
        requirements[1].id.value = 999;
        requirements[1].flags = V2_OPTIONAL_AUTO;
        CHECK(V2Resolver_Resolve(&registry, roots, 1, &rs, &resolution, &issue) == SYS_OK);
        CHECK(resolution.count == 1 && resolution.optional_missing == 1);
        return 0;
    }
    if (id == 121)
    {
        selections[1].format.size_image++;
        selections[1].format.max_payload++;
        CHECK(CoreMedia_Validate(&selections[1].format) == SYS_OK);
        CHECK(Graph_ValidateSet(&graph, selections, 2, &gs, &view, &gi) == V2_CONFLICT);
        CHECK(!memcmp(&view, &oldview, sizeof(view)));
        return 0;
    }
    if (id == 122)
    {
        selections[1].pipeline.value = 3;
        selections[1].format = raw;
        selections[1].sink_policies[0].sink.value = 5;
        CHECK(Graph_ValidateSet(&graph, selections, 2, &gs, &view, &gi) == V2_CONFLICT);
        CHECK(!memcmp(&view, &oldview, sizeof(view)));
        return 0;
    }
    if (id == 123)
    {
        static const GraphEntityOps sink_only = {0, sink_ok};
        entities[2].flags |= GRAPH_E_TERMINAL;
        entities[2].ops = &sink_only;
        CHECK(Graph_ValidateCatalog(&graph, &gs.work, &gi) == SYS_ERR_INVALID_ARG);
        CHECK(gi.code == GRAPH_ISSUE_ADAPTER);
        return 0;
    }
    if (id == 124)
    {
        caps[0].max_fps_num = 5;
        CHECK(Graph_ValidateCatalog(&graph, &gs.work, &gi) == SYS_ERR_NOT_SUPPORTED);
        CHECK(gi.code == GRAPH_ISSUE_POLICY);
        return 0;
    }
    if (id == 100)
    {
        for (i = 0; i < 16; i++)
        {
            memset(&modules[i], 0, sizeof(modules[i]));
            modules[i].id.value = 16 - i;
            modules[i].major = 1;
            roots[i].value = i + 1;
        }
        registry.count = 16;
        CHECK(V2Resolver_Resolve(&registry, roots, 16, &rs, &resolution, &issue) == 0 &&
              resolution.count == 16);
        for (i = 0; i < 16; i++)
            CHECK(resolution.order[i].value == i + 1);
        return 0;
    }
    if (id == 101)
    {
        for (i = 3; i < 32; i++)
            caps[i] = caps[0];
        graph.caps_count = 32;
        CHECK(Graph_ValidateCatalog(&graph, &gs.work, &gi) == 0);
        return 0;
    }
    if (id == 102)
    {
        for (i = 1; i < 8; i++)
        {
            pipes[i] = pipes[0];
            pipes[i].id.value = (uint16_t)(i + 1);
            selections[i] = selections[0];
            selections[i].pipeline.value = (uint16_t)(i + 1);
        }
        graph.pipeline_count = 8;
        CHECK(Graph_ValidateSet(&graph, selections, 8, &gs, &view, &gi) == 0 &&
              view.pipeline_mask == 255 && view.sink_count == 1);
        return 0;
    }
    if (id == 103)
    {
        uint32_t first = 0, j;
        memset(pads, 0, sizeof(pads));
        memset(entities, 0, sizeof(entities));
        memset(links, 0, sizeof(links));
        for (i = 0; i < 16; i++)
        {
            GraphEntityDesc *e = &entities[i];
            e->id.value = (uint16_t)(i + 1);
            e->kind = (uint16_t)(i == 0 ? 1 : i == 15 ? 6 : 5);
            e->name = "max";
            e->pad_first = (uint16_t)first;
            e->pad_count = (uint16_t)(i == 0 || i == 15 ? 1 : 2);
            e->flags = (uint16_t)(1 | (i == 15 ? 4 : 0));
            e->ops = &ops;
            for (j = 0; j < e->pad_count; j++)
            {
                GraphPadDesc *p = &pads[first + j];
                p->id.value = (uint16_t)j;
                p->parent = e->id;
                p->direction = (uint16_t)(i == 0 ? 2 : j == 0 ? 1 : 2);
                p->protocol = 4;
                p->caps.value = 1;
                p->flags = (uint16_t)(p->direction == 1 ? 2 : 0);
                p->max_links = 1;
            }
            first += e->pad_count;
            if (i < 15)
            {
                links[i].id.value = (uint16_t)(i + 1);
                links[i].flags = 1;
                links[i].source.entity.value = (uint16_t)(i + 1);
                links[i].source.pad.value = (uint16_t)(i == 0 ? 0 : 1);
                links[i].sink.entity.value = (uint16_t)(i + 2);
            }
        }
        graph.entity_count = 16;
        graph.pad_count = 30;
        graph.link_count = 15;
        graph.pipeline_count = 1;
        memset(&pipes[0].links, 0, sizeof(pipes[0].links));
        pipes[0].sinks[0].value = 16;
        pipes[0].link_count = 15;
        for (i = 0; i < 15; i++)
            pipes[0].links[i].value = (uint16_t)(i + 1);
        selections[0].sink_policies[0].sink.value = 16;
        CHECK(Graph_ValidateSet(&graph, selections, 1, &gs, &view, &gi) == 0 &&
              view.topo_count == 16);
        return 0;
    }
    if (id == 104)
    {
        caps[0].max_fps_num = 5;
        caps[0].flags = 2;
        CHECK(Graph_ValidateSet(&graph, selections, 2, &gs, &view, &gi) != 0 &&
              !memcmp(&view, &oldview, sizeof(view)));
        return 0;
    }
    if (id == 105)
    {
        selections[0].sink_policies[0].rate.num = 20;
        selections[0].sink_policies[0].rate.den = 2;
        CHECK(Graph_ValidateSet(&graph, selections, 1, &gs, &view, &gi) == 0 &&
              view.sinks[0].rate.num == 10 && view.sinks[0].rate.den == 1);
        return 0;
    }
    if (id == 106)
    {
        links[0].sink.entity.value = 1;
        CHECK(Graph_ValidateCatalog(&graph, &gs.work, &gi) != 0);
        return 0;
    }
    if (id == 107)
    {
        provided[1].major = 2;
        requirements[0].id = provided[1].id;
        modules[0].required = &requirements[0];
        modules[0].required_count = 1;
        CHECK(V2Resolver_Resolve(&registry, roots, 1, &rs, &resolution, &issue) != 0);
        return 0;
    }
    switch (id)
    {
    case 89:
        desired.root_count = 1;
        desired.selection_count = 1;
        break;
    case 90:
        roots[0].value = 30;
        desired.root_count = 1;
        desired.selections = &selections[1];
        desired.selection_count = 1;
        break;
    case 91:
        break;
    case 92:
        deny = 1;
        break;
    case 93:
        roots[0].value = 999;
        break;
    case 94:
        selections[0].format.width = 0;
        break;
    case 95:
        bindings[4].module.value = 30;
        desired.root_count = 1;
        break;
    case 96:
        CHECK(V2Composition_Validate(&registry, &graph, bindings, 7, &desired, NULL, &cs, &result,
                                     &issue, &gi) != 0);
        return 0;
    case 97:
        bindings[0].entity.value = 2;
        break;
    case 98:
        CHECK(V2Composition_Validate(&registry, &graph, bindings, 7, &desired, resource, &cs,
                                     (V2CompositionCandidate *)&cs, &issue, &gi) != 0);
        return 0;
    case 99:
        for (i = 0; i < 10; i++)
            CHECK(V2Composition_Validate(&registry, &graph, bindings, 7, &desired, resource, &cs,
                                         &result, &issue, &gi) == 0);
        return 0;
    default:
        CHECK(0);
    }
    z = V2Composition_Validate(&registry, &graph, bindings, 7, &desired, resource, &cs, &result,
                               &issue, &gi);
    if (id <= 91)
    {
        CHECK(z == 0 && result.candidate_only == 1);
        CHECK(result.modules.count == (id == 91 ? 3 : 2));
    }
    else
    {
        CHECK(z != 0);
        CHECK(!memcmp(&result, &oldresult, sizeof(result)));
    }
    return 0;
}
