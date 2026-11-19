// 校验模块描述符，解析提供方和依赖关系。

#include "v2_registry.h"
#include <string.h>
// 记录验证失败及其位置，便于定位错误配置。
static SysResult_t issue(V2Issue *diagnostic, SysResult_t result, uint32_t module_id, uint32_t capability_id, uint32_t related_id)
{
    if (diagnostic)
    {
        diagnostic->code = (uint32_t)(-result);
        diagnostic->result = result;
        diagnostic->module = module_id;
        diagnostic->capability = capability_id;
        diagnostic->related = related_id;
    }
    return result;
}
// 校验能力版本是否满足依赖约束。
static uint32_t version(uint16_t major, uint16_t minor)
{
    return ((uint32_t)major << 16) | minor;
}
// 校验单条模块依赖描述。
static int requirement(const V2Requirement *requirement_desc, int optional)
{
    return requirement_desc->id.value &&
           version(requirement_desc->min_major, requirement_desc->min_minor) <= version(requirement_desc->max_major, requirement_desc->max_minor) &&
           !(requirement_desc->flags & ~(optional ? V2_OPTIONAL_AUTO : 0U));
}
// 校验静态注册表及模块描述符的完整性。
SysResult_t V2Registry_Validate(const V2Registry *registry, V2Issue *diagnostic)
{
    uint32_t module_index, entry_index, previous_index, provided_total = 0, required_total = 0, optional_total = 0, conflict_total = 0;
    //先核对注册表、诊断输出和保留位，再遍历描述符。
    if (!registry || !diagnostic || !registry->modules || !registry->count || registry->reserved)
        return SYS_ERR_INVALID_ARG;
    if (registry->count > V2_MODULE_MAX)
        return issue(diagnostic, SYS_ERR_NO_RESOURCE, 0, 0, 0);
    for (module_index = 0; module_index < registry->count; module_index++)
    {
        const V2ModuleDesc *module = &registry->modules[module_index];
        if (!module->id.value || !module->major || module->flags & ~(V2_DESCRIPTOR_ONLY | V2_MODULE_FUTURE) ||
            (module->provided_count && !module->provided) || (module->required_count && !module->required) ||
            (module->optional_count && !module->optional) || (module->conflict_count && !module->conflicts))
            return issue(diagnostic, SYS_ERR_INVALID_ARG, module->id.value, 0, 0);
        //累计整张目录占用，限制后续解析所需的空间。
        provided_total += module->provided_count;
        required_total += module->required_count;
        optional_total += module->optional_count;
        conflict_total += module->conflict_count;
        if (provided_total > 32 || required_total > 64 || optional_total > 32 || conflict_total > 32)
            return issue(diagnostic, SYS_ERR_NO_RESOURCE, module->id.value, 0, 0);
        //只和之前的项比较，报告第一条重复模块身份。
        for (entry_index = 0; entry_index < module_index; entry_index++)
            if (module->id.value == registry->modules[entry_index].id.value)
                return issue(diagnostic, V2_CONFLICT, module->id.value, 0, 0);
        //同一模块不能重复声明一项提供能力。
        for (entry_index = 0; entry_index < module->provided_count; entry_index++)
        {
            if (!module->provided[entry_index].id.value || !module->provided[entry_index].major)
                return issue(diagnostic, SYS_ERR_INVALID_ARG, module->id.value, 0, 0);
            for (previous_index = 0; previous_index < entry_index; previous_index++)
                if (module->provided[previous_index].id.value == module->provided[entry_index].id.value)
                    return issue(diagnostic, V2_CONFLICT, module->id.value, module->provided[entry_index].id.value, 0);
        }
        //逐项核对必需依赖的能力编号和版本范围。
        for (entry_index = 0; entry_index < module->required_count; entry_index++)
            if (!requirement(&module->required[entry_index], 0))
                return issue(diagnostic, SYS_ERR_INVALID_ARG, module->id.value, 0, 0);
        //可选依赖允许自动选择标志，其他标志仍非法。
        for (entry_index = 0; entry_index < module->optional_count; entry_index++)
            if (!requirement(&module->optional[entry_index], 1))
                return issue(diagnostic, SYS_ERR_INVALID_ARG, module->id.value, 0, 0);
        for (entry_index = 0; entry_index < module->conflict_count; entry_index++)
            if (!module->conflicts[entry_index].value)
                return issue(diagnostic, SYS_ERR_INVALID_ARG, module->id.value, 0, 0);
    }
    memset(diagnostic, 0, sizeof(*diagnostic));
    return SYS_OK;
}
// 按模块身份查询静态描述符。
const V2ModuleDesc *V2Registry_Get(const V2Registry *registry, V2ModuleId id)
{
    uint32_t module_index;
    if (!registry || !registry->modules || registry->count > 16 || !id.value)
        return NULL;
    for (module_index = 0; module_index < registry->count; module_index++)
        if (registry->modules[module_index].id.value == id.value)
            return &registry->modules[module_index];
    return NULL;
}
// 收集满足能力条件的静态提供方。
static SysResult_t providers(const V2Registry *registry, const V2Requirement *requirement_desc, uint16_t *mask)
{
    uint32_t module_index, capability_index;
    uint16_t provider_mask = 0;
    //输入无效时保留调用方原来的提供方掩码。
    if (!requirement_desc || !mask || !requirement(requirement_desc, 1))
        return SYS_ERR_INVALID_ARG;
    for (module_index = 0; module_index < registry->count; module_index++)
        for (capability_index = 0; capability_index < registry->modules[module_index].provided_count; capability_index++)
        {
            const V2Provided *capability = &registry->modules[module_index].provided[capability_index];
            uint32_t ver = version(capability->major, capability->minor);
            if (capability->id.value == requirement_desc->id.value && ver >= version(requirement_desc->min_major, requirement_desc->min_minor) &&
                ver <= version(requirement_desc->max_major, requirement_desc->max_minor) &&
                !(registry->modules[module_index].flags & V2_MODULE_FUTURE))
                //只登记能力及版本都匹配且已可用的模块。
                provider_mask |= (uint16_t)(1U << module_index);
        }
    *mask = provider_mask;
    return SYS_OK;
}
// 枚举与指定依赖匹配的提供方。
SysResult_t V2Registry_Providers(const V2Registry *registry, const V2Requirement *requirement_desc, uint16_t *mask)
{
    V2Issue diagnostic;
    SysResult_t result = V2Registry_Validate(registry, &diagnostic);
    if (result)
        return result;
    return providers(registry, requirement_desc, mask);
}
// 根据选择策略和候选约束确定依赖提供方。
static SysResult_t choose(const V2Registry *registry, const V2Requirement *requirement_desc, uint16_t *provider)
{
    //暂存可用提供方，随后排除未被候选选中的模块。
    uint16_t mask;
    uint32_t module_index, provider_count = 0;
    SysResult_t result = providers(registry, requirement_desc, &mask);
    if (result)
        return result;
    //指定提供方时缩小候选集合。
    if (requirement_desc->preferred.value)
        for (module_index = 0; module_index < registry->count; module_index++)
            if (registry->modules[module_index].id.value == requirement_desc->preferred.value)
            {
                mask &= (uint16_t)(1U << module_index);
                break;
            }
    if (requirement_desc->preferred.value && module_index == registry->count)
        return SYS_ERR_NOT_FOUND;
    for (module_index = 0; module_index < registry->count; module_index++)
        if (mask & (1U << module_index))
        {
            *provider = (uint16_t)module_index;
            provider_count++;
        }
    return provider_count == 1 ? SYS_OK : provider_count ? V2_CONFLICT : SYS_ERR_NOT_FOUND;
}
// 解析模块依赖和提供方，生成稳定执行顺序。
SysResult_t V2Resolver_Resolve(const V2Registry *registry, const V2ModuleId *roots, uint16_t root_count,
                               V2ResolverScratch *scratch, V2Resolution *out, V2Issue *diagnostic)
{
    uint32_t module_index, dependency_index, provider_index, round, changed;
    uint16_t chosen_provider;
    SysResult_t result;
    //解析前校验入口集合、工作区和输出对象。
    if (!scratch || !out || !diagnostic || !roots || !root_count)
        return SYS_ERR_INVALID_ARG;
    if (root_count > 16)
        return issue(diagnostic, SYS_ERR_NO_RESOURCE, 0, 0, 0);
    //拒绝别名，防止清理工作区时覆盖输入、输出或诊断。
    if (V2_Overlap(scratch, sizeof(*scratch), out, sizeof(*out)) ||
        V2_Overlap(diagnostic, sizeof(*diagnostic), out, sizeof(*out)) || V2_Overlap(diagnostic, sizeof(*diagnostic), scratch, sizeof(*scratch)) ||
        V2_Overlap(roots, root_count * sizeof(*roots), scratch, sizeof(*scratch)) ||
        V2_Overlap(roots, root_count * sizeof(*roots), out, sizeof(*out)) ||
        V2_Overlap(roots, root_count * sizeof(*roots), diagnostic, sizeof(*diagnostic)))
        return SYS_ERR_INVALID_ARG;
    result = V2Registry_Validate(registry, diagnostic);
    if (result)
        return result;
    //从干净的临时结果开始，失败时不提交目标。
    memset(scratch, 0, sizeof(*scratch));
    for (module_index = 0; module_index < root_count; module_index++)
    {
        const V2ModuleDesc *module = V2Registry_Get(registry, roots[module_index]);
        if (!module)
            return issue(diagnostic, SYS_ERR_NOT_FOUND, roots[module_index].value, 0, 0);
        if (module->flags & V2_MODULE_FUTURE)
            return issue(diagnostic, SYS_ERR_NOT_SUPPORTED, module->id.value, 0, 0);
        for (dependency_index = 0; dependency_index < module_index; dependency_index++)
            if (roots[dependency_index].value == roots[module_index].value)
                return issue(diagnostic, V2_CONFLICT, roots[module_index].value, 0, 0);
        //登记根模块，后续继续展开它的依赖。
        scratch->selected[module - registry->modules] = 1;
    }
    //有界扩展依赖，直到本轮没有新增模块。
    for (round = 0; round < 16; round++)
    {
        changed = 0;
        for (module_index = 0; module_index < registry->count; module_index++)
            if (scratch->selected[module_index])
            {
                const V2ModuleDesc *module = &registry->modules[module_index];
                for (dependency_index = 0; dependency_index < (uint32_t)module->required_count + module->optional_count; dependency_index++)
                {
                    int opt = dependency_index >= module->required_count;
                    const V2Requirement *requirement_desc =
                        opt ? &module->optional[dependency_index - module->required_count] : &module->required[dependency_index];
                    //非自动选择的可选依赖不能主动拉入模块。
                    if (opt && !(requirement_desc->flags & V2_OPTIONAL_AUTO))
                        continue;
                    result = choose(registry, requirement_desc, &chosen_provider);
                    //可选项可以缺失，歧义和其他错误仍返回。
                    if (result == SYS_ERR_NOT_FOUND && opt)
                        continue;
                    if (result)
                        return issue(diagnostic, result, module->id.value, requirement_desc->id.value, 0);
                    //记录依赖边，首次选中提供方时才继续扩大候选。
                    scratch->candidate.dependencies[module_index] |= (uint16_t)(1U << chosen_provider);
                    if (!scratch->selected[chosen_provider])
                    {
                        scratch->selected[chosen_provider] = 1;
                        changed = 1;
                    }
                }
            }
        if (!changed)
            break;
    }
    for (module_index = 0; module_index < registry->count; module_index++)
        if (scratch->selected[module_index])
        {
            const V2ModuleDesc *module = &registry->modules[module_index];
            //固定已选模块集合，再补齐可选依赖和检查互斥。
            scratch->candidate.selected_mask |= (uint16_t)(1U << module_index);
            for (dependency_index = 0; dependency_index < module->optional_count; dependency_index++)
                if (module->optional[dependency_index].flags & V2_OPTIONAL_AUTO)
                {
                    result = choose(registry, &module->optional[dependency_index], &chosen_provider);
                    if (result == SYS_ERR_NOT_FOUND)
                        scratch->candidate.optional_missing++;
                    else if (result)
                        return issue(diagnostic, result, module->id.value, module->optional[dependency_index].id.value, 0);
                }
                else
                {
                    //暂存可用提供方，随后排除未被候选选中的模块。
                    uint16_t mask;
                    result = providers(registry, &module->optional[dependency_index], &mask);
                    if (result)
                        return result;
                    for (provider_index = 0; provider_index < registry->count; provider_index++)
                        if (!scratch->selected[provider_index])
                            mask &= (uint16_t)~(1U << provider_index);
                    if (module->optional[dependency_index].preferred.value)
                    {
                        uint32_t candidate_index;
                        for (candidate_index = 0; candidate_index < registry->count; candidate_index++)
                            if (registry->modules[candidate_index].id.value == module->optional[dependency_index].preferred.value)
                                break;
                        mask = candidate_index == registry->count ? 0U : (uint16_t)(mask & (1U << candidate_index));
                    }
                    if (!mask)
                        scratch->candidate.optional_missing++;
                    else if (mask & (mask - 1U))
                        return issue(diagnostic, V2_CONFLICT, module->id.value, module->optional[dependency_index].id.value, 0);
                    else
                        scratch->candidate.dependencies[module_index] |= mask;
                }
            //互斥检查只针对实际同时选中的模块。
            for (dependency_index = 0; dependency_index < module->conflict_count; dependency_index++)
                for (provider_index = 0; provider_index < registry->count; provider_index++)
                    if (provider_index != module_index && scratch->selected[provider_index])
                    {
                        uint32_t candidate_index;
                        for (candidate_index = 0; candidate_index < registry->modules[provider_index].provided_count; candidate_index++)
                            if (module->conflicts[dependency_index].value == registry->modules[provider_index].provided[candidate_index].id.value)
                                return issue(diagnostic, V2_CONFLICT, module->id.value, module->conflicts[dependency_index].value,
                                             registry->modules[provider_index].id.value);
                    }
        }
    //生成稳定的拓扑顺序，每轮最多提交一个已就绪模块。
    for (round = 0; round < registry->count; round++)
    {
        uint32_t best = registry->count;
        for (module_index = 0; module_index < registry->count; module_index++)
            if (scratch->selected[module_index] && !scratch->done[module_index])
            {
                int ready = 1;
                for (dependency_index = 0; dependency_index < registry->count; dependency_index++)
                    if ((scratch->candidate.dependencies[module_index] & (1U << dependency_index)) && !scratch->done[dependency_index])
                        ready = 0;
                if (ready &&
                    (best == registry->count || registry->modules[module_index].id.value < registry->modules[best].id.value))
                    best = module_index;
            }
        if (best == registry->count)
            break;
        //只提交依赖均已就绪的模块，更新后供下一轮使用。
        scratch->done[best] = 1;
        scratch->candidate.order[scratch->candidate.count++] = registry->modules[best].id;
    }
    for (module_index = 0; module_index < registry->count; module_index++)
        if (scratch->selected[module_index] && !scratch->done[module_index])
            return issue(diagnostic, SYS_ERR_INVALID_STATE, registry->modules[module_index].id.value, 0, 0);
    //所有选中模块均能排序后才提交最终候选。
    *out = scratch->candidate;
    memset(diagnostic, 0, sizeof(*diagnostic));
    return SYS_OK;
}
