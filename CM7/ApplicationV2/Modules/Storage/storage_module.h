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

SysResult_t VStorage_Init(VStorage *, uint32_t module, uint32_t session, const VStorageCaps *,
                          const VStoragePorts *);
SysResult_t VStorage_BindCandidate(VStorage *, const V2Registry *, const V2CompositionCandidate *,
                                   const VStorageConfig *);
SysResult_t VStorage_QueryCapabilities(VStorage *, const CoreMediaFormat *, const VStorageConfig *,
                                       uint32_t *interval_ms);
SysResult_t VStorage_Request(VStorage *, const LCRequest *, uint32_t now);
SysResult_t VStorage_RunRound(VStorage *, uint32_t now);
SysResult_t VStorage_QueryStatus(VStorage *, VStorageStatus *);
SysResult_t VStorage_QueryExitEvidence(VStorage *, const MRGrant *, uint32_t *flags);
SysResult_t VStorage_ReadCompletion(VStorage *, const V2ExecId *, LCCompletion *);
SysResult_t VStorage_ConfirmCompletion(VStorage *, const LCCompletion *);
#endif
