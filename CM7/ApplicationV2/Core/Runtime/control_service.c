// 维护统一控制请求身份、权限、决策和可靠回复。

#include "control_service.h"
#include <string.h>

#define MAGIC 0x43535637U

// 比较完整事务身份，避免旧消息匹配当前操作。
int CS_IdEqual(const CSIdentity *left_identity, const CSIdentity *right_identity)
{
    return left_identity && right_identity && left_identity->boot == right_identity->boot && left_identity->session == right_identity->session && left_identity->sequence == right_identity->sequence &&
           left_identity->ticket == right_identity->ticket;
}

// 核对对端绑定身份及可信传输来源。
static int peer_equal(const CSPeer *left_peer, const CSPeer *right_peer)
{
    return left_peer->transport == right_peer->transport && left_peer->principal == right_peer->principal && left_peer->client == right_peer->client;
}

// 核对对端绑定身份及可信传输来源。
static int trusted_peer(const CSPeer *peer)
{
    return peer && peer->transport >= CS_IPC && peer->transport <= CS_ETHERNET && peer->principal && peer->client;
}

// 校验所有者调用并设置重入保护，调用方仍负责跨任务串行化。
static SysResult_t enter(CSService *service)
{
    if (!service || service->magic != MAGIC || !service->ops->owner(service->port))
        return SYS_ERR_INVALID_STATE;
    if (service->busy)
        return SYS_ERR_BUSY;
    service->busy = 1U;
    return SYS_OK;
}

// 退出当前调用的重入保护。
static SysResult_t leave(CSService *service, SysResult_t result)
{
    service->busy = 0U;
    return result;
}

// 查找对端可以使用的权限配置或会话。
static const CSProfile *profile(const CSService *service, uint32_t id)
{
    uint16_t profile_index;
    for (profile_index = 0; profile_index < service->catalog->profile_count; ++profile_index)
        if (service->catalog->profiles[profile_index].id == id)
            return &service->catalog->profiles[profile_index];
    return NULL;
}

// 查找对端可以使用的权限配置或会话。
static CSSession *session(CSService *service, const CSPeer *peer, uint32_t boot, uint32_t id)
{
    uint32_t session_index;
    if (boot != service->boot || !id || !trusted_peer(peer))
        return NULL;
    for (session_index = 0; session_index < CS_SESSION_MAX; ++session_index)
        if (service->sessions[session_index].id == id && peer_equal(peer, &service->sessions[session_index].peer))
            return &service->sessions[session_index];
    return NULL;
}

// 按完整身份定位已有记录。
static CSRecord *record(CSService *service, uint32_t id, uint32_t seq)
{
    uint32_t record_index;
    for (record_index = 0; record_index < CS_RECORD_MAX; ++record_index)
        if (service->records[record_index].reply.id.session == id && service->records[record_index].reply.id.sequence == seq)
            return &service->records[record_index];
    return NULL;
}

// 识别仍需等待端口或运输推进的结果。
static int waiting(SysResult_t result)
{
    return result == SYS_ERR_BUSY || result == SYS_ERR_NO_RESOURCE || result == SYS_ERR_NOT_FOUND;
}

// 检查响应输出端口是否可用。
static int output_ok(const CSService *service, const void *port_context, size_t bytes)
{
    return port_context && !V2_Overlap(service, sizeof(*service), port_context, bytes);
}

// 绑定控制策略、端口和所有者，初始化有界事务表。
SysResult_t CS_Init(CSService *service, uint32_t boot, const CSCatalog *catalog, const CSPortOps *port_ops, void *port_context)
{
    uint16_t profile_index, previous_index;
    if (!service || service->magic || !boot || !catalog || !catalog->registry || !catalog->graph || !catalog->bindings ||
        !catalog->binding_count || !catalog->profiles || !catalog->profile_count ||
        catalog->profile_count > GRAPH_PIPELINE_MAX || !catalog->validate || !port_ops || !port_ops->owner || !port_ops->authorize ||
        !port_ops->snapshot || !port_ops->submit || !port_ops->read || !port_ops->consume)
        return SYS_ERR_INVALID_ARG;
    if (!port_ops->owner(port_context))
        return SYS_ERR_INVALID_STATE;
    for (profile_index = 0; profile_index < catalog->profile_count; ++profile_index)
    {
        if (!catalog->profiles[profile_index].id)
            return SYS_ERR_INVALID_ARG;
        for (previous_index = 0; previous_index < profile_index; ++previous_index)
            if (catalog->profiles[profile_index].id == catalog->profiles[previous_index].id)
                return SYS_ERR_ALREADY_EXISTS;
    }
    memset(service, 0, sizeof(*service));
    service->boot = boot;
    service->catalog = catalog;
    service->ops = port_ops;
    service->port = port_context;
    service->magic = MAGIC;
    return SYS_OK;
}

