// 在执行端与 Storage 任务间运输请求及完成确认。

#ifndef V2_STORAGE_ENDPOINT_H
#define V2_STORAGE_ENDPOINT_H
#include "../../Supervisor/execution_manager.h"
#include "storage_module.h"
#define VST_ACTOR_STORAGE 1U
#define VST_ACTOR_SUPERVISOR 2U
/* 独立执行传输值，保持旧52B/56B消息不变。拒绝提交时
 * 单独返回错误，不凭空构造稳定状态。 */
typedef struct
{
    LCCompletion completion;
    SysResult_t delivery_error;
} VStorageReply;
/* Existing queue adapters must provide reliable commit/copy and role checks.
 * NO_RESOURCE/BUSY means not committed. Notification is only a wakeup hint.
 * Buffers are owned by transport; no module Context is shared with Supervisor. */
typedef struct
{
    int (*is_storage)(void *);
    int (*is_supervisor)(void *);
    SysResult_t (*send_request)(void *, const LCRequest *);
    SysResult_t (*receive_request)(void *, LCRequest *);
    SysResult_t (*send_reply)(void *, const VStorageReply *);
    SysResult_t (*receive_reply)(void *, VStorageReply *);
    SysResult_t (*send_consumed)(void *, const LCCompletion *);
    SysResult_t (*receive_consumed)(void *, LCCompletion *);
    SysResult_t (*notify)(void *, uint32_t actor);
} VStorageTransportOps;
typedef struct
{
    const VStorageTransportOps *ops;
    void *port;
} VStorageTransport;
typedef struct
{
    uint32_t magic, busy, module, session, active, has_reply;
    LCRequest request;
    VStorageReply reply;
    VStorageTransport transport;
} VStorageClient; /* Supervisor owner only. No pointer to Storage/LCContext. */
typedef struct
{
    uint32_t magic, busy, active, accepted, ready, sent, ack_pending;
    LCRequest request;
    VStorageReply reply;
    LCCompletion ack;
    VStorage *storage;
    VStorageTransport transport;
} VStorageEndpoint; /* Storage owner only. */
SysResult_t VStorageClient_Init(VStorageClient *, uint32_t module, uint32_t session,
                                const VStorageTransport *);
// Storage：返回供 Execution Manager 使用的模块端口。
SysResult_t VStorageClient_Endpoint(VStorageClient *, EMEndpoint *);
// Storage：初始化模块任务端并绑定请求及回复运输端口。
SysResult_t VStorageEndpoint_Init(VStorageEndpoint *, VStorage *, const VStorageTransport *);
// Storage：推进模块轮次和可靠消息，区分回复入队与消费确认。
SysResult_t VStorageEndpoint_RunRound(VStorageEndpoint *, uint32_t now);
#endif
