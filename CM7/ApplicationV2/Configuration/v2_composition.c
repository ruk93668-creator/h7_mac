// 验证配置候选的模块绑定和媒体路径。

#include "v2_composition.h"
#include <string.h>
// 记录验证失败及其位置，便于定位错误配置。
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
// 校验候选的绑定、模块依赖和媒体路径。
static SysResult_t validate_composition(const V2Registry *registry, const GraphDesc *graph,
                                        const V2EntityBinding *bindings, uint16_t count,
                                        const V2Desired *desired, V2ResourceValidator validate,
                                        V2CompositionScratch *scratch, V2CompositionCandidate *out,
                                        V2Issue *issue, GraphIssue *ge)
{
    uint32_t entity_index, binding_index;
    SysResult_t result;
    if (!registry || !graph || !bindings || !count || count > 16 || !desired || !scratch || !out || !issue || !ge)
        return SYS_ERR_INVALID_ARG;
    if (V2_Overlap(scratch, sizeof(*scratch), out, sizeof(*out)) ||
        V2_Overlap(issue, sizeof(*issue), out, sizeof(*out)) ||
        V2_Overlap(ge, sizeof(*ge), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    result = V2Resolver_Resolve(registry, desired->roots, desired->root_count, &scratch->resolver, &scratch->candidate.modules, issue);
    if (result)
        return result;
    result = Graph_ValidateSet(graph, desired->selections, desired->selection_count, &scratch->graph, &scratch->candidate.media, ge);
    if (result)
        return result;
    for (entity_index = 0; entity_index < count; entity_index++)
    {
        if (bindings[entity_index].reserved || !Graph_GetEntity(graph, bindings[entity_index].entity))
            return binding_error(issue, SYS_ERR_INVALID_ARG, bindings[entity_index].module.value,
                                 bindings[entity_index].entity.value);
        for (binding_index = 0; binding_index < entity_index; binding_index++)
            if (bindings[binding_index].entity.value == bindings[entity_index].entity.value)
                return binding_error(issue, V2_CONFLICT, bindings[entity_index].module.value,
                                     bindings[entity_index].entity.value);
    }
    for (entity_index = 0; entity_index < graph->entity_count; entity_index++)
        if (scratch->candidate.media.entity_mask & (1UL << entity_index))
        {
            const V2ModuleDesc *module;
            for (binding_index = 0; binding_index < count; binding_index++)
                if (bindings[binding_index].entity.value == entity_index + 1)
                    break;
            if (binding_index == count)
                return binding_error(issue, SYS_ERR_NOT_FOUND, 0, entity_index + 1);
            if (!bindings[binding_index].module.value)
            {
                if (graph->entities[entity_index].kind != GRAPH_ENTITY_PROCESSOR)
                    return binding_error(issue, SYS_ERR_INVALID_ARG, 0, entity_index + 1);
                continue;
            }
            module = V2Registry_Get(registry, bindings[binding_index].module);
            if (!module || !(scratch->candidate.modules.selected_mask & (1UL << (module - registry->modules))))
                return binding_error(issue, V2_CONFLICT, bindings[binding_index].module.value, entity_index + 1);
        }
    if (!validate)
        return binding_error(issue, SYS_ERR_NOT_SUPPORTED, 0, 0);
    result = validate(&scratch->candidate.media, &scratch->candidate.modules, issue);
    if (result)
    {
        if (!issue->result)
            issue->result = result;
        return result;
    }
    scratch->candidate.candidate_only = 1;
    *out = scratch->candidate;
    memset(issue, 0, sizeof(*issue));
    return SYS_OK;
}

// 校验组合候选，候选成立不代表运行配置已经切换。
SysResult_t V2Composition_Validate(const V2Registry *registry, const GraphDesc *graph,
                                   const V2EntityBinding *bindings, uint16_t count, const V2Desired *desired,
                                   V2ResourceValidator validate, V2CompositionScratch *scratch,
                                   V2CompositionCandidate *out, V2Issue *issue, GraphIssue *ge)
{
    SysResult_t result;
    if (!scratch || !out || !issue || !ge || !desired || !bindings || count > 16)
        return SYS_ERR_INVALID_ARG;
    if (V2_Overlap(scratch, sizeof(*scratch), out, sizeof(*out)) ||
        V2_Overlap(scratch, sizeof(*scratch), issue, sizeof(*issue)) ||
        V2_Overlap(scratch, sizeof(*scratch), ge, sizeof(*ge)) || V2_Overlap(scratch, sizeof(*scratch), desired, sizeof(*desired)) ||
        V2_Overlap(scratch, sizeof(*scratch), bindings, count * sizeof(*bindings)))
        return SYS_ERR_INVALID_ARG;
    if (scratch->busy)
        return SYS_ERR_BUSY;
    scratch->busy = 1;
    result = validate_composition(registry, graph, bindings, count, desired, validate, scratch, out, issue, ge);
    scratch->busy = 0;
    return result;
}
