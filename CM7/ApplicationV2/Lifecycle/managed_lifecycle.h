#ifndef V2_MANAGED_LIFECYCLE_H
#define V2_MANAGED_LIFECYCLE_H
#include "../Core/v2_types.h"
/* Module owner only; no task/ISR synchronization provided by busy. */
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
/* Identity is independent from G/R/H and all legacy wire messages. One issuer
 * per module/session. request is monotonic across all plans, never wraps. */
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
    SysResult_t result;        /* first observed error, immutable at terminal */
    SysResult_t actual_result; /* module operation result, distinct from timeout */
    uint32_t fact;             /* proven stable module fact, not resource ownership */
} LCCompletion;
/* begin: OK means accepted ONLY; BUSY/NO_RESOURCE means not submitted with no
 * side effects. Other errors mean not submitted, also without side effects.
 * Actual completion arrives later through LC_Complete, outside callbacks.
 * prove is a pure owner-local completion guard: BUSY/IO retain pending facts;
 * it never releases resources and must not re-open new readers. */
struct V2LifecycleOps
{
    int (*is_owner)(void *);
    SysResult_t (*can_begin)(
        void *, const LCRequest *); /* pure precondition incl. old resource retirement */
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
int LC_IdEqual(const V2ExecId *, const V2ExecId *);
int LC_Stable(uint32_t);
uint32_t LC_Target(uint32_t);
SysResult_t LC_Init(LCContext *, uint32_t module, uint32_t session, const struct V2LifecycleOps *,
                    void *);
SysResult_t LC_Request(LCContext *, const LCRequest *, uint32_t now);
SysResult_t LC_Process(LCContext *, uint32_t now);
SysResult_t LC_Complete(LCContext *, const V2ExecId *, SysResult_t actual, uint32_t fact);
SysResult_t LC_Read(LCContext *, const V2ExecId *, LCCompletion *);
SysResult_t LC_Confirm(LCContext *, const LCCompletion *);
SysResult_t LC_Seal(LCContext *); /* never clears resources or current request */
SysResult_t LC_Status(LCContext *, uint32_t *state, SysResult_t *first_error);
#endif