// 为受信控制对端建立会话。
SysResult_t CS_Open(CSService *service, const CSPeer *peer, uint32_t *out)
{
    uint32_t session_index, free_index = CS_SESSION_MAX;
    SysResult_t result = enter(service);
    if (result)
        return result;
    if (!trusted_peer(peer) || !output_ok(service, out, sizeof(*out)))
        return leave(service, SYS_ERR_INVALID_ARG);
    //用可信传输身份授权会话，禁止客户端自行声明权限。
    result = service->ops->authorize(service->port, peer, 0U, 0U, 0U, 0U);
    if (result)
        return leave(service, result);
    for (session_index = 0; session_index < CS_SESSION_MAX; ++session_index)
    {
        if (service->sessions[session_index].id)
        {
            if (peer_equal(peer, &service->sessions[session_index].peer))
            {
                *out = service->sessions[session_index].id;
                return leave(service, SYS_OK);
            }
        }
        else if (free_index == CS_SESSION_MAX)
            free_index = session_index;
    }
    if (free_index == CS_SESSION_MAX || service->next_session == UINT32_MAX)
        return leave(service, SYS_ERR_NO_RESOURCE);
    service->sessions[free_index].peer = *peer;
    service->sessions[free_index].issued = 0U;
    service->sessions[free_index].id = ++service->next_session;
    *out = service->next_session;
    return leave(service, SYS_OK);
}

// 关闭匹配的控制会话入口。
SysResult_t CS_Close(CSService *service, const CSPeer *peer, uint32_t boot, uint32_t id)
{
    CSSession *session_entry;
    uint32_t record_index;
    SysResult_t result = enter(service);
    if (result)
        return result;
    session_entry = session(service, peer, boot, id);
    if (!session_entry)
        return leave(service, SYS_ERR_PROTOCOL);
    for (record_index = 0; record_index < CS_RECORD_MAX; ++record_index)
        if (service->records[record_index].reply.id.session == id && !service->records[record_index].delivered)
            return leave(service, SYS_ERR_BUSY);
    for (record_index = 0; record_index < CS_RECORD_MAX; ++record_index)
        if (service->records[record_index].reply.id.session == id)
            memset(&service->records[record_index], 0, sizeof(service->records[record_index]));
    memset(session_entry, 0, sizeof(*session_entry));
    return leave(service, SYS_OK);
}

// 检查控制请求身份及命令字段是否合规。
static int valid_request(const CSRequest *request)
{
    return request && request->version == CS_VERSION && request->sequence && request->profile && request->timeout &&
           request->timeout <= 0x7fffffffU && request->operation >= CS_VALIDATE && request->operation <= CS_STOP &&
           (request->operation == CS_VALIDATE || request->revision) &&
           (request->operation == CS_STOP ? request->stop_mask && request->stop_mask <= UINT16_MAX && request->cleanup <= 1U
                                    : !request->stop_mask && !request->cleanup);
}

