#ifndef V2_CONTROL_SERVICE_H
#define V2_CONTROL_SERVICE_H

#include "../../Supervisor/execution_manager.h"

#define CS_VERSION 1U
#define CS_SESSION_MAX 8U
#define CS_RECORD_MAX 4U
#define CS_IPC 1U
#define CS_USB 2U
#define CS_BLE 3U
#define CS_WIFI 4U
#define CS_ETHERNET 5U

#define CS_VALIDATE 1U
#define CS_START 2U
#define CS_STOP 3U

#define CS_ACCEPTED 1U
#define CS_PLANNED 2U
#define CS_EXECUTING 3U
#define CS_FINAL 4U

#define CS_EFFECT_NONE 0U
#define CS_EFFECT_CANDIDATE 1U
#define CS_EFFECT_EXECUTION 2U
#define CS_EFFECT_CANCELLED 3U
#define CS_EXEC_REFUSED 5U /* 远端接收后拒绝，尚未装载EM。 */

/* Adapter根据已认证连接生成。不得从客户端命令正文复制权限或身份。 */
typedef struct
{
    uint32_t transport, principal, client;
} CSPeer;
typedef struct
{
    uint32_t boot, session, sequence, ticket;
} CSIdentity;
typedef struct
{
    uint32_t version, boot, session, sequence, operation, profile, revision, stop_mask, cleanup;
    uint32_t timeout;
} CSRequest;
typedef struct
{
    CSIdentity id;
    uint32_t phase, effect, profile, revision, execution_state;
    SysResult_t first_error, actual_result, recovery_error;
} CSReply;
typedef struct
{
    uint32_t id;
    V2Desired desired; /* 可信、不可变目录，客户端只能选择已登记profile。 */
} CSProfile;
typedef struct
{
    uint32_t revision, prepared_profile;
    uint32_t states[V2_MODULE_MAX];
} CSSnapshot;
typedef struct
{
    CSIdentity id;
    uint32_t profile, revision, step_timeout;
    CSSnapshot expected;
    EMPlan plan;
} CSExecution;
typedef struct
{
    CSIdentity id;
    uint32_t state;
    SysResult_t result, recovery;
} CSExecResult;

typedef struct
{
    int (*owner)(void *);
    /* operation==0验证连接；module==0验证整个请求；
     * module!=0校验实际计划步骤及其可能的补偿权限。 */
    SysResult_t (*authorize)(void *, const CSPeer *, uint32_t operation, uint32_t profile,
                             uint32_t module, uint32_t lifecycle_op);
    SysResult_t (*snapshot)(void *, CSSnapshot *);      /* 同一revision下的一致值 */
    SysResult_t (*submit)(void *, const CSExecution *); /* OK表示承诺受理；其余未提交 */
    SysResult_t (*read)(void *, const CSIdentity *, CSExecResult *);
    SysResult_t (*consume)(void *, const CSExecResult *);
} CSPortOps;

typedef struct
{
    uint32_t id, issued;
    CSPeer peer;
} CSSession;
typedef struct
{
    CSRequest request;
    CSReply reply;
    uint32_t delivered;
} CSRecord;

typedef struct
{
    const V2Registry *registry;
    const GraphDesc *graph;
    const V2EntityBinding *bindings;
    const CSProfile *profiles;
    V2ResourceValidator validate;
    uint16_t binding_count, profile_count;
} CSCatalog;

typedef struct
{
    uint32_t magic, busy, boot, next_session, next_ticket, active, started, blocked, exec_seen;
    const CSCatalog *catalog;
    const CSPortOps *ops;
    void *port;
    CSSession sessions[CS_SESSION_MAX];
    CSRecord records[CS_RECORD_MAX];
    CSExecution execution;
    CSExecResult exec_result;
    V2CompositionScratch scratch;
    V2CompositionCandidate candidate;
} CSService;

/* 服务单owner，跨任务通过复制消息与绑定的传输端口。boot来自可信启动会话。 */
int CS_IdEqual(const CSIdentity *, const CSIdentity *);
SysResult_t CS_Init(CSService *, uint32_t boot, const CSCatalog *, const CSPortOps *, void *);
SysResult_t CS_Open(CSService *, const CSPeer *, uint32_t *session);
SysResult_t CS_Close(CSService *, const CSPeer *, uint32_t boot, uint32_t session);
SysResult_t CS_Submit(CSService *, const CSPeer *, const CSRequest *, uint32_t now, CSReply *);
SysResult_t CS_Query(CSService *, const CSPeer *, const CSIdentity *, CSReply *);
SysResult_t CS_Confirm(CSService *, const CSPeer *, const CSReply *);
SysResult_t CS_Cancel(CSService *, const CSPeer *, const CSIdentity *);
SysResult_t CS_Process(CSService *, uint32_t now);
#endif
