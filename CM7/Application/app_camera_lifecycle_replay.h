#ifndef CM7_APP_CAMERA_LIFECYCLE_REPLAY_H
#define CM7_APP_CAMERA_LIFECYCLE_REPLAY_H

#include "app_camera_lifecycle.h"

#define APP_LIFECYCLE_CANDIDATE_EMPTY 0U
#define APP_LIFECYCLE_CANDIDATE_HELD 1U
#define APP_LIFECYCLE_CANDIDATE_QUARANTINED 2U

//分类仅供所属任务调度，不作为终结业务回复发送
#define APP_LIFECYCLE_RX_CANDIDATE 1U
#define APP_LIFECYCLE_RX_DUPLICATE_RUNNING 2U
#define APP_LIFECYCLE_RX_REPLAY 3U
#define APP_LIFECYCLE_RX_STALE 4U
#define APP_LIFECYCLE_RX_DUPLICATE_CANDIDATE 5U
#define APP_LIFECYCLE_RX_CONFLICT 6U
#define APP_LIFECYCLE_RX_QUARANTINED 7U

//一个请求方向的发送状态，初始化后只能由其所属任务串行调用
//调用者不得直接改字段，也不能绕过本层清理pending或回退编号
//对象首次使用前必须清零，普通START/STOP不允许重新初始化
//allocated是预留上界，issued在Stage成功时推进，取消也不回退
typedef struct
{
    uint32_t source;
    uint32_t target;
    uint32_t consumer_id;
    uint32_t allocated_through;
    uint32_t issued_through;
    uint32_t initialized;
    uint32_t api_busy;
    AppLifecyclePending_t pending;
} AppLifecycleTxContext_t;

//一个请求方向的固定接收窗口，不保存资格或帧引用计数
//Replay原文用于当前请求去重，退休编号只拒绝执行而不保留历史正文
//final_reply_enqueued与当前重放待发状态分开，不能用队列成功推断资源完成
typedef struct
{
    uint32_t source;
    uint32_t target;
    uint32_t consumer_id;
    uint32_t highest_retired_request_id;
    uint32_t active_request_id;
    uint32_t final_reply_enqueued;
    uint32_t candidate_state;
    uint32_t protocol_fault;
    uint32_t initialized;
    uint32_t api_busy;
    AppLifecycleReplay_t replay;
    AppLifecyclePending_t reply_pending;
    AppLifecycleMessage_t candidate;
} AppLifecycleRxContext_t;

//诊断只复制协议进展，不证明业务资源已经完成
typedef struct
{
    uint32_t highest_retired_request_id;
    uint32_t active_request_id;
    uint32_t occupied;
    uint32_t completed;
    uint32_t final_reply_enqueued;
    uint32_t candidate_state;
    uint32_t protocol_fault;
} AppLifecycleReplayStatus_t;

#define APP_REPLAY_ASSERT(name, expression) typedef char name[(expression) ? 1 : -1]
APP_REPLAY_ASSERT(AppReplay_TxSize, sizeof(AppLifecycleTxContext_t) == 144U);
APP_REPLAY_ASSERT(AppReplay_TxPending, offsetof(AppLifecycleTxContext_t, pending) == 28U);
APP_REPLAY_ASSERT(AppReplay_RxSize, sizeof(AppLifecycleRxContext_t) == 332U);
APP_REPLAY_ASSERT(AppReplay_RxReplay, offsetof(AppLifecycleRxContext_t, replay) == 40U);
APP_REPLAY_ASSERT(AppReplay_RxReply, offsetof(AppLifecycleRxContext_t, reply_pending) == 160U);
APP_REPLAY_ASSERT(AppReplay_RxCandidate, offsetof(AppLifecycleRxContext_t, candidate) == 276U);
APP_REPLAY_ASSERT(AppReplay_StatusSize, sizeof(AppLifecycleReplayStatus_t) == 28U);
#undef APP_REPLAY_ASSERT

//首次绑定固定发送方向，拒绝运行期重置编号
SysResult_t App_LifecycleTxInit(AppLifecycleTxContext_t *tx, uint32_t source,
    uint32_t target, uint32_t consumer);
//仅在没有待决请求时预留编号，失败不改变输出和分配上界
SysResult_t App_LifecycleTxReserveIds(AppLifecycleTxContext_t *tx, uint32_t count, uint32_t *first);
//绑定一个新请求，只有Stage成功才推进issued_through
SysResult_t App_LifecycleTxStage(AppLifecycleTxContext_t *tx, const AppLifecycleMessage_t *request);
//重试READY原文，回调必须只报告真实入队结果且不能重入本对象
SysResult_t App_LifecycleTxFlush(AppLifecycleTxContext_t *tx, AppLifecycleSendFn send, void *context);
//只接受完整匹配的回复，超时没有清理接口
SysResult_t App_LifecycleTxAcceptReply(AppLifecycleTxContext_t *tx, const AppLifecycleMessage_t *reply);
//读取终结回复，不表示调用者已经消费结果
SysResult_t App_LifecycleTxGetCompleted(const AppLifecycleTxContext_t *tx, AppLifecycleMessage_t *reply);
//所属模块处理结果后按完整原请求确认，不直接证明业务资源完成
SysResult_t App_LifecycleTxConfirmConsumed(AppLifecycleTxContext_t *tx, const AppLifecycleMessage_t *request);
//只取消匹配的READY请求，已绑定的编号永久作废
SysResult_t App_LifecycleTxCancelReady(AppLifecycleTxContext_t *tx, const AppLifecycleMessage_t *request);

//首次绑定固定接收方向，四个方向的窗口各自独立
SysResult_t App_LifecycleRxInit(AppLifecycleRxContext_t *rx, uint32_t source,
    uint32_t target, uint32_t consumer);
//分类并完整保留候选，不执行任何业务，分类输出不得与窗口重叠
SysResult_t App_LifecycleRxReceiveRequest(AppLifecycleRxContext_t *rx,
    const AppLifecycleMessage_t *request, uint32_t *disposition);
//先构造临时Replay再提交接替，成功时只向调用者交付一次新请求
SysResult_t App_LifecycleRxPromote(AppLifecycleRxContext_t *rx, AppLifecycleMessage_t *request);
//固定当前事务结果并准备回复，失败结果不能被后来成功覆盖
SysResult_t App_LifecycleRxComplete(AppLifecycleRxContext_t *rx, const AppLifecycleMessage_t *reply);
//可靠提交终结回复或其重放，不自动重复发送已经入队的回复
SysResult_t App_LifecycleRxFlushReply(AppLifecycleRxContext_t *rx, AppLifecycleSendFn send, void *context);
//读取完整协议进展，不能根据此诊断释放业务资源
SysResult_t App_LifecycleRxGetStatus(const AppLifecycleRxContext_t *rx, AppLifecycleReplayStatus_t *status);

#endif
