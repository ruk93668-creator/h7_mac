#include "control_execution.h"
#include <string.h>

#define MAGIC 0x43584537U

static SysResult_t enter(CSExecutionEndpoint *e)
{
    if (!e || e->magic != MAGIC || !e->owner(e->port))
        return SYS_ERR_INVALID_STATE;
    if (e->busy)
        return SYS_ERR_BUSY;
    e->busy = 1U;
    return SYS_OK;
}

static SysResult_t leave(CSExecutionEndpoint *e, SysResult_t z)
{
    e->busy = 0U;
    return z;
}

SysResult_t CSExec_Init(CSExecutionEndpoint *e, uint32_t boot, EMContext *manager,
                        int (*owner)(void *), SysResult_t (*snapshot)(void *, CSSnapshot *),
                        void *p)
{
    uint32_t state;
    SysResult_t first, recovery;
    if (!e || e->magic || !boot || !manager || !owner || !snapshot)
        return SYS_ERR_INVALID_ARG;
    if (!owner(p) || EM_Status(manager, &state, &first, &recovery) || state != EM_IDLE)
        return SYS_ERR_INVALID_STATE;
    memset(e, 0, sizeof(*e));
    e->boot = boot;
    e->manager = manager;
    e->owner = owner;
    e->snapshot = snapshot;
    e->port = p;
    e->magic = MAGIC;
    return SYS_OK;
}

SysResult_t CSExec_Submit(CSExecutionEndpoint *e, const CSExecution *work)
{
    CSSnapshot actual = {0};
    SysResult_t z = enter(e);
    if (z)
        return z;
    if (!work || work->id.boot != e->boot || !work->id.session || !work->id.sequence ||
        !work->id.ticket || !work->profile || !work->revision ||
        work->expected.revision != work->revision ||
        work->expected.prepared_profile != work->profile)
        return leave(e, SYS_ERR_PROTOCOL);
    if (work->id.ticket == e->highest)
        return leave(e, memcmp(work, &e->accepted, sizeof(*work)) ? SYS_ERR_PROTOCOL : SYS_OK);
    if (work->id.ticket < e->highest)
        return leave(e, SYS_ERR_NOT_FOUND);
    if (e->outstanding)
        return leave(e, SYS_ERR_BUSY);
    z = e->snapshot(e->port, &actual);
    if (z)
        return leave(e, z);
    if (memcmp(&work->expected, &actual, sizeof(actual)))
        return leave(e, SYS_ERR_INVALID_STATE);
    /* 同一Execution owner核对一致快照并Load，中间不让出执行权限。 */
    z = EM_Load(e->manager, &work->plan, work->step_timeout);
    if (z)
        return leave(e, z);
    e->accepted = *work;
    e->highest = work->id.ticket;
    e->outstanding = 1U;
    e->ready = 0U;
    return leave(e, SYS_OK);
}

SysResult_t CSExec_Process(CSExecutionEndpoint *e, uint32_t now)
{
    CSExecResult result;
    SysResult_t z = enter(e);
    if (z)
        return z;
    if (!e->outstanding || e->ready)
        return leave(e, SYS_OK);
    (void)EM_Process(e->manager, now);
    z = EM_Status(e->manager, &result.state, &result.result, &result.recovery);
    if (z)
        return leave(e, z);
    if (result.state == EM_DONE || result.state == EM_BLOCKED)
    {
        result.id = e->accepted.id;
        e->result = result;
        e->ready = 1U;
    }
    return leave(e, SYS_OK);
}

SysResult_t CSExec_Read(CSExecutionEndpoint *e, const CSIdentity *id, CSExecResult *out)
{
    SysResult_t z = enter(e);
    if (z)
        return z;
    if (!out || V2_Overlap(e, sizeof(*e), out, sizeof(*out)))
        return leave(e, SYS_ERR_INVALID_ARG);
    if (!CS_IdEqual(id, &e->accepted.id))
        return leave(e, SYS_ERR_NOT_FOUND);
    if (!e->ready)
        return leave(e, SYS_ERR_BUSY);
    *out = e->result;
    return leave(e, SYS_OK);
}

SysResult_t CSExec_Consume(CSExecutionEndpoint *e, const CSExecResult *r)
{
    SysResult_t z = enter(e);
    if (z)
        return z;
    if (!e->ready || !r || memcmp(r, &e->result, sizeof(*r)))
        return leave(e, SYS_ERR_PROTOCOL);
    e->outstanding = 0U;
    return leave(e, SYS_OK);
}
