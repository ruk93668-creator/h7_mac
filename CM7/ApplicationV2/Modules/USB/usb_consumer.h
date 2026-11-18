#ifndef V2_USB_CONSUMER_H
#define V2_USB_CONSUMER_H
#include "../../Resource/media_source.h"
#include "app_camera_lifecycle_replay.h"
/* 以下是客户端事务进展，不是Camera的资格权威表。 */
#define VUC_IDLE 0U
#define VUC_QUERY 1U
#define VUC_ACQUIRE 2U
#define VUC_GRANTED 3U
#define VUC_RELEASE 4U
#define VUC_IDLE_REQUEST 5U
#define VUC_BLOCKED 6U
typedef struct
{
    int (*is_owner)(void *);
    AppLifecycleSendFn send;
    SysResult_t (*receive_reply)(void *, AppLifecycleMessage_t *);
} VUSBConsumerOps;
typedef struct
{
    uint32_t magic, busy, phase, generation_counter, generation, session, reserved, staged;
    AppLifecycleTxContext_t tx; /* 原C2.1.1工具，无第二套Replay */
    AppLifecycleMessage_t request;
    MRGrant grant;
    MRRelease release;
    SysResult_t result, first_error, camera_stop_result, business_error;
    const VUSBConsumerOps *ops;
    void *port;
} VUSBConsumer;
SysResult_t VUSBConsumer_Init(VUSBConsumer *, const VUSBConsumerOps *, void *);
SysResult_t VUSBConsumer_Begin(VUSBConsumer *);
SysResult_t VUSBConsumer_End(VUSBConsumer *, SysResult_t business_error);
SysResult_t VUSBConsumer_Process(VUSBConsumer *);
#endif
