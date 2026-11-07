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

    //拒绝空上下文，避免访问尚不存在的运行期状态
    if (runtime == NULL)
        return SYS_ERR_INVALID_ARG;

    //拒绝重复启动，防止清掉已有心跳和超时诊断
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
#if defined(CORE_CM7)
    IpcApplicationStatus_t application;
    uint32_t delay;
#endif
    SysResult_t result;
    uint32_t gap;
    uint32_t age;

    if (runtime == NULL)
        return SYS_ERR_INVALID_ARG;

    //必须先建立本地计时基线，再推进运行期处理
    if (!runtime->initialized)
        return SYS_ERR_INVALID_STATE;

    //按本核经过时间限制处理频率，Tick回绕按无符号差值处理
    gap = (uint32_t)(now - runtime->step_tick);
    if (runtime->processed && gap < IPC_HEARTBEAT_PERIOD_MS)
        return SYS_OK;

    //调度推迟只推进一次，记录实际间隔，不伪造多次心跳
    if (gap > runtime->status.max_step_gap_ms)
        runtime->status.max_step_gap_ms = gap;

    //间隔达到两个周期时记录调度延迟，不补发虚假心跳
    if (gap >= 2U * IPC_HEARTBEAT_PERIOD_MS)
        runtime->status.scheduling_delays++;

    //记录本次实际处理时刻，作为下一周期的计时起点
    runtime->step_tick = now;
    runtime->processed = true;

    //先读取完整对端快照，失败时不使用局部字段
    result = IPC_Core_ReadPeer(&peer);
    runtime->status.read_result = result;
    if (result == SYS_OK)
    {

        //只有一致性读取成功，才允许观察心跳进展
        IpcRuntime_Observe(runtime, &peer, now);
#if defined(CORE_CM4)

        //CM4缓存CM7业务快照，业务更新与心跳进展分别跟踪
        IPC_Application_Observe(&runtime->peer_application, &peer.application, now);
#endif
    }

    //更新交错导致BUSY只计数，不能立即认定对端失联
    else if (result == SYS_ERR_BUSY)
        runtime->status.busy_reads++;

    //协议不匹配单独记录，不向未知共享布局写入故障
    else if (result == SYS_ERR_PROTOCOL)
    {
        runtime->status.protocol_errors++;
        runtime->status.fault_flags |= IPC_FAULT_SHARED_INVALID;
    }

    //其他读取错误单独累计，保留真实错误分类
    else
        runtime->status.read_errors++;

    //无论读取是否成功，都用本地时钟检查最后一次已观察进展
    age = (uint32_t)(now - (runtime->status.progress_seen != 0U ?
        runtime->progress_tick : runtime->start_tick));
    runtime->status.peer_age_ms = age;

    //连续未观察到心跳进展达到阈值，才判定超时
    if (age >= IPC_HEARTBEAT_TIMEOUT_MS)
    {

        //同一轮持续超时只计一次，恢复后再次超时才重新计数
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
#if defined(CORE_CM7)

    //复制CM7待发布业务数据，避免累计修改原始采样年龄
    application = runtime->application;
    delay = runtime->application_timed ? (uint32_t)(now - runtime->application_tick) : 0U;

    //把采样到处理入口的同核等待时间补入年龄，不发送绝对Tick
    //Camera有效时补入采样后的等待时间，溢出则保持最大年龄
    if ((application.valid_mask & IPC_APP_CAMERA) != 0U)
        application.camera_age_ms = application.camera_age_ms > UINT32_MAX - delay ?
            UINT32_MAX : application.camera_age_ms + delay;

    //USB独立补入等待时间，不能借其他来源保持新鲜
    if ((application.valid_mask & IPC_APP_USB) != 0U)
        application.usb_age_ms = application.usb_age_ms > UINT32_MAX - delay ?
            UINT32_MAX : application.usb_age_ms + delay;

    //Control独立补入等待时间，防止排队延迟被遗漏
    if ((application.valid_mask & IPC_APP_CONTROL) != 0U)
        application.control_age_ms = application.control_age_ms > UINT32_MAX - delay ?
            UINT32_MAX : application.control_age_ms + delay;

    //CM7将心跳和业务载荷作为同一份序号快照提交
    result = IPC_Core_PublishRuntimeApplication(&local, &application);
#else

    //CM4只发布自己的运行状态，不写CM7专用业务载荷
    result = IPC_Core_PublishRuntime(&local);
#endif

    //保留发布结果，只有提交成功才推进本地心跳计数
    runtime->status.publish_result = result;
    if (result == SYS_OK)
        runtime->status.local_heartbeat = local.heartbeat;
    return result;
}

//只把后续心跳新值视为进展，Ready和第一次采样都不能单独授权ONLINE
static void IpcRuntime_Observe(IpcRuntime_t *runtime, const IpcStatusSnapshot_t *peer, uint32_t now)
{

    //对端尚未准备好时，不把残留心跳当成运行进展
    if (peer->ready != IPC_CORE_READY)
        return;

    //首次心跳只建立比较基线，不能单凭Ready进入ONLINE
    if (!runtime->sampled)
    {
        runtime->sampled = true;
        runtime->status.peer_heartbeat = peer->heartbeat;
        return;
    }

    //计数未变说明没有新进展，不延长超时窗口
    if (peer->heartbeat == runtime->status.peer_heartbeat)
        return;

    //只比较是否变化，允许UINT32_MAX到0，也允许跳过多个计数
    runtime->status.peer_heartbeat = peer->heartbeat;
    runtime->status.progress_seen = 1U;
    runtime->progress_tick = now;
    runtime->timed_out = false;
}
