// 协调 Camera 配置、启停、保护性停止和真实退出证明。

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
// Camera：绑定模块所有者及端口并初始化本地上下文。
SysResult_t VCamera_Init(VCamera *, uint32_t module, uint32_t session, const VCameraOps *, void *,
                         const VCameraResources *);
// Camera：校验并绑定已选配置候选，保留底层应用配置的独立结果。
SysResult_t VCamera_BindCandidate(VCamera *, const V2Registry *, const V2CompositionCandidate *);
// Camera：查询模块端口公开的格式及运行能力。
SysResult_t VCamera_QueryCapabilities(VCamera *, const CoreMediaFormat *, VCameraCapabilities *);
// Camera：读取模块的生命周期状态和观察到的业务事实。
SysResult_t VCamera_QueryStatus(VCamera *, VCameraStatus *);
// Camera：分别读取退出所需的硬件、业务和媒体资源证据。
SysResult_t VCamera_QueryExitEvidence(VCamera *, const MRRelease *, VCameraExitEvidence *);
// Camera：登记带执行身份的生命周期请求，实际完成由模块轮次推进。
SysResult_t VCamera_Request(VCamera *, const LCRequest *, uint32_t now);
/* Configure/Activate/Deactivate/Cleanup/ErrorProcessing use matching operation
 * in VCamera_Request. No direct native START/STOP alternative is provided. */
SysResult_t VCamera_BeginRound(VCamera *);
// 推进本轮 Camera 配置、启动及采集端口操作。
SysResult_t VCamera_Process(VCamera *, uint32_t now);
// 推进 Camera 所有者负责的帧归还。
SysResult_t VCamera_Returns(VCamera *);
// 在本轮归还推进后处理停止并核对完成事实。
SysResult_t VCamera_FinishRound(VCamera *, uint32_t now);
// Camera：按身份读取模块已固定的完成结果。
SysResult_t VCamera_ReadCompletion(VCamera *, const V2ExecId *, LCCompletion *);
// Camera：确认与请求身份匹配的完成已消费。
SysResult_t VCamera_ConfirmCompletion(VCamera *, const LCCompletion *);
#endif
