// 在 Storage 所有者上下文推进 Camera 资格事务。

#ifndef V2_STORAGE_CONSUMER_H
#define V2_STORAGE_CONSUMER_H
#include "../../Resource/media_source.h"
#include "app_camera_lifecycle_replay.h"
/* 以下是客户端事务进展，不是Camera的资格权威表。 */
#define VSC_IDLE 0U
#define VSC_QUERY 1U
#define VSC_ACQUIRE 2U
#define VSC_GRANTED 3U
#define VSC_RELEASE 4U
#define VSC_IDLE_REQUEST 5U
#define VSC_BLOCKED 6U
typedef struct
{
    int (*is_owner)(void *);
    AppLifecycleSendFn send;
    SysResult_t (*receive_reply)(void *, AppLifecycleMessage_t *);
} VStorageConsumerOps;
typedef struct
{
    uint32_t magic, busy, phase, generation_counter, generation, session, reserved, staged;
    AppLifecycleTxContext_t tx; /* 原C2.1.1工具，无第二套Replay */
    AppLifecycleMessage_t request;
    MRGrant grant;
    MRRelease release;
    SysResult_t result, camera_stop_result, business_error, cleanup_error;
    uint32_t release_flags;
    const VStorageConsumerOps *ops;
    void *port;
} VStorageConsumer;
// Storage：绑定资格事务的消费者身份和所有者。
SysResult_t VStorageConsumer_Init(VStorageConsumer *, const VStorageConsumerOps *, void *);
// Storage：启动原资格申请流程，授权以匹配回复为准。
SysResult_t VStorageConsumer_Begin(VStorageConsumer *);
// Storage：提交独立资格释放事务，等待真实释放确认。
SysResult_t VStorageConsumer_End(VStorageConsumer *, SysResult_t business_error,
                                 SysResult_t cleanup_error, uint32_t release_flags);
// Storage：推进资格事务和匹配回复，不绕过原 Tx/Rx。
SysResult_t VStorageConsumer_Process(VStorageConsumer *);
#endif
