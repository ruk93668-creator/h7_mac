// 校验模块状态转换并保留可靠异步完成。

#ifndef V2_MANAGED_LIFECYCLE_H
#define V2_MANAGED_LIFECYCLE_H
#include "../Core/v2_types.h"
// 仅模块所有者调用。busy 只拒绝重入，不同步任务或中断。
#define LC_UNCONFIGURED 1U
#define LC_INACTIVE 2U
#define LC_ACTIVE 3U
#define LC_ERROR 4U
#define LC_CONFIGURE 1U
#define LC_ACTIVATE 2U
#define LC_DEACTIVATE 3U
#define LC_CLEANUP 4U
#define LC_ERROR_PROCESS 5U
#define LC_TRANSITION_BASE 16U
// 执行身份独立于 G/R/H 和 Legacy 消息。同一模块和会话只允许一个发号者，跨计划递增且禁止回绕。
typedef struct
{
    uint32_t session, request, module, operation;
} V2ExecId;
typedef struct
{
    V2ExecId id;
    uint32_t expected, timeout;
} LCRequest;
typedef struct
{
    V2ExecId id;
    SysResult_t result;        // 首错在终结后保持不变。
    SysResult_t actual_result; // 模块操作的实际结果，与超时记录独立。
    uint32_t fact;             // 已证明的模块稳定状态，资源所有权由 Resource 管理。
} LCCompletion;
// begin 返回 OK 只表示受理，其余结果要求操作未提交且无副作用。真实完成在回调外通过 LC_Complete 提交。
// prove 只读核验所有者的完成事实。BUSY 或 IO 保留待核验状态，不得释放资源或重新开放读取者。
struct V2LifecycleOps
{
    int (*is_owner)(void *);
    SysResult_t (*can_begin)(
        void *, const LCRequest *); // 只读前置检查，包含旧资源退休。
    SysResult_t (*begin)(void *, const LCRequest *);
    SysResult_t (*prove)(void *, const LCRequest *, uint32_t fact);
};
typedef struct
{
    uint32_t magic, busy, module, session, state, sealed;
    uint32_t pending, dispatched, decided, terminal, started;
    LCRequest request;
    LCCompletion completion;
    SysResult_t first_error, guard_error;
    const struct V2LifecycleOps *ops;
    void *port;
} LCContext;
// 比较完整事务身份，避免旧消息匹配当前操作。
int LC_IdEqual(const V2ExecId *, const V2ExecId *);
// 判断状态是否属于可受理新操作的稳定阶段。
int LC_Stable(uint32_t);
// 计算指定过渡成功后的目标状态。
uint32_t LC_Target(uint32_t);
// 绑定模块所有者和操作回调，初始化生命周期上下文。
SysResult_t LC_Init(LCContext *, uint32_t module, uint32_t session, const struct V2LifecycleOps *,
                    void *);
// 校验并登记过渡请求，受理不代表操作完成。
SysResult_t LC_Request(LCContext *, const LCRequest *, uint32_t now);
// 在所有者上下文推进正在进行的生命周期过渡。
SysResult_t LC_Process(LCContext *, uint32_t now);
// 接收匹配的实际操作完成结果。
SysResult_t LC_Complete(LCContext *, const V2ExecId *, SysResult_t actual, uint32_t fact);
// 读取已固定的完成消息。
SysResult_t LC_Read(LCContext *, const V2ExecId *, LCCompletion *);
// 确认匹配完成已消费并允许相应记录退休。
SysResult_t LC_Confirm(LCContext *, const LCCompletion *);
// 关闭新操作入口并保留既有事务状态。
SysResult_t LC_Seal(LCContext *); // 封闭接入，不清理资源或当前请求。
SysResult_t LC_Status(LCContext *, uint32_t *state, SysResult_t *first_error);
#endif
