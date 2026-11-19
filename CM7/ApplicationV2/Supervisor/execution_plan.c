// 将组合候选中的依赖顺序转换为有界执行计划。

#include "execution_manager.h"
#include <string.h>
// 向有界执行计划添加一个步骤。
static SysResult_t append(EMPlan *plan, uint32_t module_id, uint32_t op, uint32_t from)
{
    //先确认还有静态计划槽位，写完整步骤后才增长计数。
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
    //只有有效候选和稳定状态快照才能构建计划。
    if (!registry || !current_candidate || !states || !out || current_candidate->candidate_only != 1 ||
        (kind != EM_START && kind != EM_STOP) || cleanup > 1)
        return SYS_ERR_INVALID_ARG;
    //禁止输出覆盖候选或状态快照。
    if (V2_Overlap(out, sizeof(*out), current_candidate, sizeof(*current_candidate)) ||
        V2_Overlap(out, sizeof(*out), registry, sizeof(*registry)) ||
        V2_Overlap(out, sizeof(*out), states, sizeof(*states) * V2_MODULE_MAX))
        return SYS_ERR_INVALID_ARG;
    result = V2Registry_Validate(registry, &issue);
    if (result)
        return result;
    if (!current_candidate->modules.count || current_candidate->modules.count > registry->count || current_candidate->modules.selected_mask >> registry->count)
        return SYS_ERR_PROTOCOL;
    //按已验证的模块顺序检查状态或追加启动步骤。
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
    //顺序表必须精确覆盖所有选中模块。
    if (seen != current_candidate->modules.selected_mask)
        return SYS_ERR_PROTOCOL;
    // 停止计划必须检查仍运行的依赖者，目标候选不能隐藏它们。此处不进行运行时增量重配置。
    for (item_index = 0; item_index < registry->count; item_index++)
    {
        if (!LC_Stable(states[item_index]))
            return SYS_ERR_INVALID_STATE;
        if (!(seen & (1U << item_index)) && states[item_index] != LC_UNCONFIGURED)
            return SYS_ERR_BUSY;
    }
    //用同一注册表重新解析，防止接受不匹配的候选依赖。
    memset(&scratch, 0, sizeof(scratch));
    result = V2Resolver_Resolve(registry, current_candidate->modules.order, current_candidate->modules.count, &scratch, &verified, &issue);
    if (result)
        return result;
    if (memcmp(&verified, &current_candidate->modules, sizeof(verified)))
        return SYS_ERR_PROTOCOL;
    //停止目标必须是当前候选的非空子集。
    if (kind == EM_STOP && (!mask || (mask & ~seen)))
        return SYS_ERR_INVALID_ARG;
    // 如果停止集合外仍有运行模块依赖 Provider，就保留该 Provider。
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
    //计划只在本地生成，容量或状态失败时保留原输出。
    memset(&plan, 0, sizeof(plan));
    plan.kind = kind;
    //先配置未配置模块，再激活，不能颠倒阶段顺序。
    if (kind == EM_START)
    {
        //按已验证的模块顺序检查状态或追加启动步骤。
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
        //按已验证的模块顺序检查状态或追加启动步骤。
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
        //逆依赖顺序收尾，先处理使用者再处理提供方。
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
            //逆依赖顺序收尾，先处理使用者再处理提供方。
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
    //所有步骤生成成功后一次性提交给调用方。
    *out = plan;
    return SYS_OK;
}
