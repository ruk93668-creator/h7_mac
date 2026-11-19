// 验证配置候选的模块绑定和媒体路径。

#include "v2_composition.h"
#include <string.h>
// 记录验证失败及其位置，便于定位错误配置。
static SysResult_t binding_error(V2Issue *issue, SysResult_t result, uint32_t module,
                                 uint32_t entity)
{
    //先清除上一条诊断，再保留本次失败的模块与实体。
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
    //先拒绝缺失输入和超容量绑定，避免访问无效目录。
    if (!registry || !graph || !bindings || !count || count > 16 || !desired || !scratch || !out || !issue || !ge)
        return SYS_ERR_INVALID_ARG;
    //工作区和诊断都不能覆盖候选输出，失败时保持原输出。
    if (V2_Overlap(scratch, sizeof(*scratch), out, sizeof(*out)) ||
        V2_Overlap(issue, sizeof(*issue), out, sizeof(*out)) ||
        V2_Overlap(ge, sizeof(*ge), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    // 先解析模块依赖，再验证媒体路由；两步任一失败都不写候选输出。
    result = V2Resolver_Resolve(registry, desired->roots, desired->root_count, &scratch->resolver, &scratch->candidate.modules, issue);
    if (result)
        return result;
    //模块依赖通过后再校验媒体拓扑与格式。
    result = Graph_ValidateSet(graph, desired->selections, desired->selection_count, &scratch->graph, &scratch->candidate.media, ge);
    if (result)
        return result;
    //逐条确认实体存在且没有重复绑定，先保留首个错误。
    for (entity_index = 0; entity_index < count; entity_index++)
    {
        if (bindings[entity_index].reserved || !Graph_GetEntity(graph, bindings[entity_index].entity))
            return binding_error(issue, SYS_ERR_INVALID_ARG, bindings[entity_index].module.value,
                                 bindings[entity_index].entity.value);
        //仅检查前面的绑定，遇到首个重复实体就结束。
        for (binding_index = 0; binding_index < entity_index; binding_index++)
            if (bindings[binding_index].entity.value == bindings[entity_index].entity.value)
                return binding_error(issue, V2_CONFLICT, bindings[entity_index].module.value,
                                     bindings[entity_index].entity.value);
    }
    //只核对当前候选选中的实体，未使用的实体不限制本次装配。
    for (entity_index = 0; entity_index < graph->entity_count; entity_index++)
    {
        const V2ModuleDesc *module;

        if (!(scratch->candidate.media.entity_mask & (1UL << entity_index)))
            continue;

        //实体编号从 1 开始，按编号找原绑定记录。
        for (binding_index = 0; binding_index < count; binding_index++)
            if (bindings[binding_index].entity.value == entity_index + 1)
                break;
        //缺少绑定就拒绝候选，不能让选中的实体缺少业务归属。
        if (binding_index == count)
            return binding_error(issue, SYS_ERR_NOT_FOUND, 0, entity_index + 1);
        //只有纯逻辑处理节点允许没有对应模块。
        if (!bindings[binding_index].module.value)
        {
            if (graph->entities[entity_index].kind != GRAPH_ENTITY_PROCESSOR)
                return binding_error(issue, SYS_ERR_INVALID_ARG, 0, entity_index + 1);
            continue;
        }
        //绑定的模块也必须出现在本轮 Resolver 候选中。
        module = V2Registry_Get(registry, bindings[binding_index].module);
        if (!module || !(scratch->candidate.modules.selected_mask & (1UL << (module - registry->modules))))
            return binding_error(issue, V2_CONFLICT, bindings[binding_index].module.value, entity_index + 1);
    }
    //资源验证器缺失时拒绝候选，不推断板上容量足够。
    if (!validate)
        return binding_error(issue, SYS_ERR_NOT_SUPPORTED, 0, 0);
    //交由板级验证器检查组合资源，并保留其首条诊断。
    result = validate(&scratch->candidate.media, &scratch->candidate.modules, issue);
    if (result)
    {
        if (!issue->result)
            issue->result = result;
        return result;
    }
    // 仅在全部校验完成后发布候选，实际运行配置仍由执行流程提交。
    scratch->candidate.candidate_only = 1;
    *out = scratch->candidate;
    //候选发布成功，清除上次的失败诊断。
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
    //确认接口参数和绑定数量后再访问工作区。
    if (!scratch || !out || !issue || !ge || !desired || !bindings || count > 16)
        return SYS_ERR_INVALID_ARG;
    //工作区和诊断都不能覆盖候选输出，失败时保持原输出。
    if (V2_Overlap(scratch, sizeof(*scratch), out, sizeof(*out)) ||
        V2_Overlap(scratch, sizeof(*scratch), issue, sizeof(*issue)) ||
        V2_Overlap(scratch, sizeof(*scratch), ge, sizeof(*ge)) || V2_Overlap(scratch, sizeof(*scratch), desired, sizeof(*desired)) ||
        V2_Overlap(scratch, sizeof(*scratch), bindings, count * sizeof(*bindings)))
        return SYS_ERR_INVALID_ARG;
    //重入时保留原调用的工作区。
    if (scratch->busy)
        return SYS_ERR_BUSY;
    //只在同步校验期间占用工作区，成功和失败统一解除标志。
    scratch->busy = 1;
    result = validate_composition(registry, graph, bindings, count, desired, validate, scratch, out, issue, ge);
    scratch->busy = 0;
    return result;
}
