// 协调 Storage 生命周期、文件操作和实际退出证据。

#ifndef V2_STORAGE_MODULE_H
#define V2_STORAGE_MODULE_H

#include "../../Configuration/v2_composition.h"
#include "../../Lifecycle/managed_lifecycle.h"
#include "storage_backend.h"
#include "storage_consumer.h"
#include "storage_frame.h"

typedef struct
{
    const VStorageBackendOps *backend;
    void *backend_port;
    const VStorageFrameOps *frame;
    void *frame_port;
    const VStorageConsumerOps *consumer;
    void *consumer_port;
} VStoragePorts;

typedef struct
{
    uint32_t magic, busy, bound, work, drain, file_step, recovery_issued;
    uint32_t frames, interval_ms, sample_tick;
    SysResult_t first_fault, business_error, cleanup_error, recovery_result;
    LCContext lifecycle;
    CoreMediaFormat format;
    VStorageConfig config;
    VStorageCaps caps;
    VStorageFacts facts;
    VStorageConsumer consumer;
    VStorageFrame frame;
    VStoragePorts ports;
} VStorage;

typedef struct
{
    uint32_t lifecycle, drain, frame_state, frames, qualification;
    MRGrant grant;
    VStorageFacts files;
    SysResult_t first_fault, business_error, cleanup_error, recovery_result, camera_stop_result;
} VStorageStatus;

extern const struct V2LifecycleOps VStorage_LifecycleOps;

// Storage：绑定模块所有者及端口并初始化本地上下文。
SysResult_t VStorage_Init(VStorage *, uint32_t module, uint32_t session, const VStorageCaps *,
                          const VStoragePorts *);
// Storage：校验并绑定已选配置候选，保留底层应用配置的独立结果。
SysResult_t VStorage_BindCandidate(VStorage *, const V2Registry *, const V2CompositionCandidate *,
                                   const VStorageConfig *);
// Storage：查询模块端口公开的格式及运行能力。
SysResult_t VStorage_QueryCapabilities(VStorage *, const CoreMediaFormat *, const VStorageConfig *,
                                       uint32_t *interval_ms);
// Storage：登记带执行身份的生命周期请求，实际完成由模块轮次推进。
SysResult_t VStorage_Request(VStorage *, const LCRequest *, uint32_t now);
// 在 Storage 所有者上下文推进生命周期、文件收尾及归还。
SysResult_t VStorage_RunRound(VStorage *, uint32_t now);
// Storage：读取模块的生命周期状态和观察到的业务事实。
SysResult_t VStorage_QueryStatus(VStorage *, VStorageStatus *);
// Storage：分别读取退出所需的硬件、业务和媒体资源证据。
SysResult_t VStorage_QueryExitEvidence(VStorage *, const MRGrant *, uint32_t *flags);
// Storage：按身份读取模块已固定的完成结果。
SysResult_t VStorage_ReadCompletion(VStorage *, const V2ExecId *, LCCompletion *);
// Storage：确认与请求身份匹配的完成已消费。
SysResult_t VStorage_ConfirmCompletion(VStorage *, const LCCompletion *);
#endif
