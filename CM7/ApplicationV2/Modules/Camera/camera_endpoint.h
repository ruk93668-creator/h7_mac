// Camera 客户端与任务端的请求、完成和消费适配。

#ifndef V2_CAMERA_ENDPOINT_H
#define V2_CAMERA_ENDPOINT_H
#include "../../Supervisor/execution_manager.h"
#include "camera_module.h"
#define VC_ACTOR_CAMERA 1U
#define VC_ACTOR_SUPERVISOR 2U
/* Local transport value, NOT a new 52B/56B wire protocol. A transport error
 * preserves a rejected/unknown operation without inventing a stable fact. */
typedef struct
{
    LCCompletion completion;
    SysResult_t delivery_error;
} VCameraReply;
/* Existing queue adapters must provide reliable commit/copy and role checks.
 * NO_RESOURCE/BUSY means not committed. Notification is only a wakeup hint.
 * Buffers are owned by transport; no module Context is shared with Supervisor. */
typedef struct
{
    int (*is_camera)(void *);
    int (*is_supervisor)(void *);
    SysResult_t (*send_request)(void *, const LCRequest *);
    SysResult_t (*receive_request)(void *, LCRequest *);
    SysResult_t (*send_reply)(void *, const VCameraReply *);
    SysResult_t (*receive_reply)(void *, VCameraReply *);
    SysResult_t (*send_consumed)(void *, const LCCompletion *);
    SysResult_t (*receive_consumed)(void *, LCCompletion *);
    SysResult_t (*notify)(void *, uint32_t actor);
} VCameraTransportOps;
typedef struct
{
    const VCameraTransportOps *ops;
    void *port;
} VCameraTransport;
typedef struct
{
    uint32_t magic, busy, module, session, active, has_reply;
    LCRequest request;
    VCameraReply reply;
    VCameraTransport transport;
} VCameraClient; /* Supervisor owner only. No pointer to Camera/LCContext. */
typedef struct
{
    uint32_t magic, busy, active, accepted, ready, sent, ack_pending;
    LCRequest request;
    VCameraReply reply;
    LCCompletion ack;
    VCamera *camera;
    VCameraTransport transport;
} VCameraEndpoint; /* Camera owner only. */
SysResult_t VCameraClient_Init(VCameraClient *, uint32_t module, uint32_t session,
                               const VCameraTransport *);
// Camera：返回供 Execution Manager 使用的模块端口。
SysResult_t VCameraClient_Endpoint(VCameraClient *, EMEndpoint *);
// Camera：初始化模块任务端并绑定请求及回复运输端口。
SysResult_t VCameraEndpoint_Init(VCameraEndpoint *, VCamera *, const VCameraTransport *);
// Camera：推进模块轮次和可靠消息，区分回复入队与消费确认。
SysResult_t VCameraEndpoint_RunRound(VCameraEndpoint *, uint32_t now);
#endif
