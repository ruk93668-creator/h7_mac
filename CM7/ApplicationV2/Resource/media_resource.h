#ifndef V2_MEDIA_RESOURCE_H
#define V2_MEDIA_RESOURCE_H
#include "media_source.h"
#define MR_HANDOFF_MAX 7U
#define MR_RECEIPT_MAX 7U
#define MR_GRANT_MAX 3U
#define MR_EMPTY 0U
#define MR_LOCAL 1U
#define MR_COMMITTED 2U
#define MR_ACCEPTED 3U
#define MR_BOUND 4U
#define MR_TERMINAL 5U
#define MR_BLOCKED 6U
#define MR_RETURN_PENDING 7U
#define MR_RELEASED 8U
typedef struct { MRGrant g; MRHandoff h; MRBridge b; MRSlot s; } MRRequest;
typedef struct { MRGrant g; MRHandoff h; MRMedia m; uint32_t serial; SysResult_t result; } MRReceipt;
typedef struct { MRRequest request; MRMedia media; MRView view; uint32_t state; SysResult_t last_release; } MRRecord;
typedef struct { MRReceipt value; uint32_t occupied, enqueued, consumed; } MRReceiptRecord;
/* Admission association only, not qualification or physical reference counts. */
typedef struct { MRGrant g; MRRelease closing; uint32_t occupied, closed; } MRAdmission;
typedef struct {
    uint32_t magic, busy, issued, receipt_issued;
    MRSource source; MRSourceCaps caps;
    MRAdmission admission[MR_GRANT_MAX];
    MRRecord records[MR_HANDOFF_MAX];
    MRReceiptRecord receipts[MR_RECEIPT_MAX];
} MRContext;
typedef SysResult_t (*MRSendReceipt)(void *, const MRReceipt *);
/* Zero-init once; caller-owned disjoint storage; immutable ports lifetime.
 * Camera owner only, nonblocking callbacks; no thread mutex or reset API.
 * No returned view is valid after its actual backend release.
 */
SysResult_t MR_Init(MRContext *,const MRSource *);
SysResult_t MR_AssociateGrant(MRContext *,const MRGrant *);
SysResult_t MR_Stage(MRContext *,const MRGrant *,const MRBridge *,const MRSlot *,MRRequest *);
SysResult_t MR_Commit(MRContext *,const MRRequest *);
SysResult_t MR_CancelLocal(MRContext *,const MRRequest *);
SysResult_t MR_Admit(MRContext *,const MRRequest *);
SysResult_t MR_Close(MRContext *,const MRRelease *);
SysResult_t MR_Obtain(MRContext *,const MRRequest *,MRMedia *,MRView *);
SysResult_t MR_Return(MRContext *,const MRRequest *,const MRMedia *);
SysResult_t MR_ReadReceipt(MRContext *,const MRRequest *,MRReceipt *);
SysResult_t MR_FlushReceipt(MRContext *,const MRReceipt *,MRSendReceipt,void *);
SysResult_t MR_ConfirmConsumed(MRContext *,const MRReceipt *);
SysResult_t MR_Retire(MRContext *,const MRReceipt *);
SysResult_t MR_VerifyExit(MRContext *,const MRRelease *);
#endif
