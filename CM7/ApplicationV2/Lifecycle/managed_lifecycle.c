// 校验模块状态转换并保留可靠异步完成。

#include "managed_lifecycle.h"
#include <string.h>
#define MAGIC 0x4C435633U
// 比较完整事务身份，避免旧消息匹配当前操作。
int LC_IdEqual(const V2ExecId *left_identity, const V2ExecId *right_identity)
{
    return left_identity && right_identity && !memcmp(left_identity, right_identity, sizeof(*left_identity));
}
// 判断状态是否属于可受理新操作的稳定阶段。
int LC_Stable(uint32_t state)
{
    return state >= LC_UNCONFIGURED && state <= LC_ERROR;
}
// 计算指定过渡成功后的目标状态。
uint32_t LC_Target(uint32_t op)
{
    switch (op)
    {
    case LC_CONFIGURE:
    case LC_DEACTIVATE:
        return LC_INACTIVE;
    case LC_ACTIVATE:
        return LC_ACTIVE;
    case LC_CLEANUP:
    case LC_ERROR_PROCESS:
        return LC_UNCONFIGURED;
    default:
        return 0;
    }
}
// 校验请求的操作是否允许从当前稳定状态发起。
static int legal(uint32_t state, uint32_t op)
{
    return (op == LC_CONFIGURE && state == LC_UNCONFIGURED) ||
           (op == LC_ACTIVATE && state == LC_INACTIVE) || (op == LC_DEACTIVATE && state == LC_ACTIVE) ||
           (op == LC_CLEANUP && state == LC_INACTIVE) || (op == LC_ERROR_PROCESS && state == LC_ERROR);
}
// 校验所有者调用并设置重入保护，调用方仍负责跨任务串行化。
static SysResult_t enter(LCContext *lifecycle)
{
    if (!lifecycle || lifecycle->magic != MAGIC || !lifecycle->ops->is_owner(lifecycle->port))
        return SYS_ERR_INVALID_STATE;
    if (lifecycle->busy)
        return SYS_ERR_BUSY;
    lifecycle->busy = 1;
    return SYS_OK;
}
// 退出当前调用的重入保护。
static SysResult_t leave(LCContext *lifecycle, SysResult_t result)
{
    lifecycle->busy = 0;
    return result;
}
// 报告生命周期过渡的实际失败原因。
static void error(LCContext *lifecycle, SysResult_t result)
{
    if (result && !lifecycle->first_error)
        lifecycle->first_error = result;
}
// 绑定模块所有者和操作回调，初始化生命周期上下文。
SysResult_t LC_Init(LCContext *lifecycle, uint32_t module, uint32_t session,
                    const struct V2LifecycleOps *ops, void *port)
{
    if (!lifecycle || lifecycle->magic || !module || !session || !ops || !ops->is_owner || !ops->can_begin ||
        !ops->begin || !ops->prove)
        return SYS_ERR_INVALID_ARG;
    if (!ops->is_owner(port))
        return SYS_ERR_INVALID_STATE;
    memset(lifecycle, 0, sizeof(*lifecycle));
    lifecycle->module = module;
    lifecycle->session = session;
    lifecycle->state = LC_UNCONFIGURED;
    lifecycle->ops = ops;
    lifecycle->port = port;
    lifecycle->magic = MAGIC;
    return SYS_OK;
}
// 校验并登记过渡请求，受理不代表操作完成。
SysResult_t LC_Request(LCContext *lifecycle, const LCRequest *request, uint32_t now)
{
    SysResult_t result;
    if (!request || !request->id.request || !LC_Target(request->id.operation) || !request->timeout ||
        request->timeout > 0x7FFFFFFFU)
        return SYS_ERR_INVALID_ARG;
    result = enter(lifecycle);
    if (result)
        return result;
    if (request->id.module != lifecycle->module || request->id.session != lifecycle->session)
        return leave(lifecycle, SYS_ERR_PROTOCOL);
    if (request->id.request == lifecycle->request.id.request)
        return leave(lifecycle, !memcmp(request, &lifecycle->request, sizeof(*request)) ? SYS_OK : SYS_ERR_PROTOCOL);
    if (request->id.request < lifecycle->request.id.request)
        return leave(lifecycle, SYS_ERR_PROTOCOL);
    if (lifecycle->pending)
        return leave(lifecycle, SYS_ERR_BUSY);
    if ((lifecycle->sealed && (request->id.operation == LC_ACTIVATE || request->id.operation == LC_CONFIGURE)) ||
        request->expected != lifecycle->state || !legal(lifecycle->state, request->id.operation))
        return leave(lifecycle, SYS_ERR_INVALID_STATE);
    lifecycle->request = *request;
    lifecycle->started = now;
    lifecycle->pending = 1;
    lifecycle->dispatched = 0;
    lifecycle->decided = 0;
    lifecycle->terminal = 0;
    lifecycle->first_error = 0;
    lifecycle->guard_error = 0;
    memset(&lifecycle->completion, 0, sizeof(lifecycle->completion));
    lifecycle->completion.id = request->id;
    lifecycle->state = LC_TRANSITION_BASE + request->id.operation;
    return leave(lifecycle, SYS_OK);
}
// 接收匹配的实际操作完成结果。
SysResult_t LC_Complete(LCContext *lifecycle, const V2ExecId *id, SysResult_t actual, uint32_t fact)
{
    SysResult_t result = enter(lifecycle);
    if (result)
        return result;
    if (actual > 0 || !LC_Stable(fact) || !LC_IdEqual(id, &lifecycle->request.id))
        return leave(lifecycle, SYS_ERR_PROTOCOL);
    if (lifecycle->decided)
        return leave(lifecycle, lifecycle->completion.actual_result == actual && lifecycle->completion.fact == fact
                            ? SYS_OK
                            : SYS_ERR_PROTOCOL);
    if (!lifecycle->pending || !lifecycle->dispatched)
        return leave(lifecycle, SYS_ERR_INVALID_STATE);
    if ((!actual && fact != LC_Target(id->operation)) ||
        (actual && fact != lifecycle->request.expected && fact != LC_Target(id->operation) &&
         fact != LC_ERROR))
        return leave(lifecycle, SYS_ERR_PROTOCOL);
    lifecycle->completion.actual_result = actual;
    lifecycle->completion.fact = fact;
    lifecycle->decided = 1;
    error(lifecycle, actual);
    return leave(lifecycle, SYS_OK);
}
// 在所有者上下文推进正在进行的生命周期过渡。
SysResult_t LC_Process(LCContext *lifecycle, uint32_t now)
{
    SysResult_t result = enter(lifecycle);
    if (result)
        return result;
    if (!lifecycle->pending || lifecycle->terminal)
        return leave(lifecycle, SYS_OK);
    if ((uint32_t)(now - lifecycle->started) >= lifecycle->request.timeout)
        error(lifecycle, SYS_ERR_TIMEOUT);
    if (!lifecycle->dispatched && !lifecycle->decided)
    {
        if (lifecycle->first_error == SYS_ERR_TIMEOUT)
        {
            lifecycle->completion.actual_result = SYS_ERR_TIMEOUT;
            lifecycle->completion.fact = lifecycle->request.expected;
            lifecycle->decided = 1;
        }
        else
        {
            result = lifecycle->ops->can_begin(lifecycle->port, &lifecycle->request);
            if (!result)
                result = lifecycle->ops->begin(lifecycle->port, &lifecycle->request);
            if (result == SYS_ERR_BUSY || result == SYS_ERR_NO_RESOURCE)
                return leave(lifecycle, result);
            if (!result)
            {
                lifecycle->dispatched = 1;
                return leave(lifecycle, SYS_OK);
            }
            error(lifecycle, result);
            lifecycle->completion.actual_result = result;
            lifecycle->completion.fact = lifecycle->request.expected;
            lifecycle->decided = 1;
        }
    }
    if (!lifecycle->decided)
        return leave(lifecycle, lifecycle->first_error ? lifecycle->first_error : SYS_ERR_BUSY);
    // 失败或结果未知时保留 ERROR。只有模块和资源权威才能证明已恢复到稳定状态。
    if (lifecycle->completion.fact != LC_ERROR)
    {
        result = lifecycle->ops->prove(lifecycle->port, &lifecycle->request, lifecycle->completion.fact);
        if (result)
        {
            lifecycle->guard_error = result;
            if (result != SYS_ERR_BUSY && result != SYS_ERR_NO_RESOURCE)
                error(lifecycle, result);
            return leave(lifecycle, result);
        }
    }
    lifecycle->state = lifecycle->completion.fact;
    lifecycle->completion.result = lifecycle->first_error;
    lifecycle->terminal = 1;
    return leave(lifecycle, SYS_OK);
}
// 读取已固定的完成消息。
SysResult_t LC_Read(LCContext *lifecycle, const V2ExecId *id, LCCompletion *out)
{
    SysResult_t result;
    if (!out || !lifecycle || V2_Overlap(lifecycle, sizeof(*lifecycle), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    result = enter(lifecycle);
    if (result)
        return result;
    if (!LC_IdEqual(id, &lifecycle->request.id) || !lifecycle->terminal)
        return leave(lifecycle, SYS_ERR_NOT_FOUND);
    *out = lifecycle->completion;
    return leave(lifecycle, SYS_OK);
}
// 确认匹配完成已消费并允许相应记录退休。
SysResult_t LC_Confirm(LCContext *lifecycle, const LCCompletion *completion)
{
    SysResult_t result = enter(lifecycle);
    if (result)
        return result;
    if (!completion || !lifecycle->terminal || memcmp(completion, &lifecycle->completion, sizeof(*completion)))
        return leave(lifecycle, SYS_ERR_PROTOCOL);
    lifecycle->pending = 0;
    return leave(lifecycle, SYS_OK);
}
// 关闭新操作入口并保留既有事务状态。
SysResult_t LC_Seal(LCContext *lifecycle)
{
    SysResult_t result = enter(lifecycle);
    if (result)
        return result;
    lifecycle->sealed = 1;
    return leave(lifecycle, SYS_OK);
}
// 获取生命周期状态和过渡结果。
SysResult_t LC_Status(LCContext *lifecycle, uint32_t *state, SysResult_t *first)
{
    SysResult_t result;
    if (!state || !first || !lifecycle || V2_Overlap(lifecycle, sizeof(*lifecycle), state, sizeof(*state)) ||
        V2_Overlap(lifecycle, sizeof(*lifecycle), first, sizeof(*first)))
        return SYS_ERR_INVALID_ARG;
    result = enter(lifecycle);
    if (result)
        return result;
    *state = lifecycle->state;
    *first = lifecycle->first_error;
    return leave(lifecycle, SYS_OK);
}
