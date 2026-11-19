// 按依赖计划协调模块操作、异步完成和失败补偿。

#ifndef V2_EXECUTION_MANAGER_H
#define V2_EXECUTION_MANAGER_H
#include "../Configuration/v2_composition.h"
#include "../Lifecycle/managed_lifecycle.h"
#define EM_STEP_MAX (2U * V2_MODULE_MAX)
#define EM_START 1U
#define EM_STOP 2U
#define EM_IDLE 0U
#define EM_RUNNING 1U
#define EM_COMPENSATING 2U
#define EM_DONE 3U
#define EM_BLOCKED 4U
typedef struct
{
    uint32_t module, operation, expected;
} EMStep;
typedef struct
{
    uint32_t kind, count;
    EMStep steps[EM_STEP_MAX];
} EMPlan;
// 规划时使用模块所有者提供的稳定快照。下标对应 registry.modules，计划本身不改变已应用配置。
SysResult_t EM_BuildPlan(const V2Registry *, const V2CompositionCandidate *,
                         const uint32_t *snapshot, uint16_t stop_mask, uint32_t kind,
                         uint32_t cleanup, EMPlan *);
// submit 返回 OK 表示可靠受理，BUSY 或 NO_RESOURCE 表示未提交。read 只读匹配结果，consume 只确认交付。
// 每个模块和会话由单一发号者串行推进。端口回调有界、非阻塞且禁止重入，不直接访问其他任务上下文或归还帧。
typedef struct
{
    uint32_t module;
    void *port;
    SysResult_t (*submit)(void *, const LCRequest *);
    SysResult_t (*read)(void *, const V2ExecId *, LCCompletion *);
    SysResult_t (*consume)(void *, const LCCompletion *);
} EMEndpoint;
typedef struct
{
    uint32_t magic, busy, session, issued, state, stage, index, undo_count;
    uint32_t timeout, started, blocked_unknown;
    SysResult_t first_error, recovery_error;
    EMPlan plan;
    EMStep undo[EM_STEP_MAX];
    LCRequest current;
    LCCompletion completion;
    EMEndpoint endpoints[V2_MODULE_MAX];
    uint32_t endpoint_count;
    int (*is_owner)(void *);
    void *owner;
} EMContext;
// 绑定执行所有者及模块端口，初始化计划执行状态。
SysResult_t EM_Init(EMContext *, uint32_t session, const EMEndpoint *, uint32_t count,
                    int (*owner)(void *), void *);
// 装载已校验计划，保留执行事务身份。
SysResult_t EM_Load(EMContext *, const EMPlan *, uint32_t timeout);
// 推进声明式计划并基于实际完成结果安排补偿。
SysResult_t EM_Process(EMContext *, uint32_t now);
// 读取计划进度、首错及补偿状态。
SysResult_t EM_Status(EMContext *, uint32_t *state, SysResult_t *first, SysResult_t *recovery);
#endif
