// 按依赖计划协调模块操作、异步完成和失败补偿。

#include "execution_manager.h"
#include <string.h>
#define MAGIC 0x454D5633U
// 校验所有者调用并设置重入保护，调用方仍负责跨任务串行化。
static SysResult_t enter(EMContext *manager)
{
    if (!manager || manager->magic != MAGIC || !manager->is_owner(manager->owner))
        return SYS_ERR_INVALID_STATE;
    if (manager->busy)
        return SYS_ERR_BUSY;
    manager->busy = 1;
    return SYS_OK;
}
// 退出当前调用的重入保护。
static SysResult_t leave(EMContext *manager, SysResult_t result)
{
    manager->busy = 0;
    return result;
}
// 记录首个错误，后续恢复结果不能覆盖它。
static void first(EMContext *manager, SysResult_t result)
{
    if (result && !manager->first_error)
        manager->first_error = result;
}
// 在已注册的模块端口中查找指定执行目标。
static EMEndpoint *port(EMContext *manager, uint32_t module_id)
{
    uint32_t endpoint_index;
    for (endpoint_index = 0; endpoint_index < manager->endpoint_count; endpoint_index++)
        if (manager->endpoints[endpoint_index].module == module_id)
            return &manager->endpoints[endpoint_index];
    return NULL;
}
// 记录验证失败及其位置，便于定位错误配置。
static void fail(EMContext *manager, SysResult_t result)
{
    first(manager, result);
    if (manager->state == EM_COMPENSATING)
    {
        if (!manager->recovery_error)
            manager->recovery_error = result;
        manager->state = EM_BLOCKED;
    }
    else if (manager->plan.kind == EM_START && !manager->blocked_unknown)
    {
        manager->state = EM_COMPENSATING;
        manager->stage = 0;
    }
    else
        manager->state = EM_BLOCKED;
}
// 绑定执行所有者及模块端口，初始化计划执行状态。
SysResult_t EM_Init(EMContext *manager, uint32_t session, const EMEndpoint *endpoint, uint32_t endpoint_count,
                    int (*owner)(void *), void *ctx)
{
    uint32_t endpoint_index, previous_index;
    if (!manager || manager->magic || !session || !endpoint || !endpoint_count || endpoint_count > V2_MODULE_MAX || !owner)
        return SYS_ERR_INVALID_ARG;
    if (V2_Overlap(manager, sizeof(*manager), endpoint, sizeof(*endpoint) * endpoint_count))
        return SYS_ERR_INVALID_ARG;
    if (!owner(ctx))
        return SYS_ERR_INVALID_STATE;
    for (endpoint_index = 0; endpoint_index < endpoint_count; endpoint_index++)
    {
        if (!endpoint[endpoint_index].module || !endpoint[endpoint_index].submit || !endpoint[endpoint_index].read || !endpoint[endpoint_index].consume)
            return SYS_ERR_INVALID_ARG;
        for (previous_index = 0; previous_index < endpoint_index; previous_index++)
            if (endpoint[endpoint_index].module == endpoint[previous_index].module)
                return SYS_ERR_ALREADY_EXISTS;
    }
    memset(manager, 0, sizeof(*manager));
    manager->session = session;
    manager->endpoint_count = endpoint_count;
    memcpy(manager->endpoints, endpoint, endpoint_count * sizeof(*endpoint));
    manager->is_owner = owner;
    manager->owner = ctx;
    manager->magic = MAGIC;
    return SYS_OK;
}
// 装载已校验计划，保留执行事务身份。
SysResult_t EM_Load(EMContext *manager, const EMPlan *plan, uint32_t timeout)
{
    uint32_t step_index;
    SysResult_t result;
    if (!plan || plan->count > EM_STEP_MAX || (plan->kind != EM_START && plan->kind != EM_STOP) || !timeout ||
        timeout > 0x7FFFFFFFU || !manager || V2_Overlap(manager, sizeof(*manager), plan, sizeof(*plan)))
        return SYS_ERR_INVALID_ARG;
    result = enter(manager);
    if (result)
        return result;
    if (manager->state != EM_IDLE && manager->state != EM_DONE)
        return leave(manager, SYS_ERR_BUSY);
    for (step_index = 0; step_index < plan->count; step_index++)
        if (!port(manager, plan->steps[step_index].module) || !LC_Target(plan->steps[step_index].operation) ||
            !LC_Stable(plan->steps[step_index].expected) ||
            (plan->kind == EM_START && plan->steps[step_index].operation != LC_CONFIGURE &&
             plan->steps[step_index].operation != LC_ACTIVATE) ||
            (plan->kind == EM_STOP && plan->steps[step_index].operation != LC_DEACTIVATE &&
             plan->steps[step_index].operation != LC_CLEANUP) ||
            (plan->steps[step_index].operation == LC_CONFIGURE && plan->steps[step_index].expected != LC_UNCONFIGURED) ||
            (plan->steps[step_index].operation == LC_ACTIVATE && plan->steps[step_index].expected != LC_INACTIVE) ||
            (plan->steps[step_index].operation == LC_DEACTIVATE && plan->steps[step_index].expected != LC_ACTIVE) ||
            (plan->steps[step_index].operation == LC_CLEANUP && plan->steps[step_index].expected != LC_INACTIVE))
            return leave(manager, SYS_ERR_INVALID_ARG);
    manager->plan = *plan;
    manager->timeout = timeout;
    manager->index = 0;
    manager->stage = 0;
    manager->undo_count = 0;
    manager->first_error = 0;
    manager->recovery_error = 0;
    manager->blocked_unknown = 0;
    manager->state = EM_RUNNING;
    return leave(manager, SYS_OK);
}
// 冻结当前操作结果，供下一阶段读取。
static void finish(EMContext *manager)
{
    EMStep *step =
        manager->state == EM_COMPENSATING ? &manager->undo[manager->undo_count - 1] : &manager->plan.steps[manager->index];
    SysResult_t result = manager->completion.result;
    uint32_t fact = manager->completion.fact;
    if (manager->state == EM_RUNNING && manager->plan.kind == EM_START && fact == LC_Target(step->operation) &&
        fact != step->expected)
    {
        EMStep *undo_step = &manager->undo[manager->undo_count++];
        undo_step->module = step->module;
        undo_step->expected = fact;
        undo_step->operation = step->operation == LC_ACTIVATE ? LC_DEACTIVATE : LC_CLEANUP;
    }
    if (fact == LC_ERROR)
        manager->blocked_unknown = 1;
    if (result)
    {
        fail(manager, result);
        return;
    }
    if (fact != LC_Target(step->operation))
    {
        fail(manager, SYS_ERR_PROTOCOL);
        return;
    }
    if (manager->state == EM_COMPENSATING)
        manager->undo_count--;
    else
        manager->index++;
    manager->stage = 0;
    if (manager->first_error && manager->state == EM_RUNNING)
        fail(manager, manager->first_error);
}
// 推进声明式计划并基于实际完成结果安排补偿。
SysResult_t EM_Process(EMContext *manager, uint32_t now)
{
    SysResult_t result = enter(manager);
    EMEndpoint *endpoint;
    EMStep *step;
    if (result)
        return result;
    if (manager->state == EM_IDLE || manager->state == EM_DONE)
        return leave(manager, SYS_OK);
    if (manager->state == EM_BLOCKED)
        return leave(manager, manager->first_error ? manager->first_error : SYS_ERR_INVALID_STATE);
    if (!manager->stage)
    {
        if ((manager->state == EM_RUNNING && manager->index == manager->plan.count) ||
            (manager->state == EM_COMPENSATING && !manager->undo_count))
        {
            manager->state = EM_DONE;
            return leave(manager, SYS_OK);
        }
        if (manager->issued == UINT32_MAX)
        {
            fail(manager, SYS_ERR_NO_RESOURCE);
            return leave(manager, SYS_ERR_NO_RESOURCE);
        }
        step = manager->state == EM_COMPENSATING ? &manager->undo[manager->undo_count - 1] : &manager->plan.steps[manager->index];
        memset(&manager->current, 0, sizeof(manager->current));
        manager->current.id.session = manager->session;
        manager->current.id.request = ++manager->issued;
        manager->current.id.module = step->module;
        manager->current.id.operation = step->operation;
        manager->current.expected = step->expected;
        manager->current.timeout = manager->timeout;
        manager->started = now;
        manager->stage = 1;
    }
    endpoint = port(manager, manager->current.id.module);
    if ((uint32_t)(now - manager->started) >= manager->timeout)
    {
        first(manager, SYS_ERR_TIMEOUT);
        if (manager->stage == 1)
        {
            manager->stage = 0;
            fail(manager, SYS_ERR_TIMEOUT);
            return leave(manager, SYS_ERR_TIMEOUT);
        }
    }
    if (manager->stage == 1)
    {
        result = endpoint->submit(endpoint->port, &manager->current);
        if (result == SYS_ERR_BUSY || result == SYS_ERR_NO_RESOURCE)
            return leave(manager, result);
        if (result)
        {
            manager->stage = 0;
            fail(manager, result);
            return leave(manager, result);
        }
        manager->stage = 2;
        return leave(manager, SYS_OK);
    }
    if (manager->stage == 2)
    {
        LCCompletion completion;
        result = endpoint->read(endpoint->port, &manager->current.id, &completion);
        if (result == SYS_ERR_NOT_FOUND || result == SYS_ERR_BUSY || result == SYS_ERR_NO_RESOURCE)
            return leave(manager, result);
        if (result)
        {
            first(manager, result);
            manager->state = EM_BLOCKED;
            return leave(manager, result);
        }
        if (!LC_IdEqual(&completion.id, &manager->current.id) || completion.result > 0 || completion.actual_result > 0 ||
            !LC_Stable(completion.fact) ||
            (!completion.actual_result && completion.fact != LC_Target(manager->current.id.operation)) ||
            (completion.actual_result && completion.fact != manager->current.expected &&
             completion.fact != LC_Target(manager->current.id.operation) && completion.fact != LC_ERROR))
        {
            first(manager, SYS_ERR_PROTOCOL);
            manager->state = EM_BLOCKED;
            return leave(manager, SYS_ERR_PROTOCOL);
        }
        /* Freeze an observed business failure before transport consumption retries. */
        first(manager, completion.result);
        manager->completion = completion;
        manager->stage = 3;
        return leave(manager, SYS_OK);
    }
    result = endpoint->consume(endpoint->port, &manager->completion);
    if (result)
    {
        if (result != SYS_ERR_BUSY && result != SYS_ERR_NO_RESOURCE)
        {
            first(manager, result);
            manager->state = EM_BLOCKED;
        }
        return leave(manager, result);
    }
    finish(manager);
    return leave(manager, SYS_OK);
}
// 读取计划进度、首错及补偿状态。
SysResult_t EM_Status(EMContext *manager, uint32_t *state, SysResult_t *err, SysResult_t *recovery)
{
    SysResult_t result;
    if (!manager || !state || !err || !recovery || V2_Overlap(manager, sizeof(*manager), state, sizeof(*state)) ||
        V2_Overlap(manager, sizeof(*manager), err, sizeof(*err)) ||
        V2_Overlap(manager, sizeof(*manager), recovery, sizeof(*recovery)))
        return SYS_ERR_INVALID_ARG;
    result = enter(manager);
    if (result)
        return result;
    *state = manager->state;
    *err = manager->first_error;
    *recovery = manager->recovery_error;
    return leave(manager, SYS_OK);
}
