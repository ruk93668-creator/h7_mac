// 在执行端与 USB 任务间运输请求及完成确认。

#ifndef V2_USB_ENDPOINT_H
#define V2_USB_ENDPOINT_H
#include "../../Supervisor/execution_manager.h"
#include "usb_module.h"
#define VU_ACTOR_USB 1U
#define VU_ACTOR_SUPERVISOR 2U
/* 独立执行传输值，保持旧52B/56B消息不变。拒绝提交时
 * 单独返回错误，不凭空构造稳定状态。 */
typedef struct
{
    LCCompletion completion;
    SysResult_t delivery_error;
} VUSBReply;
// 端口核验任务角色并完整复制消息。BUSY 或 NO_RESOURCE 表示未入队，通知只负责唤醒。
// 队列存储由运输层持有，Supervisor 与模块各自维护私有 Context。
typedef struct
{
    int (*is_usb)(void *);
    int (*is_supervisor)(void *);
    SysResult_t (*send_request)(void *, const LCRequest *);
    SysResult_t (*receive_request)(void *, LCRequest *);
    SysResult_t (*send_reply)(void *, const VUSBReply *);
    SysResult_t (*receive_reply)(void *, VUSBReply *);
    SysResult_t (*send_consumed)(void *, const LCCompletion *);
    SysResult_t (*receive_consumed)(void *, LCCompletion *);
    SysResult_t (*notify)(void *, uint32_t actor);
} VUSBTransportOps;
typedef struct
{
    const VUSBTransportOps *ops;
    void *port;
} VUSBTransport;
typedef struct
{
    uint32_t magic, busy, module, session, active, has_reply;
    LCRequest request;
    VUSBReply reply;
    VUSBTransport transport;
} VUSBClient; // 仅 Supervisor 所有者访问，不引用 USB 或 LCContext。
typedef struct
{
    uint32_t magic, busy, active, accepted, ready, sent, ack_pending;
    LCRequest request;
    VUSBReply reply;
    LCCompletion ack;
    VUSB *usb;
    VUSBTransport transport;
} VUSBEndpoint; // 仅 USB 所有者访问。
SysResult_t VUSBClient_Init(VUSBClient *, uint32_t module, uint32_t session, const VUSBTransport *);
// USB：返回供 Execution Manager 使用的模块端口。
SysResult_t VUSBClient_Endpoint(VUSBClient *, EMEndpoint *);
// USB：初始化模块任务端并绑定请求及回复运输端口。
SysResult_t VUSBEndpoint_Init(VUSBEndpoint *, VUSB *, const VUSBTransport *);
// USB：推进模块轮次和可靠消息，区分回复入队与消费确认。
SysResult_t VUSBEndpoint_RunRound(VUSBEndpoint *, uint32_t now);
#endif
