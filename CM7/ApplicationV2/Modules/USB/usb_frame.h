#ifndef V2_USB_FRAME_H
#define V2_USB_FRAME_H
#include "../../Resource/media_resource.h"
#define VUF_IDLE 0U
#define VUF_PREPARING 1U
#define VUF_PREPARED 2U
#define VUF_COMMITTED 3U
#define VUF_OFFERED 4U
#define VUF_IN_UVC 5U
#define VUF_RETURNING 6U
#define VUF_WAIT_ACK 7U
#define VUF_CONSUMING 8U
#define VUF_RETIRING 9U
#define VUF_CANCELLING 10U
#define VUF_BLOCKED 11U
#define VUF_FRAME 1U
#define VUF_TERMINAL 2U
typedef struct
{
    MRMedia media;
    MRBridge bridge;
    MRView view;
} VUSBFrameData;
typedef struct
{
    MRRequest request;
    uint32_t kind;
    VUSBFrameData data;
    MRReceipt terminal;
} VUSBDelivery;
/* USB端只存交接进展，不持有第二份物理引用计数。所有MR操作在Camera owner。 */
typedef struct
{
    int (*is_owner)(void *);
    SysResult_t (*prepare)(void *, const MRGrant *);
    SysResult_t (*prepared)(void *, MRRequest *);
    SysResult_t (*commit)(void *, const MRRequest *);
    SysResult_t (*cancel)(void *, const MRRequest *);
    SysResult_t (*cancelled)(void *, const MRRequest *);
    SysResult_t (*offer)(void *, VUSBDelivery *);
    SysResult_t (*submit_uvc)(void *, const MRRequest *, const VUSBFrameData *);
    SysResult_t (*readers_safe)(void *, const MRRequest *, const VUSBFrameData *);
    SysResult_t (*return_frame)(void *, const MRRequest *, const VUSBFrameData *);
    SysResult_t (*return_ack)(void *, MRReceipt *);
    SysResult_t (*consumed)(void *, const MRReceipt *);
    SysResult_t (*retired)(void *, const MRReceipt *);
} VUSBFrameOps;
typedef struct
{
    uint32_t magic, busy, phase, highest_h;
    SysResult_t first_error;
    MRGrant preparing;
    MRRequest request;
    VUSBFrameData data;
    MRReceipt terminal;
    const VUSBFrameOps *ops;
    void *port;
} VUSBFrame;
SysResult_t VUSBFrame_Init(VUSBFrame *, const VUSBFrameOps *, void *);
SysResult_t VUSBFrame_Process(VUSBFrame *, const MRGrant *, const CoreMediaFormat *, int accepting);
int VUSBFrame_IsIdle(const VUSBFrame *);
#endif
