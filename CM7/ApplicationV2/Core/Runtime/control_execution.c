// 在执行端提交、推进和确认控制操作。

#include "control_execution.h"
#include <string.h>

#define MAGIC 0x43584537U

// 校验所有者调用并设置重入保护，调用方仍负责跨任务串行化。
static SysResult_t enter(CSExecutionEndpoint *endpoint)
{
    if (!endpoint || endpoint->magic != MAGIC || !endpoint->owner(endpoint->port))
        return SYS_ERR_INVALID_STATE;
    if (endpoint->busy)
        return SYS_ERR_BUSY;
    endpoint->busy = 1U;
    return SYS_OK;
}

// 退出当前调用的重入保护。
static SysResult_t leave(CSExecutionEndpoint *endpoint, SysResult_t result)
{
    endpoint->busy = 0U;
    return result;
}

// 初始化对应执行端点并绑定其所有者和端口。
SysResult_t CSExec_Init(CSExecutionEndpoint *endpoint, uint32_t boot, EMContext *manager,
                        int (*owner)(void *), SysResult_t (*snapshot)(void *, CSSnapshot *),
                        void *port_context)
{
    uint32_t state;
    SysResult_t first, recovery;
    if (!endpoint || endpoint->magic || !boot || !manager || !owner || !snapshot)
        return SYS_ERR_INVALID_ARG;
    if (!owner(port_context) || EM_Status(manager, &state, &first, &recovery) || state != EM_IDLE)
        return SYS_ERR_INVALID_STATE;
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->boot = boot;
    endpoint->manager = manager;
    endpoint->owner = owner;
    endpoint->snapshot = snapshot;
    endpoint->port = port_context;
    endpoint->magic = MAGIC;
    return SYS_OK;
}

// 提交带完整身份的异步执行请求。
SysResult_t CSExec_Submit(CSExecutionEndpoint *endpoint, const CSExecution *work)
{
    CSSnapshot actual = {0};
    SysResult_t result = enter(endpoint);
    if (result)
        return result;
    if (!work || work->id.boot != endpoint->boot || !work->id.session || !work->id.sequence ||
        !work->id.ticket || !work->profile || !work->revision ||
        work->expected.revision != work->revision ||
        work->expected.prepared_profile != work->profile)
        return leave(endpoint, SYS_ERR_PROTOCOL);
    if (work->id.ticket == endpoint->highest)
        return leave(endpoint, memcmp(work, &endpoint->accepted, sizeof(*work)) ? SYS_ERR_PROTOCOL : SYS_OK);
    if (work->id.ticket < endpoint->highest)
        return leave(endpoint, SYS_ERR_NOT_FOUND);
    if (endpoint->outstanding)
        return leave(endpoint, SYS_ERR_BUSY);
    result = endpoint->snapshot(endpoint->port, &actual);
    if (result)
        return leave(endpoint, result);
    if (memcmp(&work->expected, &actual, sizeof(actual)))
        return leave(endpoint, SYS_ERR_INVALID_STATE);
    /* 同一Execution owner核对一致快照并Load，中间不让出执行权限。 */
    result = EM_Load(endpoint->manager, &work->plan, work->step_timeout);
    if (result)
        return leave(endpoint, result);
    endpoint->accepted = *work;
    endpoint->highest = work->id.ticket;
    endpoint->outstanding = 1U;
    endpoint->ready = 0U;
    return leave(endpoint, SYS_OK);
}

// 在执行所有者上下文推进请求及结果运输。
SysResult_t CSExec_Process(CSExecutionEndpoint *endpoint, uint32_t now)
{
    CSExecResult result;
    SysResult_t status_result = enter(endpoint);
    if (status_result)
        return status_result;
    if (!endpoint->outstanding || endpoint->ready)
        return leave(endpoint, SYS_OK);
    (void)EM_Process(endpoint->manager, now);
    status_result = EM_Status(endpoint->manager, &result.state, &result.result, &result.recovery);
    if (status_result)
        return leave(endpoint, status_result);
    if (result.state == EM_DONE || result.state == EM_BLOCKED)
    {
        result.id = endpoint->accepted.id;
        endpoint->result = result;
        endpoint->ready = 1U;
    }
    return leave(endpoint, SYS_OK);
}

// 按指定执行身份读取已冻结结果。
SysResult_t CSExec_Read(CSExecutionEndpoint *endpoint, const CSIdentity *id, CSExecResult *out)
{
    SysResult_t result = enter(endpoint);
    if (result)
        return result;
    if (!out || V2_Overlap(endpoint, sizeof(*endpoint), out, sizeof(*out)))
        return leave(endpoint, SYS_ERR_INVALID_ARG);
    if (!CS_IdEqual(id, &endpoint->accepted.id))
        return leave(endpoint, SYS_ERR_NOT_FOUND);
    if (!endpoint->ready)
        return leave(endpoint, SYS_ERR_BUSY);
    *out = endpoint->result;
    return leave(endpoint, SYS_OK);
}

// 确认执行结果已消费，允许相应运输记录退休。
SysResult_t CSExec_Consume(CSExecutionEndpoint *endpoint, const CSExecResult *execution_result)
{
    SysResult_t result = enter(endpoint);
    if (result)
        return result;
    if (!endpoint->ready || !execution_result || memcmp(execution_result, &endpoint->result, sizeof(*execution_result)))
        return leave(endpoint, SYS_ERR_PROTOCOL);
    endpoint->outstanding = 0U;
    return leave(endpoint, SYS_OK);
}