// 验证权限及身份后受理控制命令。
SysResult_t CS_Submit(CSService *service, const CSPeer *peer, const CSRequest *request, uint32_t now, CSReply *out)
{
    CSSession *session_entry;
    CSRecord *active_record;
    uint32_t record_index, index = CS_RECORD_MAX, oldest = UINT32_MAX;
    SysResult_t result = enter(service);
    if (result)
        return result;
    if (!valid_request(request) || !output_ok(service, out, sizeof(*out)) ||
        V2_Overlap(request, sizeof(*request), out, sizeof(*out)))
        return leave(service, SYS_ERR_INVALID_ARG);
    session_entry = session(service, peer, request->boot, request->session);
    if (!session_entry)
        return leave(service, SYS_ERR_PROTOCOL);
    result = service->ops->authorize(service->port, peer, request->operation, request->profile, 0U, 0U);
    if (result)
        return leave(service, result);
    active_record = record(service, request->session, request->sequence);
    if (active_record)
    {
        if (memcmp(&active_record->request, request, sizeof(*request)))
            return leave(service, SYS_ERR_PROTOCOL);
        *out = active_record->reply;
        return leave(service, SYS_OK);
    }
    if (request->sequence <= session_entry->issued)
        return leave(service, SYS_ERR_NOT_FOUND); //已退休或跳号，不重新执行。
    if (!profile(service, request->profile))
        return leave(service, SYS_ERR_NOT_FOUND);
    if (service->active)
        return leave(service, SYS_ERR_BUSY);
    if (service->blocked && request->operation != CS_VALIDATE)
        return leave(service, SYS_ERR_INVALID_STATE);
    for (record_index = 0; record_index < CS_RECORD_MAX; ++record_index)
    {
        active_record = &service->records[record_index];
        if (!active_record->reply.id.ticket)
        {
            index = record_index;
            break;
        }
        if (active_record->delivered && active_record->reply.id.ticket < oldest)
        {
            index = record_index;
            oldest = active_record->reply.id.ticket;
        }
    }
    if (index == CS_RECORD_MAX || service->next_ticket == UINT32_MAX)
        return leave(service, SYS_ERR_NO_RESOURCE);
    active_record = &service->records[index];
    memset(active_record, 0, sizeof(*active_record));
    active_record->request = *request;
    active_record->reply.id = (CSIdentity){service->boot, request->session, request->sequence, ++service->next_ticket};
    //装入完整身份后登记已受理，尚未提交执行。
    active_record->reply.phase = CS_ACCEPTED;
    active_record->reply.profile = request->profile;
    active_record->reply.revision = request->revision;
    session_entry->issued = request->sequence;
    service->active = index + 1U;
    service->started = now;
    service->exec_seen = 0U;
    *out = active_record->reply;
    return leave(service, SYS_OK);
}

// 查找指定对端有权访问的控制请求记录。
static CSRecord *owned_record(CSService *service, const CSPeer *peer, const CSIdentity *id)
{
    CSRecord *active_record;
    if (!id || !session(service, peer, id->boot, id->session))
        return NULL;
    active_record = record(service, id->session, id->sequence);
    return active_record && CS_IdEqual(id, &active_record->reply.id) ? active_record : NULL;
}

// 按对端和请求身份查询进度或冻结结果。
SysResult_t CS_Query(CSService *service, const CSPeer *peer, const CSIdentity *id, CSReply *out)
{
    CSRecord *active_record;
    SysResult_t result = enter(service);
    if (result)
        return result;
    if (!output_ok(service, out, sizeof(*out)))
        return leave(service, SYS_ERR_INVALID_ARG);
    active_record = owned_record(service, peer, id);
    if (!active_record)
        return leave(service, SYS_ERR_NOT_FOUND);
    result = service->ops->authorize(service->port, peer, active_record->request.operation, active_record->request.profile, 0U, 0U);
    if (!result)
        *out = active_record->reply;
    return leave(service, result);
}

// 确认匹配的控制回复已被消费。
SysResult_t CS_Confirm(CSService *service, const CSPeer *peer, const CSReply *reply)
{
    CSRecord *active_record;
    SysResult_t result = enter(service);
    if (result)
        return result;
    active_record = owned_record(service, peer, reply ? &reply->id : NULL);
    if (!active_record || active_record->reply.phase != CS_FINAL || memcmp(reply, &active_record->reply, sizeof(*reply)))
        return leave(service, SYS_ERR_PROTOCOL);
    //消费只退休交付凭证，同序号重传仍读旧结果。
    active_record->delivered = 1U;
    return leave(service, SYS_OK);
}

