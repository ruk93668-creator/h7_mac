// 校验静态媒体拓扑和候选路径的格式约束。

#include "media_graph.h"
#include <string.h>
// 记录验证失败及其位置，便于定位错误配置。
static SysResult_t fail(GraphIssue *diagnostic, uint32_t code, SysResult_t result, uint32_t entity,
                        uint32_t field)
{
    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->code = code;
    diagnostic->entity.value = (uint16_t)entity;
    diagnostic->field = field;
    diagnostic->backend_result = result;
    return result;
}
// 计算格式布局或帧率验证所需的约束值。
static int power(uint32_t value)
{
    return value && !(value & (value - 1U));
}
// 计算格式布局或帧率验证所需的约束值。
static uint32_t gcd(uint32_t dividend, uint32_t divisor)
{
    uint32_t iteration = 0;
    while (divisor && iteration++ < 64)
    {
        uint32_t remainder = dividend % divisor;
        dividend = divisor;
        divisor = remainder;
    }
    return dividend;
}
// 按身份查询静态拓扑描述。
const GraphEntityDesc *Graph_GetEntity(const GraphDesc *graph, GraphEntityId id)
{
    return graph && graph->entities && graph->entity_count <= 16 && id.value && id.value <= graph->entity_count
               ? &graph->entities[id.value - 1]
               : NULL;
}
// 按身份查询静态拓扑描述。
const GraphLinkDesc *Graph_GetLink(const GraphDesc *graph, GraphLinkId id)
{
    return graph && graph->links && graph->link_count <= 32 && id.value && id.value <= graph->link_count
               ? &graph->links[id.value - 1]
               : NULL;
}
// 按身份查询静态拓扑描述。
const GraphPipelineDesc *Graph_GetPipeline(const GraphDesc *graph, GraphPipelineId id)
{
    return graph && graph->pipelines && graph->pipeline_count <= 8 && id.value && id.value <= graph->pipeline_count
               ? &graph->pipelines[id.value - 1]
               : NULL;
}
// 定位拓扑端口并检查描述表索引边界。
static const GraphPadDesc *pad(const GraphDesc *graph, GraphEndpoint endpoint)
{
    const GraphEntityDesc *entity = Graph_GetEntity(graph, endpoint.entity);
    if (!entity || endpoint.pad.value >= entity->pad_count || entity->pad_first + endpoint.pad.value >= graph->pad_count)
        return NULL;
    return &graph->pads[entity->pad_first + endpoint.pad.value];
}
// 定位拓扑端口并检查描述表索引边界。
static int bounds(const GraphDesc *graph)
{
    return graph && graph->schema_version == V2_INTERFACE_VERSION && graph->check_context &&
           graph->check_context->schema_version == V2_INTERFACE_VERSION && !graph->reserved &&
           graph->entities && graph->pads && graph->links && graph->pipelines && graph->caps && graph->entity_count &&
           graph->entity_count <= 16 && graph->pad_count && graph->pad_count <= 32 && graph->link_count &&
           graph->link_count <= 32 && graph->pipeline_count && graph->pipeline_count <= 8 && graph->caps_count &&
           graph->caps_count <= 32;
}
// 检查工作区是否与验证输入或输出存在不允许的重叠。
static int alias(const GraphDesc *graph, const void *buffer, size_t bytes)
{
    uint32_t pipeline_index;
    if (V2_Overlap(buffer, bytes, graph, sizeof(*graph)) ||
        V2_Overlap(buffer, bytes, graph->entities, graph->entity_count * sizeof(*graph->entities)) ||
        V2_Overlap(buffer, bytes, graph->pads, graph->pad_count * sizeof(*graph->pads)) ||
        V2_Overlap(buffer, bytes, graph->links, graph->link_count * sizeof(*graph->links)) ||
        V2_Overlap(buffer, bytes, graph->caps, graph->caps_count * sizeof(*graph->caps)) ||
        V2_Overlap(buffer, bytes, graph->pipelines, graph->pipeline_count * sizeof(*graph->pipelines)) ||
        V2_Overlap(buffer, bytes, graph->check_context, sizeof(*graph->check_context)))
        return 1;
    for (pipeline_index = 0; pipeline_index < graph->pipeline_count; pipeline_index++)
    {
        const GraphPipelineDesc *pipeline = &graph->pipelines[pipeline_index];
        if (pipeline->sink_count > 5)
            return 1;
        if (V2_Overlap(buffer, bytes, pipeline->default_format, sizeof(CoreMediaFormat)) ||
            V2_Overlap(buffer, bytes, pipeline->default_sink_rates, pipeline->sink_count * sizeof(GraphFrameRate)))
            return 1;
    }
    return 0;
}
// 校验端口能力及格式是否落在声明范围内。
static int capvalid(const GraphCaps *caps)
{
    return caps->media_mask && !(caps->media_mask & ~3U) && caps->pixel_mask && !(caps->pixel_mask & ~15U) &&
           caps->backend_mask && !(caps->backend_mask & ~3U) && caps->width_min &&
           caps->width_min <= caps->width_max && caps->height_min && caps->height_min <= caps->height_max &&
           caps->width_step && caps->height_step && caps->max_spans && caps->max_spans <= 2 &&
           !(caps->flags & ~3U) && caps->max_fps_den &&
           ((caps->max_fps_num != 0) || ((caps->flags & GRAPH_CAP_RATE_UNKNOWN) != 0)) &&
           power(caps->dma_alignment) && power(caps->frame_alignment);
}
// 校验端口能力及格式是否落在声明范围内。
static int accepts(const GraphCaps *caps, const GraphFormat *format)
{
    return (caps->media_mask & format->media_type) && (caps->pixel_mask & (1U << (format->pixel_format - 1U))) &&
           (caps->backend_mask & (1U << (format->backend - 1U))) && format->width >= caps->width_min &&
           format->width <= caps->width_max && format->height >= caps->height_min && format->height <= caps->height_max &&
           (format->width - caps->width_min) % caps->width_step == 0 &&
           (format->height - caps->height_min) % caps->height_step == 0 &&
           (!caps->max_payload || format->max_payload <= caps->max_payload) && format->max_spans <= caps->max_spans &&
           format->dma_alignment >= caps->dma_alignment && format->frame_alignment >= caps->frame_alignment;
}
// 检查启用路径是否构成依赖环。
static SysResult_t cycle(const GraphDesc *graph, uint32_t em, uint32_t lm, GraphWorkspace *workspace,
                         GraphEntityId *order, uint16_t *count)
{
    uint32_t entity_index, link_index, step_index, ordered_count = 0;
    memset(workspace->indegree, 0, 16);
    memset(workspace->ready, 0, 16);
    for (entity_index = 0; entity_index < graph->link_count; entity_index++)
        if (lm & (1UL << entity_index))
            workspace->indegree[graph->links[entity_index].sink.entity.value - 1]++;
    for (step_index = 0; step_index < graph->entity_count; step_index++)
    {
        for (entity_index = 0; entity_index < graph->entity_count; entity_index++)
            if ((em & (1UL << entity_index)) && !workspace->ready[entity_index] && !workspace->indegree[entity_index])
                break;
        if (entity_index == graph->entity_count)
            break;
        workspace->ready[entity_index] = 1;
        workspace->entity_order[ordered_count] = (uint8_t)(entity_index + 1);
        if (order)
            order[ordered_count].value = (uint16_t)(entity_index + 1);
        ordered_count++;
        for (link_index = 0; link_index < graph->link_count; link_index++)
            if ((lm & (1UL << link_index)) && graph->links[link_index].source.entity.value == entity_index + 1)
                workspace->indegree[graph->links[link_index].sink.entity.value - 1]--;
    }
    for (entity_index = 0; entity_index < graph->entity_count; entity_index++)
        if ((em & (1UL << entity_index)) && !workspace->ready[entity_index])
            return SYS_ERR_INVALID_STATE;
    if (count)
        *count = (uint16_t)ordered_count;
    return SYS_OK;
}
// 检查静态拓扑描述的结构约束。
static SysResult_t shape(const GraphDesc *graph, uint32_t lm, uint32_t em, uint16_t source,
                         const GraphEntityId *sinks, uint16_t ns, GraphWorkspace *workspace, GraphIssue *diagnostic)
{
    uint32_t item_index, link_index, pass_index, degree;
    uint8_t degrees[GRAPH_PAD_MAX] = {0};
    for (item_index = 0; item_index < graph->link_count; item_index++)
        if (lm & (1UL << item_index))
        {
            const GraphLinkDesc *link = &graph->links[item_index];
            degrees[graph->entities[link->source.entity.value - 1].pad_first + link->source.pad.value]++;
            degrees[graph->entities[link->sink.entity.value - 1].pad_first + link->sink.pad.value]++;
        }
    memset(workspace->forward, 0, 16);
    memset(workspace->reverse, 0, 16);
    workspace->forward[source - 1] = 1;
    for (item_index = 0; item_index < ns; item_index++)
    {
        uint16_t id = sinks[item_index].value;
        if (!id || id > graph->entity_count || !(graph->entities[id - 1].flags & GRAPH_E_TERMINAL))
            return fail(diagnostic, GRAPH_ISSUE_PATH, SYS_ERR_INVALID_ARG, id, 0);
        workspace->reverse[id - 1] = 1;
    }
    for (pass_index = 0; pass_index < graph->entity_count; pass_index++)
        for (link_index = 0; link_index < graph->link_count; link_index++)
            if (lm & (1UL << link_index))
            {
                const GraphLinkDesc *link = &graph->links[link_index];
                if (workspace->forward[link->source.entity.value - 1])
                    workspace->forward[link->sink.entity.value - 1] = 1;
            }
    for (pass_index = 0; pass_index < graph->entity_count; pass_index++)
        for (link_index = 0; link_index < graph->link_count; link_index++)
            if (lm & (1UL << link_index))
            {
                const GraphLinkDesc *link = &graph->links[link_index];
                if (workspace->reverse[link->sink.entity.value - 1])
                    workspace->reverse[link->source.entity.value - 1] = 1;
            }
    for (item_index = 0; item_index < graph->entity_count; item_index++)
        if (em & (1UL << item_index))
        {
            if (!workspace->forward[item_index] || !workspace->reverse[item_index])
                return fail(diagnostic, GRAPH_ISSUE_PATH, SYS_ERR_INVALID_ARG, item_index + 1, 0);
            for (link_index = 0; link_index < graph->entities[item_index].pad_count; link_index++)
            {
                const GraphPadDesc *pad_desc = &graph->pads[graph->entities[item_index].pad_first + link_index];
                degree = degrees[graph->entities[item_index].pad_first + link_index];
                if (degree > pad_desc->max_links ||
                    (pad_desc->direction == GRAPH_PAD_SINK && (pad_desc->flags & GRAPH_P_REQUIRED) && !degree) ||
                    (item_index + 1 == source && pad_desc->direction == GRAPH_PAD_SINK && degree))
                    return fail(diagnostic, GRAPH_ISSUE_DEGREE, SYS_ERR_INVALID_ARG, item_index + 1, link_index);
                if (pad_desc->direction == GRAPH_PAD_SOURCE && degree)
                    for (pass_index = 0; pass_index < ns; pass_index++)
                        if (sinks[pass_index].value == item_index + 1)
                            return fail(diagnostic, GRAPH_ISSUE_PATH, SYS_ERR_INVALID_ARG, item_index + 1, link_index);
            }
            for (link_index = 0; link_index < item_index; link_index++)
                if ((em & (1UL << link_index)) && graph->entities[item_index].exclusion_group &&
                    graph->entities[link_index].exclusion_group == graph->entities[item_index].exclusion_group)
                    return fail(diagnostic, GRAPH_ISSUE_BACKEND, V2_CONFLICT, item_index + 1, 0);
        }
    if (cycle(graph, em, lm, workspace, NULL, NULL))
        return fail(diagnostic, GRAPH_ISSUE_CYCLE, SYS_ERR_INVALID_STATE, 0, 0);
    return SYS_OK;
}
// 校验目录结构、端口方向、连接及 Pipeline 描述。
SysResult_t Graph_ValidateCatalog(const GraphDesc *graph, GraphWorkspace *workspace, GraphIssue *diagnostic)
{
    uint32_t item_index, inner_index, dependency_index, covered = 0;
    uint8_t done[8] = {0};
    if (!bounds(graph) || !workspace || !diagnostic)
        return SYS_ERR_INVALID_ARG;
    if (alias(graph, workspace, sizeof(*workspace)) || alias(graph, diagnostic, sizeof(*diagnostic)) ||
        V2_Overlap(workspace, sizeof(*workspace), diagnostic, sizeof(*diagnostic)))
        return SYS_ERR_INVALID_ARG;
    for (item_index = 0; item_index < graph->caps_count; item_index++)
        if (!capvalid(&graph->caps[item_index]))
            return fail(diagnostic, GRAPH_ISSUE_FORMAT, SYS_ERR_INVALID_ARG, 0, item_index);
    for (item_index = 0; item_index < graph->entity_count; item_index++)
    {
        const GraphEntityDesc *entity = &graph->entities[item_index];
        uint32_t ins = 0, outs = 0;
        if (entity->id.value != item_index + 1 || entity->kind < 1 || entity->kind > 6 || !entity->name ||
            entity->pad_first != covered || !entity->pad_count || covered + entity->pad_count > graph->pad_count ||
            (entity->flags & ~7U) || ((entity->flags & 3U) != 1 && (entity->flags & 3U) != 2))
            return fail(diagnostic, GRAPH_ISSUE_ID, SYS_ERR_INVALID_ARG, item_index + 1, 0);
        for (inner_index = 0; inner_index < 32 && entity->name[inner_index]; inner_index++)
        {
        }
        if (inner_index == 32)
            return fail(diagnostic, GRAPH_ISSUE_BOUND, SYS_ERR_INVALID_ARG, item_index + 1, 0);
        if (entity->flags & GRAPH_E_IMPLEMENTED)
            if (!entity->ops ||
                ((!(entity->flags & GRAPH_E_TERMINAL) || entity->kind == GRAPH_ENTITY_PROVIDER) &&
                 !entity->ops->validate_format) ||
                ((entity->flags & GRAPH_E_TERMINAL) && !entity->ops->validate_sink_config))
                return fail(diagnostic, GRAPH_ISSUE_ADAPTER, SYS_ERR_INVALID_ARG, item_index + 1, 0);
        for (inner_index = 0; inner_index < entity->pad_count; inner_index++)
        {
            const GraphPadDesc *pad_desc = &graph->pads[covered + inner_index];
            if (pad_desc->id.value != inner_index || pad_desc->parent.value != item_index + 1 || pad_desc->reserved || pad_desc->caps.value == 0 ||
                pad_desc->caps.value > graph->caps_count || pad_desc->protocol < 1 || pad_desc->protocol > 5 ||
                pad_desc->direction < 1 || pad_desc->direction > 2 || pad_desc->flags & ~3U || !pad_desc->max_links ||
                pad_desc->max_links > 32 ||
                (pad_desc->direction == GRAPH_PAD_SINK &&
                 ((pad_desc->flags & GRAPH_P_FANOUT) || pad_desc->max_links != 1)) ||
                (!(pad_desc->flags & GRAPH_P_FANOUT) && pad_desc->max_links != 1))
                return fail(diagnostic, GRAPH_ISSUE_DIRECTION, SYS_ERR_INVALID_ARG, item_index + 1, inner_index);
            // 内部路线按类型验证，禁止隐式转换图像格式或传输协议。
            if (!(entity->flags & GRAPH_E_FUTURE))
            {
                uint16_t expected = pad_desc->protocol;
                if (entity->kind == GRAPH_ENTITY_CAPTURE)
                    expected = pad_desc->direction == GRAPH_PAD_SINK ? GRAPH_PROTO_SENSOR_BYTES
                                                              : GRAPH_PROTO_DMA_CHUNKS;
                else if (entity->kind == GRAPH_ENTITY_PROVIDER)
                    expected = pad_desc->direction == GRAPH_PAD_SINK ? GRAPH_PROTO_DMA_CHUNKS
                                                              : GRAPH_PROTO_FRAME_VIEW;
                else if (entity->kind == GRAPH_ENTITY_PROCESSOR || entity->kind == GRAPH_ENTITY_CONSUMER)
                    expected = GRAPH_PROTO_FRAME_VIEW;
                if (pad_desc->protocol != expected)
                    return fail(diagnostic, GRAPH_ISSUE_LINK, SYS_ERR_NOT_SUPPORTED, item_index + 1, inner_index);
            }
            if (pad_desc->direction == GRAPH_PAD_SINK)
                ins++;
            else
                outs++;
        }
        if (((entity->flags & GRAPH_E_TERMINAL) && entity->kind != GRAPH_ENTITY_CONSUMER &&
             entity->kind != GRAPH_ENTITY_PROVIDER) ||
            (entity->kind == GRAPH_ENTITY_CONSUMER && !(entity->flags & GRAPH_E_TERMINAL)) || ins > 1 ||
            (!outs && entity->kind != GRAPH_ENTITY_CONSUMER) ||
            (entity->kind == GRAPH_ENTITY_CONSUMER && (ins != 1 || outs)) ||
            (entity->kind == GRAPH_ENTITY_HW && ins))
            return fail(diagnostic, GRAPH_ISSUE_DIRECTION, SYS_ERR_INVALID_ARG, item_index + 1, 0);
        covered += entity->pad_count;
    }
    if (covered != graph->pad_count)
        return fail(diagnostic, GRAPH_ISSUE_BOUND, SYS_ERR_INVALID_ARG, 0, 0);
    for (item_index = 0; item_index < graph->link_count; item_index++)
    {
        const GraphLinkDesc *link = &graph->links[item_index];
        const GraphPadDesc *source_pad = pad(graph, link->source), *sink_pad = pad(graph, link->sink);
        if (link->id.value != item_index + 1 || link->reserved[0] || link->reserved[1] ||
            (link->flags != GRAPH_L_AVAILABLE && link->flags != GRAPH_L_FUTURE) || !source_pad || !sink_pad ||
            source_pad->direction != GRAPH_PAD_SOURCE || sink_pad->direction != GRAPH_PAD_SINK ||
            source_pad->protocol != sink_pad->protocol || link->source.entity.value == link->sink.entity.value)
            return fail(diagnostic, GRAPH_ISSUE_LINK, SYS_ERR_INVALID_ARG, 0, item_index + 1);
        if (!(graph->caps[source_pad->caps.value - 1].media_mask & graph->caps[sink_pad->caps.value - 1].media_mask) ||
            !(graph->caps[source_pad->caps.value - 1].pixel_mask & graph->caps[sink_pad->caps.value - 1].pixel_mask) ||
            !(graph->caps[source_pad->caps.value - 1].backend_mask & graph->caps[sink_pad->caps.value - 1].backend_mask))
            return fail(diagnostic, GRAPH_ISSUE_FORMAT, SYS_ERR_NOT_SUPPORTED, 0, item_index + 1);
        for (inner_index = 0; inner_index < item_index; inner_index++)
            if (!memcmp(&link->source, &graph->links[inner_index].source, sizeof(link->source)) &&
                !memcmp(&link->sink, &graph->links[inner_index].sink, sizeof(link->sink)))
                return fail(diagnostic, GRAPH_ISSUE_LINK, V2_CONFLICT, 0, item_index + 1);
    }
    if (cycle(graph, (1UL << graph->entity_count) - 1U,
              graph->link_count == 32 ? UINT32_MAX : (1UL << graph->link_count) - 1U, workspace, NULL, NULL))
        return fail(diagnostic, GRAPH_ISSUE_CYCLE, SYS_ERR_INVALID_STATE, 0, 0);
    for (item_index = 0; item_index < graph->pipeline_count; item_index++)
    {
        const GraphPipelineDesc *pipeline = &graph->pipelines[item_index];
        uint32_t lm = 0, em = 0;
        if (pipeline->id.value != item_index + 1 || pipeline->flags & ~1U || !pipeline->name || !pipeline->source.value ||
            pipeline->source.value > graph->entity_count || !pipeline->sink_count || pipeline->sink_count > 5 ||
            !pipeline->link_count || pipeline->link_count > 32 || pipeline->dependency_count > 4 || !pipeline->default_format ||
            !pipeline->default_sink_rates || CoreMedia_Validate(pipeline->default_format) ||
            pipeline->backend != pipeline->default_format->backend)
            return fail(diagnostic, GRAPH_ISSUE_BOUND, SYS_ERR_INVALID_ARG, 0, item_index + 1);
        for (inner_index = 0; inner_index < 32 && pipeline->name[inner_index]; inner_index++)
        {
        }
        if (inner_index == 32)
            return fail(diagnostic, GRAPH_ISSUE_BOUND, SYS_ERR_INVALID_ARG, 0, item_index + 1);
        for (inner_index = 0; inner_index < 32; inner_index++)
            if (inner_index < pipeline->link_count)
            {
                uint16_t id = pipeline->links[inner_index].value;
                if (!id || id > graph->link_count || (lm & (1UL << (id - 1))))
                    return fail(diagnostic, GRAPH_ISSUE_LINK, SYS_ERR_INVALID_ARG, 0, item_index + 1);
                lm |= 1UL << (id - 1);
                em |= 1UL << (graph->links[id - 1].source.entity.value - 1);
                em |= 1UL << (graph->links[id - 1].sink.entity.value - 1);
                if (graph->links[id - 1].flags == GRAPH_L_FUTURE && !(pipeline->flags & GRAPH_PIPE_TEMPLATE))
                    return fail(diagnostic, GRAPH_ISSUE_FUTURE, SYS_ERR_INVALID_ARG, 0, item_index + 1);
            }
            else if (pipeline->links[inner_index].value)
                return fail(diagnostic, GRAPH_ISSUE_BOUND, SYS_ERR_INVALID_ARG, 0, item_index + 1);
        for (inner_index = 0; inner_index < 5; inner_index++)
            if (inner_index < pipeline->sink_count)
            {
                if (CoreRate_Validate((const CoreFrameRate *)&pipeline->default_sink_rates[inner_index]))
                    return fail(diagnostic, GRAPH_ISSUE_POLICY, SYS_ERR_INVALID_ARG, 0, item_index + 1);
                for (dependency_index = 0; dependency_index < inner_index; dependency_index++)
                    if (pipeline->sinks[inner_index].value == pipeline->sinks[dependency_index].value)
                        return fail(diagnostic, GRAPH_ISSUE_POLICY, V2_CONFLICT, 0, item_index + 1);
            }
            else if (pipeline->sinks[inner_index].value)
                return fail(diagnostic, GRAPH_ISSUE_BOUND, SYS_ERR_INVALID_ARG, 0, item_index + 1);
        for (inner_index = 0; inner_index < 4; inner_index++)
            if (inner_index < pipeline->dependency_count)
            {
                uint16_t id = pipeline->dependencies[inner_index].value;
                if (!id || id > graph->pipeline_count || id == item_index + 1)
                    return fail(diagnostic, GRAPH_ISSUE_DEPENDENCY, SYS_ERR_INVALID_ARG, 0, item_index + 1);
                for (dependency_index = 0; dependency_index < inner_index; dependency_index++)
                    if (id == pipeline->dependencies[dependency_index].value)
                        return fail(diagnostic, GRAPH_ISSUE_DEPENDENCY, V2_CONFLICT, 0, item_index + 1);
            }
            else if (pipeline->dependencies[inner_index].value)
                return fail(diagnostic, GRAPH_ISSUE_DEPENDENCY, SYS_ERR_INVALID_ARG, 0, item_index + 1);
        for (inner_index = 0; inner_index < graph->entity_count; inner_index++)
            if ((em & (1UL << inner_index)) && (graph->entities[inner_index].flags & GRAPH_E_FUTURE) &&
                !(pipeline->flags & GRAPH_PIPE_TEMPLATE))
                return fail(diagnostic, GRAPH_ISSUE_FUTURE, SYS_ERR_INVALID_ARG, inner_index + 1, item_index + 1);
        if (shape(graph, lm, em, pipeline->source.value, pipeline->sinks, pipeline->sink_count, workspace, diagnostic))
            return diagnostic->backend_result;
        for (inner_index = 0; inner_index < pipeline->sink_count; inner_index++)
        {
            const GraphEntityDesc *sink = &graph->entities[pipeline->sinks[inner_index].value - 1];
            const GraphCaps *caps = &graph->caps[graph->pads[sink->pad_first].caps.value - 1];
            const GraphFrameRate *rate = &pipeline->default_sink_rates[inner_index];
            if (caps->max_fps_num &&
                (uint64_t)rate->num * caps->max_fps_den > (uint64_t)caps->max_fps_num * rate->den)
                return fail(diagnostic, GRAPH_ISSUE_POLICY, SYS_ERR_NOT_SUPPORTED, sink->id.value, item_index + 1);
        }
        for (inner_index = 0; inner_index < graph->pad_count; inner_index++)
            if (em & (1UL << (graph->pads[inner_index].parent.value - 1)))
                if (!accepts(&graph->caps[graph->pads[inner_index].caps.value - 1], pipeline->default_format))
                    return fail(diagnostic, GRAPH_ISSUE_FORMAT, SYS_ERR_NOT_SUPPORTED, 0, item_index + 1);
    }
    for (dependency_index = 0; dependency_index < graph->pipeline_count; dependency_index++)
        for (item_index = 0; item_index < graph->pipeline_count; item_index++)
            if (!done[item_index])
            {
                const GraphPipelineDesc *pipeline = &graph->pipelines[item_index];
                int ready = 1;
                for (inner_index = 0; inner_index < pipeline->dependency_count; inner_index++)
                    if (!done[pipeline->dependencies[inner_index].value - 1])
                        ready = 0;
                if (ready)
                    done[item_index] = 1;
            }
    for (item_index = 0; item_index < graph->pipeline_count; item_index++)
        if (!done[item_index])
            return fail(diagnostic, GRAPH_ISSUE_CYCLE, SYS_ERR_INVALID_STATE, 0, item_index + 1);
    memset(diagnostic, 0, sizeof(*diagnostic));
    return SYS_OK;
}
// 合并已选路径并校验启用的连接及格式。
static SysResult_t validate_set(const GraphDesc *graph, const GraphSelection *sel, uint16_t selection_count,
                                GraphScratch *scratch, GraphValidatedView *out, GraphIssue *diagnostic)
{
    uint32_t item_index, inner_index, policy_index, source = 0, pm = 0;
    SysResult_t result;
    GraphValidatedView *candidate;
    if (!bounds(graph) || !sel || !selection_count || selection_count > 8 || !scratch || !out || !diagnostic)
        return SYS_ERR_INVALID_ARG;
    if (alias(graph, scratch, sizeof(*scratch)) || alias(graph, out, sizeof(*out)) || alias(graph, diagnostic, sizeof(*diagnostic)) ||
        V2_Overlap(scratch, sizeof(*scratch), out, sizeof(*out)) || V2_Overlap(scratch, sizeof(*scratch), diagnostic, sizeof(*diagnostic)) ||
        V2_Overlap(out, sizeof(*out), diagnostic, sizeof(*diagnostic)) ||
        V2_Overlap(sel, selection_count * sizeof(*sel), out, sizeof(*out)) ||
        V2_Overlap(sel, selection_count * sizeof(*sel), scratch, sizeof(*scratch)) ||
        V2_Overlap(sel, selection_count * sizeof(*sel), diagnostic, sizeof(*diagnostic)))
        return SYS_ERR_INVALID_ARG;
    result = Graph_ValidateCatalog(graph, &scratch->work, diagnostic);
    if (result)
        return result;
    memset(&scratch->candidate, 0, sizeof(scratch->candidate));
    candidate = &scratch->candidate;
    candidate->graph = graph;
    for (item_index = 0; item_index < selection_count; item_index++)
    {
        const GraphPipelineDesc *pipeline = Graph_GetPipeline(graph, sel[item_index].pipeline);
        uint32_t lm = 0, em = 0;
        if (!pipeline || sel[item_index].reserved || sel[item_index].reserved_tail ||
            sel[item_index].sink_policy_count != pipeline->sink_count || CoreMedia_Validate(&sel[item_index].format))
            return fail(diagnostic, GRAPH_ISSUE_FORMAT, SYS_ERR_INVALID_ARG, 0, item_index);
        if (pm & (1UL << (pipeline->id.value - 1)))
            return fail(diagnostic, GRAPH_ISSUE_ID, V2_CONFLICT, 0, item_index);
        pm |= 1UL << (pipeline->id.value - 1);
        if (pipeline->flags & GRAPH_PIPE_TEMPLATE)
            return fail(diagnostic, GRAPH_ISSUE_FUTURE, SYS_ERR_NOT_SUPPORTED, 0, item_index);
        if (item_index &&
            (source != pipeline->source.value || memcmp(&candidate->format, &sel[item_index].format, sizeof(candidate->format))))
            return fail(diagnostic, GRAPH_ISSUE_FORMAT, V2_CONFLICT, 0, item_index);
        if (sel[item_index].format.backend != pipeline->backend)
            return fail(diagnostic, GRAPH_ISSUE_BACKEND, V2_CONFLICT, 0, item_index);
        source = pipeline->source.value;
        candidate->format = sel[item_index].format;
        for (inner_index = 0; inner_index < pipeline->link_count; inner_index++)
        {
            uint16_t id = pipeline->links[inner_index].value;
            lm |= 1UL << (id - 1);
            em |= 1UL << (graph->links[id - 1].source.entity.value - 1);
            em |= 1UL << (graph->links[id - 1].sink.entity.value - 1);
        }
        result = shape(graph, lm, em, (uint16_t)source, pipeline->sinks, pipeline->sink_count, &scratch->work, diagnostic);
        if (result)
            return result;
        candidate->link_mask |= lm;
        candidate->entity_mask |= em;
        for (inner_index = 0; inner_index < pipeline->sink_count; inner_index++)
        {
            const GraphSinkPolicy *sink_policy = NULL;
            GraphFrameRate rate;
            uint32_t rate_divisor;
            for (policy_index = 0; policy_index < sel[item_index].sink_policy_count; policy_index++)
                if (sel[item_index].sink_policies[policy_index].sink.value == pipeline->sinks[inner_index].value)
                {
                    if (sink_policy)
                        return fail(diagnostic, GRAPH_ISSUE_POLICY, V2_CONFLICT, 0, item_index);
                    sink_policy = &sel[item_index].sink_policies[policy_index];
                }
            if (!sink_policy || sink_policy->reserved || !sink_policy->rate.num || !sink_policy->rate.den)
                return fail(diagnostic, GRAPH_ISSUE_POLICY, SYS_ERR_INVALID_ARG, 0, item_index);
            rate = sink_policy->rate;
            rate_divisor = gcd(rate.num, rate.den);
            rate.num /= rate_divisor;
            rate.den /= rate_divisor;
            for (policy_index = 0; policy_index < candidate->sink_count; policy_index++)
                if (candidate->sinks[policy_index].sink.value == sink_policy->sink.value)
                    break;
            if (policy_index < candidate->sink_count)
            {
                if (candidate->sinks[policy_index].rate.num != rate.num || candidate->sinks[policy_index].rate.den != rate.den)
                    return fail(diagnostic, GRAPH_ISSUE_POLICY, V2_CONFLICT, 0, item_index);
            }
            else
            {
                if (policy_index == 5)
                    return fail(diagnostic, GRAPH_ISSUE_BOUND, SYS_ERR_NO_RESOURCE, 0, item_index);
                candidate->sinks[policy_index] = *sink_policy;
                candidate->sinks[policy_index].rate = rate;
                candidate->sink_count++;
            }
        }
    }
    for (item_index = 0; item_index < selection_count; item_index++)
    {
        const GraphPipelineDesc *pipeline = Graph_GetPipeline(graph, sel[item_index].pipeline);
        for (inner_index = 0; inner_index < pipeline->dependency_count; inner_index++)
            if (!(pm & (1UL << (pipeline->dependencies[inner_index].value - 1))))
                return fail(diagnostic, GRAPH_ISSUE_DEPENDENCY, SYS_ERR_INVALID_ARG, 0, item_index);
    }
    {
        GraphEntityId sinks[5];
        for (item_index = 0; item_index < candidate->sink_count; item_index++)
            sinks[item_index] = candidate->sinks[item_index].sink;
        result = shape(graph, candidate->link_mask, candidate->entity_mask, (uint16_t)source, sinks, candidate->sink_count, &scratch->work,
                  diagnostic);
        if (result)
            return result;
    }
    for (item_index = 0; item_index < graph->entity_count; item_index++)
        if (candidate->entity_mask & (1UL << item_index))
        {
            const GraphEntityDesc *entity = &graph->entities[item_index];
            if (entity->flags & GRAPH_E_FUTURE)
                return fail(diagnostic, GRAPH_ISSUE_FUTURE, SYS_ERR_NOT_SUPPORTED, item_index + 1, 0);
            for (inner_index = 0; inner_index < entity->pad_count; inner_index++)
                if (!accepts(&graph->caps[graph->pads[entity->pad_first + inner_index].caps.value - 1], &candidate->format))
                    return fail(diagnostic, GRAPH_ISSUE_FORMAT, SYS_ERR_NOT_SUPPORTED, item_index + 1, inner_index);
            if (entity->ops->validate_format)
            {
                memset(diagnostic, 0, sizeof(*diagnostic));
                result = entity->ops->validate_format(entity, &candidate->format, graph->check_context, diagnostic);
                if (result)
                {
                    if (!diagnostic->code)
                        diagnostic->code = GRAPH_ISSUE_ADAPTER;
                    diagnostic->entity.value = (uint16_t)(item_index + 1);
                    diagnostic->backend_result = result;
                    return result;
                }
            }
        }
    // Sink 保持固定顺序，避免候选结果随请求排列变化。
    for (item_index = 0; item_index < candidate->sink_count; item_index++)
        for (inner_index = item_index + 1; inner_index < candidate->sink_count; inner_index++)
            if (candidate->sinks[inner_index].sink.value < candidate->sinks[item_index].sink.value)
            {
                GraphSinkPolicy sink_policy = candidate->sinks[item_index];
                candidate->sinks[item_index] = candidate->sinks[inner_index];
                candidate->sinks[inner_index] = sink_policy;
            }
    for (item_index = 0; item_index < candidate->sink_count; item_index++)
    {
        const GraphEntityDesc *entity = &graph->entities[candidate->sinks[item_index].sink.value - 1];
        const GraphCaps *caps = &graph->caps[graph->pads[entity->pad_first].caps.value - 1];
        GraphFrameRate *current_rate = &candidate->sinks[item_index].rate;
        if (caps->max_fps_num && (uint64_t)current_rate->num * caps->max_fps_den > (uint64_t)caps->max_fps_num * current_rate->den)
            return fail(diagnostic, GRAPH_ISSUE_POLICY, SYS_ERR_NOT_SUPPORTED, entity->id.value, 0);
        memset(diagnostic, 0, sizeof(*diagnostic));
        result = entity->ops->validate_sink_config(entity, &candidate->format, current_rate, graph->check_context, diagnostic);
        if (result)
        {
            if (!diagnostic->code)
                diagnostic->code = GRAPH_ISSUE_ADAPTER;
            diagnostic->entity = entity->id;
            diagnostic->backend_result = result;
            return result;
        }
    }
    // 复用已校验的 Kahn 拓扑顺序，避免额外遍历突破有界预算。
    for (item_index = 0; item_index < graph->entity_count; item_index++)
        if (candidate->entity_mask & (1UL << item_index))
            candidate->topo_count++;
    for (item_index = 0; item_index < candidate->topo_count; item_index++)
        candidate->topo[item_index].value = scratch->work.entity_order[item_index];
    candidate->pipeline_mask = pm;
    *out = *candidate;
    memset(diagnostic, 0, sizeof(*diagnostic));
    return SYS_OK;
}

// 校验所选路径的合并、扇出和端口格式。
SysResult_t Graph_ValidateSet(const GraphDesc *graph, const GraphSelection *sel, uint16_t selection_count,
                              GraphScratch *scratch, GraphValidatedView *out, GraphIssue *diagnostic)
{
    SysResult_t result;
    if (!bounds(graph) || !sel || !selection_count || selection_count > 8 || !scratch || !out || !diagnostic)
        return SYS_ERR_INVALID_ARG;
    if (alias(graph, scratch, sizeof(*scratch)) || V2_Overlap(scratch, sizeof(*scratch), out, sizeof(*out)) ||
        V2_Overlap(scratch, sizeof(*scratch), diagnostic, sizeof(*diagnostic)) ||
        V2_Overlap(sel, selection_count * sizeof(*sel), scratch, sizeof(*scratch)))
        return SYS_ERR_INVALID_ARG;
    if (scratch->work.reserved[15])
        return SYS_ERR_BUSY;
    scratch->work.reserved[15] = 1;
    result = validate_set(graph, sel, selection_count, scratch, out, diagnostic);
    scratch->work.reserved[15] = 0;
    return result;
}
