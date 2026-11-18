// 推进 Storage 帧交付、写入、归还和收据确认。

#ifndef V2_STORAGE_FRAME_H
#define V2_STORAGE_FRAME_H

#include "../../Resource/media_resource.h"

#define VSF_FREE 0U
#define VSF_POST 1U
#define VSF_WAIT 2U
#define VSF_READY 3U
#define VSF_RETURN 4U
#define VSF_ACK 5U
#define VSF_CONSUME 6U
#define VSF_RETIRE 7U
#define VSF_BLOCKED 8U
#define VSF_DELIVERY_FRAME 1U
#define VSF_DELIVERY_WITHDRAWN 2U
#define VSF_DELIVERY_TERMINAL 3U

typedef struct
{
    MRRequest request;
    uint32_t kind, timestamp;
    MRMedia media;
    MRView view;
    MRReceipt terminal;
} VStorageOffer;

/* Camera端串行Commit、Admit、Obtain/Retain、物理Return；Storage只提交和消费。
 * post/withdraw成功表示复制入队，其他返回表示没有提交。
 * WITHDRAWN只能由Camera证明从未建档，具有H的交接走正常终结收据。 */
typedef struct
{
    SysResult_t (*post)(void *, const MRGrant *, const MRSlot *);
    SysResult_t (*withdraw)(void *, const MRGrant *, const MRSlot *);
    SysResult_t (*offer)(void *, VStorageOffer *);
    SysResult_t (*return_frame)(void *, const MRRequest *, const MRMedia *);
    SysResult_t (*receipt)(void *, MRReceipt *);
    SysResult_t (*consume)(void *, const MRReceipt *);
    SysResult_t (*retired)(void *, const MRReceipt *);
} VStorageFrameOps;

typedef struct
{
    uint32_t state, generation, highest_h, withdrawn;
    MRGrant grant;
    MRSlot slot;
    VStorageOffer current;
    MRReceipt terminal;
} VStorageFrame;

// 推进 Storage 的帧写入和真实归还流程。
SysResult_t VStorageFrame_Process(VStorageFrame *, const VStorageFrameOps *, void *,
                                  const MRGrant *, int accepting, int readers_quiet);
#endif