// 记录首个错误，后续恢复结果不能覆盖它。
static void first(CSRecord *active_record, SysResult_t result)
{
    if (result && !active_record->reply.first_error)
        active_record->reply.first_error = result;
}

// 冻结当前操作结果，供下一阶段读取。
static void finish(CSService *service, CSRecord *active_record, SysResult_t result)
{
    first(active_record, result);
    active_record->reply.actual_result = result;
    //冻结终结结果和首错，等待运输层消费确认。
    active_record->reply.phase = CS_FINAL;
    service->active = 0U;
}

// 请求取消控制事务，区分尚未执行和已经提交的操作。
SysResult_t CS_Cancel(CSService *service, const CSPeer *peer, const CSIdentity *id)
{
    CSRecord *active_record;
    SysResult_t result = enter(service);
    if (result)
        return result;
    active_record = owned_record(service, peer, id);
    if (!active_record)
        return leave(service, SYS_ERR_NOT_FOUND);
    result = service->ops->authorize(service->port, peer, active_record->request.operation, active_record->request.profile, 0U, 0U);
    if (result)
        return leave(service, result);
    if (active_record->reply.phase == CS_FINAL)
        return leave(service, SYS_OK);
    //进入执行阶段后不能将取消当作撤回已提交操作。
    if (active_record->reply.phase == CS_EXECUTING)
        return leave(service, SYS_ERR_BUSY);
    active_record->reply.effect = CS_EFFECT_CANCELLED;
    finish(service, active_record, SYS_ERR_INVALID_STATE);
    return leave(service, SYS_OK);
}

// 将请求命令转换成经过校验的组合候选及执行计划。
static SysResult_t check_plan(CSService *service, const CSPeer *peer, const CSRecord *active_record)
{
    uint32_t step_index;
    for (step_index = 0; step_index < service->execution.plan.count; ++step_index)
    {
        const EMStep *step = &service->execution.plan.steps[step_index];
        SysResult_t result = service->ops->authorize(service->port, peer, active_record->request.operation, active_record->request.profile,
                                          step->module, step->operation);
        if (!result && active_record->request.operation == CS_START)
            result = service->ops->authorize(service->port, peer, CS_START, active_record->request.profile, step->module,
                                  step->operation == LC_CONFIGURE ? LC_CLEANUP : LC_DEACTIVATE);
        if (result)
            return result;
    }
    return SYS_OK;
}

// 将请求命令转换成经过校验的组合候选及执行计划。
static SysResult_t build(CSService *service, CSRecord *active_record)
{
    const CSProfile *profile_desc = profile(service, active_record->request.profile);
    const CSCatalog *catalog = service->catalog;
    V2Issue issue = {0};
    GraphIssue graph_issue = {0};
    SysResult_t result =
        V2Composition_Validate(catalog->registry, catalog->graph, catalog->bindings, catalog->binding_count, &profile_desc->desired,
                               catalog->validate, &service->scratch, &service->candidate, &issue, &graph_issue);
    if (result || active_record->request.operation == CS_VALIDATE)
        return result;
    memset(&service->execution, 0, sizeof(service->execution));
    //按当前模块快照生成计划，禁止直接调用硬件。
    result = service->ops->snapshot(service->port, &service->execution.expected);
    if (result)
        return result;
    if (service->execution.expected.revision != active_record->request.revision ||
        service->execution.expected.prepared_profile != profile_desc->id)
        return SYS_ERR_INVALID_STATE; //仅执行已经绑定的配置；重配置属于后续阶段。
    service->execution.id = active_record->reply.id;
    service->execution.profile = profile_desc->id;
    service->execution.revision = active_record->request.revision;
    service->execution.step_timeout = active_record->request.timeout;
    return EM_BuildPlan(catalog->registry, &service->candidate, service->execution.expected.states,
                        (uint16_t)active_record->request.stop_mask,
                        active_record->request.operation == CS_START ? EM_START : EM_STOP, active_record->request.cleanup,
                        &service->execution.plan);
}

