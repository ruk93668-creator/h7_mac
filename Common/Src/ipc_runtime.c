#include "ipc_runtime.h"
#include "ipc_core.h"
#include "ipc_config.h"
#include "ipc_fault.h"
#include <stddef.h>
#include <string.h>

//根据成功快照推进对端心跳观察
static void IpcRuntime_Observe(IpcRuntime_t *runtime, const IpcStatusSnapshot_t *peer, uint32_t now);

//建立运行期计时与诊断基线
SysResult_t IPC_Runtime_Start(IpcRuntime_t *runtime, uint32_t now)
{
    if (runtime == NULL)
        return SYS_ERR_INVALID_ARG;
    if (runtime->initialized)
        return SYS_ERR_ALREADY_EXISTS;

    //启动期计时从本核开始运行时起算，不采用共享区残留Tick
    memset(runtime, 0, sizeof(*runtime));
    runtime->initialized = true;
    runtime->start_tick = now;
    runtime->step_tick = now;
    runtime->status.peer_state = IPC_PEER_STARTING;
    return SYS_OK;
}

//推进心跳、读取和超时诊断
SysResult_t IPC_Runtime_Process(IpcRuntime_t *runtime, uint32_t now)
{
    IpcStatusSnapshot_t peer;
    IpcState_t local;
    SysResult_t result;
    uint32_t gap;
    uint32_t age;

    if (runtime == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!runtime->initialized)
        return SYS_ERR_INVALID_STATE;
    gap = (uint32_t)(now - runtime->step_tick);
    if (runtime->processed && gap < IPC_HEARTBEAT_PERIOD_MS)
        return SYS_OK;

    //调度推迟只推进一次，记录实际间隔，不伪造多次心跳
    if (gap > runtime->status.max_step_gap_ms)
        runtime->status.max_step_gap_ms = gap;
    if (gap >= 2U * IPC_HEARTBEAT_PERIOD_MS)
        runtime->status.scheduling_delays++;
    runtime->step_tick = now;
    runtime->processed = true;
    result = IPC_Core_ReadPeer(&peer);
    runtime->status.read_result = result;
    if (result == SYS_OK)
        IpcRuntime_Observe(runtime, &peer, now);
    else if (result == SYS_ERR_BUSY)
        runtime->status.busy_reads++;
    else if (result == SYS_ERR_PROTOCOL)
    {
        runtime->status.protocol_errors++;
        runtime->status.fault_flags |= IPC_FAULT_SHARED_INVALID;
    }
    else
        runtime->status.read_errors++;

    //无论读取是否成功，都用本地时钟检查最后一次已观察进展
    age = (uint32_t)(now - (runtime->status.progress_seen != 0U ?
        runtime->progress_tick : runtime->start_tick));
    runtime->status.peer_age_ms = age;
    if (age >= IPC_HEARTBEAT_TIMEOUT_MS)
    {
        if (!runtime->timed_out)
        {
            runtime->status.timeout_events++;
            if (runtime->status.progress_seen == 0U)
                runtime->status.startup_timeouts++;
        }
        runtime->timed_out = true;
        runtime->status.fault_flags |= IPC_FAULT_HEARTBEAT_TIMEOUT;
    }

    //协议错误和时间超时分别累计，单次BUSY不会立即变成失联
    if (result == SYS_ERR_PROTOCOL)
        runtime->status.peer_state = IPC_PEER_PROTOCOL_ERROR;
    else if (runtime->timed_out)
        runtime->status.peer_state = runtime->status.progress_seen != 0U ?
            IPC_PEER_TIMEOUT : IPC_PEER_STARTUP_TIMEOUT;
    else
        runtime->status.peer_state = runtime->status.progress_seen != 0U ?
            IPC_PEER_ONLINE : IPC_PEER_STARTING;

    //心跳按uint32_t自然回绕；当前错误与历史累计故障分开
    local.ready = IPC_CORE_READY;
    local.heartbeat = runtime->status.local_heartbeat + 1U;
    local.fault_flags = runtime->status.fault_flags;
    local.last_result = result == SYS_ERR_PROTOCOL ? SYS_ERR_PROTOCOL :
        (runtime->timed_out ? SYS_ERR_TIMEOUT : SYS_OK);
    result = IPC_Core_PublishRuntime(&local);
    runtime->status.publish_result = result;
    if (result == SYS_OK)
        runtime->status.local_heartbeat = local.heartbeat;
    return result;
}

//只把后续心跳新值视为进展，Ready和第一次采样都不能单独授权ONLINE
static void IpcRuntime_Observe(IpcRuntime_t *runtime, const IpcStatusSnapshot_t *peer, uint32_t now)
{
    if (peer->ready != IPC_CORE_READY)
        return;
    if (!runtime->sampled)
    {
        runtime->sampled = true;
        runtime->status.peer_heartbeat = peer->heartbeat;
        return;
    }
    if (peer->heartbeat == runtime->status.peer_heartbeat)
        return;

    //只比较是否变化，允许UINT32_MAX到0，也允许跳过多个计数
    runtime->status.peer_heartbeat = peer->heartbeat;
    runtime->status.progress_seen = 1U;
    runtime->progress_tick = now;
    runtime->timed_out = false;
}
