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
/* No module-state cache here: snapshot is supplied by module owners at planning.
 * Indexed like registry.modules; stable states only. Plan is not applied config. */
SysResult_t EM_BuildPlan(const V2Registry *, const V2CompositionCandidate *,
                         const uint32_t *snapshot, uint16_t stop_mask, uint32_t kind,
                         uint32_t cleanup, EMPlan *);
/* Message adapters: submit OK=durably accepted, BUSY/NO_RESOURCE=not accepted.
 * read only returns frozen matched results; consume acknowledges delivery only.
 * Caller must serialize one issuer per module/session. Ports never release frames.
 * All callbacks bounded, nonblocking, no reentry. No cross-task raw LC access. */
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
SysResult_t EM_Init(EMContext *, uint32_t session, const EMEndpoint *, uint32_t count,
                    int (*owner)(void *), void *);
SysResult_t EM_Load(EMContext *, const EMPlan *, uint32_t timeout);
SysResult_t EM_Process(EMContext *, uint32_t now);
SysResult_t EM_Status(EMContext *, uint32_t *state, SysResult_t *first, SysResult_t *recovery);
#endif
