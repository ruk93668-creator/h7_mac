// 关联授权、交接和真实媒体持有，协调归还、收据和退出核验。

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
typedef struct
{
    MRGrant g;
    MRHandoff h;
    MRBridge b;
    MRSlot s;
} MRRequest;
typedef struct
{
    MRGrant g;
    MRHandoff h;
    MRMedia m;
    uint32_t serial;
    SysResult_t result;         /* frozen operation error, not a legacy wire ACK result */
    MRBridge bridge;            /* actual B, bound once after M; zero when no bridge loan */
    uint32_t physical_returned; /* historical fact; only trusted backend proof sets it */
} MRReceipt;
typedef struct
{
    MRRequest request;
    MRMedia media;
    MRView view;
    MRBridge bridge;
    uint32_t state;
    SysResult_t last_release;
} MRRecord;
typedef struct
{
    MRReceipt value;
    uint32_t occupied, enqueued, consumed;
} MRReceiptRecord;
/* Admission association only, not qualification or physical reference counts. */
typedef struct
{
    MRGrant g;
    MRRelease closing;
    uint32_t occupied, closed;
} MRAdmission;
typedef struct
{
    uint32_t magic, busy, issued, receipt_issued;
    MRSource source;
    MRSourceCaps caps;
    MRAdmission admission[MR_GRANT_MAX];
    MRRecord records[MR_HANDOFF_MAX];
    MRReceiptRecord receipts[MR_RECEIPT_MAX];
} MRContext;
typedef SysResult_t (*MRSendReceipt)(void *, const MRReceipt *);
/* Zero-init once; caller-owned disjoint storage; immutable ports lifetime.
 * Camera owner only; exactly one context per physical source (composition/port invariant).
 * No alternate context or direct backend entry may bypass closure.
 * Nonblocking callbacks; no thread mutex or reset API.
 * No returned view is valid after its actual backend release.
 */
SysResult_t MR_Init(MRContext *, const MRSource *);
// 关联来自权威资格方的 Grant，建立准入上下文。
SysResult_t MR_AssociateGrant(MRContext *, const MRGrant *);
// 登记原交接身份和元数据，尚未提交队列时保持可本地取消。
SysResult_t MR_Stage(MRContext *, const MRGrant *, const MRBridge *, const MRSlot *, MRRequest *);
// 记录原请求已提交，后续按同一身份办理终结。
SysResult_t MR_Commit(MRContext *, const MRRequest *);
// 取消尚未提交的交接，不生成 Camera 终结收据。
SysResult_t MR_CancelLocal(MRContext *, const MRRequest *);
// 在 Camera 所有者上下文核验准入并预留终结所需空间。
SysResult_t MR_Admit(MRContext *, const MRRequest *);
// 关闭指定 Grant 的准入，保留已受理交接的归还路径。
SysResult_t MR_Close(MRContext *, const MRRelease *);
// 通过可信后端取得物理媒体持有并关联到交接。
SysResult_t MR_Obtain(MRContext *, const MRRequest *, MRMedia *, MRView *);
// 为已取得的媒体绑定 Bridge 身份。
SysResult_t MR_BindBridge(MRContext *, const MRRequest *, const MRBridge *);
// 按完整 Bridge 身份查找关联的交接信息。
SysResult_t MR_LookupBridge(MRContext *, const MRBridge *, MRRequest *, MRMedia *);
// 在读取者退场后调用后端真实归还，保留失败后的持有事实。
SysResult_t MR_Return(MRContext *, const MRRequest *, const MRMedia *);
// 按匹配身份读取终结收据。
SysResult_t MR_ReadReceipt(MRContext *, const MRRequest *, MRReceipt *);
// 运送冻结的终结收据，入队不证明消费。
SysResult_t MR_FlushReceipt(MRContext *, const MRReceipt *, MRSendReceipt, void *);
// 登记客户端实际消费了匹配的归还收据。
SysResult_t MR_ConfirmConsumed(MRContext *, const MRReceipt *);
// 在终结及消费门禁成立后退休交接身份。
SysResult_t MR_Retire(MRContext *, const MRReceipt *);
// 核验指定 Grant 已关闭准入、无未完成交接且无物理持有。
SysResult_t MR_VerifyExit(MRContext *, const MRRelease *);
#endif
