// 将组合候选中的依赖顺序转换为有界执行计划。

#include "execution_manager.h"
#include <string.h>
// 向有界执行计划添加一个步骤。
static SysResult_t append(EMPlan *plan, uint32_t module_id, uint32_t op, uint32_t from)
{
    if (plan->count == EM_STEP_MAX)
        return SYS_ERR_NO_RESOURCE;
    plan->steps[plan->count].module = module_id;
    plan->steps[plan->count].operation = op;
    plan->steps[plan->count++].expected = from;
    return SYS_OK;
}
// 按候选依赖关系构造有界启动或逆序停止计划。
SysResult_t EM_BuildPlan(const V2Registry *registry, const V2CompositionCandidate *current_candidate,
                         const uint32_t *states, uint16_t mask, uint32_t kind, uint32_t cleanup,
                         EMPlan *out)
{
    EMPlan plan;
    V2Issue issue;
    V2ResolverScratch scratch;
    V2Resolution verified;
    uint32_t item_index, module_index, reverse_index, seen = 0, done = 0, ix[V2_MODULE_MAX];
    SysResult_t result;
    if (!registry || !current_candidate || !states || !out || current_candidate->candidate_only != 1 ||
        (kind != EM_START && kind != EM_STOP) || cleanup > 1)
        return SYS_ERR_INVALID_ARG;
    if (V2_Overlap(out, sizeof(*out), current_candidate, sizeof(*current_candidate)) ||
        V2_Overlap(out, sizeof(*out), registry, sizeof(*registry)) ||
        V2_Overlap(out, sizeof(*out), states, sizeof(*states) * V2_MODULE_MAX))
        return SYS_ERR_INVALID_ARG;
    result = V2Registry_Validate(registry, &issue);
    if (result)
        return result;
    if (!current_candidate->modules.count || current_candidate->modules.count > registry->count || current_candidate->modules.selected_mask >> registry->count)
        return SYS_ERR_PROTOCOL;
    for (item_index = 0; item_index < current_candidate->modules.count; item_index++)
    {
        for (module_index = 0; module_index < registry->count; module_index++)
            if (registry->modules[module_index].id.value == current_candidate->modules.order[item_index].value)
                break;
        if (module_index == registry->count || (seen & (1U << module_index)) || !(current_candidate->modules.selected_mask & (1U << module_index)) ||
            !LC_Stable(states[module_index]) || (current_candidate->modules.dependencies[module_index] & ~done) ||
            registry->modules[module_index].flags & (V2_DESCRIPTOR_ONLY | V2_MODULE_FUTURE))
            return SYS_ERR_PROTOCOL;
        ix[item_index] = module_index;
        seen |= 1U << module_index;
        done = seen;
    }
    if (seen != current_candidate->modules.selected_mask)
        return SYS_ERR_PROTOCOL;
    /* A target candidate cannot hide a live module from stop-dependency checks.
     * Phase8 incremental live/target reconciliation is deliberately absent. */
    for (item_index = 0; item_index < registry->count; item_index++)
    {
        if (!LC_Stable(states[item_index]))
            return SYS_ERR_INVALID_STATE;
        if (!(seen & (1U << item_index)) && states[item_index] != LC_UNCONFIGURED)
            return SYS_ERR_BUSY;
    }
    memset(&scratch, 0, sizeof(scratch));
    result = V2Resolver_Resolve(registry, current_candidate->modules.order, current_candidate->modules.count, &scratch, &verified, &issue);
    if (result)
        return result;
    if (memcmp(&verified, &current_candidate->modules, sizeof(verified)))
        return SYS_ERR_PROTOCOL;
    if (kind == EM_STOP && (!mask || (mask & ~seen)))
        return SYS_ERR_INVALID_ARG;
    /* Stop dependency closure: a live dependent outside subset pins its provider. */
    if (kind == EM_STOP)
        for (item_index = 0; item_index < registry->count; item_index++)
            if ((seen & (1U << item_index)) && !(mask & (1U << item_index)) && states[item_index] != LC_UNCONFIGURED &&
                (current_candidate->modules.dependencies[item_index] & mask))
                return SYS_ERR_BUSY;
    for (item_index = 0; item_index < registry->count; item_index++)
        if ((seen & (1U << item_index)) && states[item_index] == LC_ACTIVE)
            for (module_index = 0; module_index < registry->count; module_index++)
                if ((current_candidate->modules.dependencies[item_index] & (1U << module_index)) && states[module_index] != LC_ACTIVE)
                    return SYS_ERR_INVALID_STATE;
    memset(&plan, 0, sizeof(plan));
    plan.kind = kind;
    if (kind == EM_START)
    {
        for (item_index = 0; item_index < current_candidate->modules.count; item_index++)
        {
            module_index = ix[item_index];
            if (states[module_index] == LC_ERROR)
                return SYS_ERR_INVALID_STATE;
            if (states[module_index] == LC_UNCONFIGURED)
            {
                result = append(&plan, registry->modules[module_index].id.value, LC_CONFIGURE, LC_UNCONFIGURED);
                if (result)
                    return result;
            }
        }
        for (item_index = 0; item_index < current_candidate->modules.count; item_index++)
        {
            module_index = ix[item_index];
            if (states[module_index] != LC_ACTIVE)
            {
                result = append(&plan, registry->modules[module_index].id.value, LC_ACTIVATE, LC_INACTIVE);
                if (result)
                    return result;
            }
        }
    }
    else
    {
        for (reverse_index = current_candidate->modules.count; reverse_index > 0; reverse_index--)
        {
            module_index = ix[reverse_index - 1];
            if (!(mask & (1U << module_index)))
                continue;
            if (states[module_index] == LC_ERROR)
                return SYS_ERR_INVALID_STATE;
            if (states[module_index] == LC_ACTIVE)
            {
                result = append(&plan, registry->modules[module_index].id.value, LC_DEACTIVATE, LC_ACTIVE);
                if (result)
                    return result;
            }
        }
        if (cleanup)
            for (reverse_index = current_candidate->modules.count; reverse_index > 0; reverse_index--)
            {
                module_index = ix[reverse_index - 1];
                if ((mask & (1U << module_index)) && states[module_index] != LC_UNCONFIGURED)
                {
                    result = append(&plan, registry->modules[module_index].id.value, LC_CLEANUP, LC_INACTIVE);
                    if (result)
                        return result;
                }
            }
    }
    *out = plan;
    return SYS_OK;
}
