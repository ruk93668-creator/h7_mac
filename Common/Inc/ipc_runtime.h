#ifndef IPC_RUNTIME_H
#define IPC_RUNTIME_H

#include "ipc_status.h"
#include <stdbool.h>

//本地诊断状态，不写入共享协议，也不证明对端属于新会话
typedef enum
{
    IPC_PEER_STARTING = 0,//尚未观察到后续心跳进展
    IPC_PEER_ONLINE,//已观察到进展且尚未超时
    IPC_PEER_STARTUP_TIMEOUT,//首次握手完成前已超时
    IPC_PEER_TIMEOUT,//已握手但后续心跳进展超时
    IPC_PEER_PROTOCOL_ERROR//最近读取发现协议错误
} IpcPeerState_t;

//本核诊断副本，通信状态与每次操作结果分别记录
typedef struct
{
    uint32_t peer_state;//当前对端观察状态，取IpcPeerState_t
    uint32_t local_heartbeat;//本核成功发布的心跳计数，允许回绕
    uint32_t peer_heartbeat;//最后观察到的对端有效心跳
    uint32_t progress_seen;//是否已经观察到后续心跳变化
    uint32_t peer_age_ms;//距最后进展或启动基线的本地毫秒数
    uint32_t busy_reads;//读取暂时BUSY的累计次数
    uint32_t protocol_errors;//协议校验失败的累计次数
    uint32_t read_errors;//其他读取错误的累计次数
    uint32_t timeout_events;//进入超时状态的次数，持续超时不重复累计
    uint32_t startup_timeouts;//尚未握手就超时的次数
    uint32_t scheduling_delays;//实际处理间隔达到两个周期的次数
    uint32_t max_step_gap_ms;//运行期处理的历史最大实际间隔
    uint32_t fault_flags;//本地累计故障位，恢复ONLINE后仍保留
    SysResult_t read_result;//最近一次对端读取结果
    SysResult_t publish_result;//最近一次本核发布结果
} IpcRuntimeStatus_t;

//只由唯一运行期执行者访问的本地上下文
typedef struct
{
    IpcRuntimeStatus_t status;//保存心跳、超时和发布读取诊断

    //CM7待发布的业务载荷，CM4只缓存对端业务副本
    IpcApplicationStatus_t application;
    IpcApplicationCache_t peer_application;//CM4保存CM7最近一次有效业务快照

    //仅本核使用的采样时刻，不放入共享协议
    uint32_t application_tick;
    bool application_timed;//是否具有与待发布载荷配套的采样时刻
    uint32_t start_tick;//本核运行期启动时间
    uint32_t progress_tick;//本核观察到最近新心跳的时间
    uint32_t step_tick;//上次实际处理时间
    bool initialized;//是否已启动本地运行期
    bool sampled;//是否已有对端Ready心跳基线
    bool processed;//是否已执行过首轮处理，避免伪造首轮间隔
    bool timed_out;//当前是否已达到无进展期限
} IpcRuntime_t;

//建立本地计时基线，不读取或比较对端绝对Tick
SysResult_t IPC_Runtime_Start(IpcRuntime_t *runtime, uint32_t now);

//按实际经过的本地时间推进一次心跳，不补发延迟期间的历史次数
SysResult_t IPC_Runtime_Process(IpcRuntime_t *runtime, uint32_t now);

#endif