// 推进控制事务的候选构建、异步执行和最终回复。
SysResult_t CS_Process(CSService *service, uint32_t now)
{
    CSRecord *active_record;
    CSSession *session_entry = NULL;
    CSExecResult execution_result;
    uint32_t session_index;
    SysResult_t result = enter(service);
    if (result)
        return result;
    if (!service->active)
        return leave(service, SYS_OK);
    active_record = &service->records[service->active - 1U];
    if (!service->exec_seen && (uint32_t)(now - service->started) >= active_record->request.timeout)
    {
        if (active_record->reply.phase != CS_EXECUTING)
        {
            finish(service, active_record, SYS_ERR_TIMEOUT);
            return leave(service, SYS_OK);
        }
        first(active_record, SYS_ERR_TIMEOUT); //继续等待实际完成，不清理活动执行。
    }
    if (active_record->reply.phase != CS_EXECUTING)
    {
        for (session_index = 0; session_index < CS_SESSION_MAX; ++session_index)
            if (service->sessions[session_index].id == active_record->reply.id.session)
                session_entry = &service->sessions[session_index];
        if (!session_entry)
            result = SYS_ERR_PROTOCOL;
        else
            result = service->ops->authorize(service->port, &session_entry->peer, active_record->request.operation, active_record->request.profile, 0U,
                                  0U);
        if (result)
        {
            finish(service, active_record, result);
            return leave(service, SYS_OK);
        }
    }
    if (active_record->reply.phase == CS_ACCEPTED)
    {
        //先通过 Composition 和依赖计划计算候选。
        result = build(service, active_record);
        if (result || active_record->request.operation == CS_VALIDATE)
        {
            if (!result)
                active_record->reply.effect = CS_EFFECT_CANDIDATE;
            finish(service, active_record, result);
        }
        else
            active_record->reply.phase = CS_PLANNED;
        return leave(service, SYS_OK);
    }
    if (active_record->reply.phase == CS_PLANNED)
    {
        //逐步骤确认执行及补偿权限。
        result = check_plan(service, &session_entry->peer, active_record);
        if (result)
            finish(service, active_record, result);
        else
        {
            //运输背压时保留原计划和请求编号。
            result = service->ops->submit(service->port, &service->execution);
            if (!result)
                active_record->reply.phase = CS_EXECUTING;
            else if (result != SYS_ERR_BUSY && result != SYS_ERR_NO_RESOURCE)
                finish(service, active_record, result);
        }
        return leave(service, SYS_OK);
    }
    if (!service->exec_seen)
    {
        //只读当前执行身份的冻结结果。
        result = service->ops->read(service->port, &active_record->reply.id, &execution_result);
        if (result)
        {
            if (!waiting(result))
                first(active_record, result);
            return leave(service, result);
        }
        if (!CS_IdEqual(&execution_result.id, &active_record->reply.id) ||
            (execution_result.state != EM_DONE && execution_result.state != EM_BLOCKED && execution_result.state != CS_EXEC_REFUSED) ||
            execution_result.result > 0 || execution_result.recovery > 0 ||
            ((execution_result.state == EM_BLOCKED || execution_result.state == CS_EXEC_REFUSED) && !execution_result.result))
            return leave(service, SYS_ERR_PROTOCOL); //丢弃错身份，保留当前事务。
        service->exec_result = execution_result;
        service->exec_seen = 1U;
        first(active_record, execution_result.result); //先冻结首错，随后才重试消费。
    }
    //先完成匹配的消费握手，再结束控制事务。
    result = service->ops->consume(service->port, &service->exec_result);
    if (result)
    {
        if (!waiting(result))
            first(active_record, result);
        return leave(service, result);
    }
    execution_result = service->exec_result;
    if (execution_result.state == EM_BLOCKED)
        service->blocked = 1U;
    if (execution_result.state != CS_EXEC_REFUSED)
        active_record->reply.effect = CS_EFFECT_EXECUTION;
    active_record->reply.execution_state = execution_result.state;
    active_record->reply.recovery_error = execution_result.recovery;
    finish(service, active_record, execution_result.result);
    return leave(service, SYS_OK);
}
