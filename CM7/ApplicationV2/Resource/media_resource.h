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
    MRGrant g;   // G 表示消费者这一代的授权。
    MRHandoff h; // H 表示本次交接，独立于授权和释放事务。
    MRBridge b;  // B 表示 USB Bridge 交接身份。
    MRSlot s;    // S 表示 Storage 槽位身份。
} MRRequest;
typedef struct
{
    MRGrant g;   // 关联原授权。
    MRHandoff h; // 关联原交接。
    MRMedia m;   // 后端实际媒体持有凭据，不能由帧地址推断。
    uint32_t serial;
    SysResult_t result;         // 冻结的操作结果，独立于旧协议 ACK 返回码。
    MRBridge bridge;            // 取得 M 后只绑定一次 B，没有 Bridge 持有时为零。
    uint32_t physical_returned; // 实际归还事实，仅可信后端证据可设置。
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
// 只记录准入关联，资格和物理引用分别由各自权威维护。
typedef struct
{
    MRGrant g;         // 此条准入记录绑定的授权。
    MRRelease closing; // 独立的 RELEASE 事务，用于核验本次关闭。
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
// 由调用方清零一次并保持端口有效。仅 Camera 所有者操作，每个物理源只用一个上下文。
// 回调必须非阻塞，不得绕过准入直接调用后端。不提供线程互斥或重置接口。
// 实际归还后，原媒体视图立即失效。
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
