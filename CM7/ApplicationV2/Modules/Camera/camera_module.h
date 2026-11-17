#ifndef V2_CAMERA_MODULE_H
#define V2_CAMERA_MODULE_H
#include "../../Configuration/v2_composition.h"
#include "../../Lifecycle/managed_lifecycle.h"
#include "../../Resource/media_resource.h"
#include "camera_format.h"
#define VC_DEVICE_RUNNING 1U
#define VC_MEDIA_RUNNING 2U
#define VC_MEDIA_STOPPED 4U
#define VC_DEVICE_READY 8U
#define VC_DMA_QUIET 16U
#define VC_BRIDGE_IDLE 32U
#define VC_CONSUMERS_IDLE 64U
#define VC_TRANSFERS_IDLE 128U
#define VC_NEVER_STARTED 256U
#define VC_STOP_MASK                                                                               \
    (VC_MEDIA_STOPPED | VC_DEVICE_READY | VC_DMA_QUIET | VC_BRIDGE_IDLE | VC_CONSUMERS_IDLE |      \
     VC_TRANSFERS_IDLE)
#define VC_COLD_MASK                                                                               \
    (VC_NEVER_STARTED | VC_DMA_QUIET | VC_BRIDGE_IDLE | VC_CONSUMERS_IDLE | VC_TRANSFERS_IDLE)
#define VC_GATE_BEGIN 1U
#define VC_GATE_PROVE 2U
typedef struct
{
    uint32_t flags, stop_sequence;
    SysResult_t stop_result, media_result;
    uint32_t stop_kind;
} VCameraObservation;
typedef struct
{
    V2ExecId id;
    SysResult_t result;
    VCameraObservation observation;
} VCameraResult;
typedef struct
{
    MRSource raw_source, jpeg_source;
    MRContext *raw_resource, *jpeg_resource; /* existing one-per-physical-source contexts */
} VCameraResources;
typedef struct
{
    int (*is_owner)(void *);
    SysResult_t (*validate_layout)(void *, const CoreMediaFormat *, const CameraConfig_t *,
                                   const MRSourceCaps *);
    /* Pure authoritative precondition/completion proof. Missing proof must fail. */
    SysResult_t (*gate)(void *, const LCRequest *, uint32_t stage);
    SysResult_t (*request)(void *, const LCRequest *,
                           const CameraConfig_t *); /* intent only for STOP */
    SysResult_t (*read_result)(void *, const V2ExecId *, VCameraResult *);
    SysResult_t (*consume_result)(void *, const VCameraResult *);
    SysResult_t (*facts)(void *, VCameraObservation *);
    SysResult_t (*release_identity)(void *, const MRRelease *); /* R -> original G; never revokes */
    void (*begin_round)(void *);
    void (*process)(void *);      /* necessary protective STOP remains inside existing owner */
    void (*returns)(void *);      /* Bridge / Storage Return */
    void (*finish_round)(void *); /* sole ordinary STOP coordinator */
} VCameraOps;
typedef struct
{
    uint32_t magic, busy, round, bound, prepared, work, submitted, decided, delivered;
    SysResult_t first_fault, port_error;
    LCContext lifecycle; /* only lifecycle authority, no running/stopping replica */
    CoreMediaFormat format;
    CameraConfig_t native;
    LCRequest operation;
    VCameraResult result;
    VCameraObservation first_stop_failure;
    VCameraResources resources;
    const VCameraOps *ops;
    void *port;
} VCamera;
typedef struct
{
    uint32_t lifecycle, bound, prepared;
    VCameraObservation current, first_stop_failure;
    SysResult_t operation_error, first_fault, port_error;
} VCameraStatus;
typedef struct
{
    CameraConfig_t native;
    MRSourceCaps resource;
} VCameraCapabilities;
typedef struct
{
    MRRelease release;
    uint32_t backend;
    SysResult_t identity_result, resource_result;
} VCameraExitEvidence;
extern const struct V2LifecycleOps VCamera_LifecycleOps;
SysResult_t VCamera_Init(VCamera *, uint32_t module, uint32_t session, const VCameraOps *, void *,
                         const VCameraResources *);
SysResult_t VCamera_BindCandidate(VCamera *, const V2Registry *, const V2CompositionCandidate *);
SysResult_t VCamera_QueryCapabilities(VCamera *, const CoreMediaFormat *, VCameraCapabilities *);
SysResult_t VCamera_QueryStatus(VCamera *, VCameraStatus *);
SysResult_t VCamera_QueryExitEvidence(VCamera *, const MRRelease *, VCameraExitEvidence *);
SysResult_t VCamera_Request(VCamera *, const LCRequest *, uint32_t now);
/* Configure/Activate/Deactivate/Cleanup/ErrorProcessing use matching operation
 * in VCamera_Request. No direct native START/STOP alternative is provided. */
SysResult_t VCamera_BeginRound(VCamera *);
SysResult_t VCamera_Process(VCamera *, uint32_t now);
SysResult_t VCamera_Returns(VCamera *);
SysResult_t VCamera_FinishRound(VCamera *, uint32_t now);
SysResult_t VCamera_ReadCompletion(VCamera *, const V2ExecId *, LCCompletion *);
SysResult_t VCamera_ConfirmCompletion(VCamera *, const LCCompletion *);
#endif
