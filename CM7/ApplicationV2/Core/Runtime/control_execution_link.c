// 在控制服务和执行任务之间传送请求、回复与消费确认。

#include "control_execution_link.h"
#include <string.h>

#define CLIENT_MAGIC 0x434c4337U
#define SERVER_MAGIC 0x434c5337U
#define LINK_RECEIVED 1U
#define LINK_RUNNING 2U
#define LINK_RESULT 3U
#define LINK_SENT 4U

// 校验异步执行请求的身份字段。
static int valid(const CSExecLinkOps *link_ops)
{
    return link_ops && link_ops->control_owner && link_ops->execution_owner && link_ops->send_work && link_ops->receive_work &&
           link_ops->send_result && link_ops->receive_result && link_ops->send_ack && link_ops->receive_ack;
}

// 初始化对应执行端点并绑定其所有者和端口。
SysResult_t CSExecClient_Init(CSExecClient *client, const CSExecLinkOps *link_ops, void *port_context)
{
    if (!client || client->magic || !valid(link_ops))
        return SYS_ERR_INVALID_ARG;
    if (!link_ops->control_owner(port_context))
        return SYS_ERR_INVALID_STATE;
    memset(client, 0, sizeof(*client));
    client->magic = CLIENT_MAGIC;
    client->ops = link_ops;
    client->port = port_context;
    return SYS_OK;
}

// 校验所有者调用并设置重入保护，调用方仍负责跨任务串行化。
static SysResult_t enter(CSExecClient *client)
{
    if (!client || client->magic != CLIENT_MAGIC || !client->ops->control_owner(client->port))
        return SYS_ERR_INVALID_STATE;
    if (client->busy)
        return SYS_ERR_BUSY;
    client->busy = 1U;
    return SYS_OK;
}

// 退出当前调用的重入保护。
static SysResult_t leave(CSExecClient *client, SysResult_t result)
{
    client->busy = 0U;
    return result;
}

// 提交带完整身份的异步执行请求。
SysResult_t CSExecClient_Submit(CSExecClient *client, const CSExecution *work)
{
    SysResult_t result = enter(client);
    if (result)
        return result;
    if (!work || !work->id.ticket || work->id.ticket <= client->id.ticket)
        return leave(client, SYS_ERR_PROTOCOL);
    if (client->active)
        return leave(client, SYS_ERR_BUSY);
    //工作消息成功入队后才登记提交。
    result = client->ops->send_work(client->port, work);
    if (!result)
    {
        client->id = work->id;
        client->active = 1U;
        client->cached = 0U;
    }
    return leave(client, result);
}

// 按指定执行身份读取已冻结结果。
SysResult_t CSExecClient_Read(CSExecClient *client, const CSIdentity *id, CSExecResult *out)
{
    CSExecResult execution_result;
    SysResult_t result = enter(client);
    if (result)
        return result;
    if (!out || V2_Overlap(client, sizeof(*client), out, sizeof(*out)) || !CS_IdEqual(id, &client->id))
        return leave(client, SYS_ERR_PROTOCOL);
    if (!client->cached)
    {
        //收到匹配结果之前不交付业务完成。
        result = client->ops->receive_result(client->port, &execution_result);
        if (result)
            return leave(client, result);
        if (!CS_IdEqual(id, &execution_result.id))
            return leave(client, SYS_ERR_NOT_FOUND);
        if ((execution_result.state != EM_DONE && execution_result.state != EM_BLOCKED && execution_result.state != CS_EXEC_REFUSED) ||
            execution_result.result > SYS_OK || execution_result.recovery > SYS_OK ||
            ((execution_result.state == EM_BLOCKED || execution_result.state == CS_EXEC_REFUSED) && !execution_result.result))
            return leave(client, SYS_ERR_PROTOCOL);
        client->result = execution_result;
        client->cached = 1U;
    }
    *out = client->result;
    return leave(client, SYS_OK);
}

// 确认执行结果已消费，允许相应运输记录退休。
SysResult_t CSExecClient_Consume(CSExecClient *client, const CSExecResult *execution_result)
{
    SysResult_t result = enter(client);
    if (result)
        return result;
    if (!execution_result || !client->cached || memcmp(execution_result, &client->result, sizeof(*execution_result)))
        return leave(client, SYS_ERR_PROTOCOL);
    if (!client->active)
        return leave(client, SYS_OK);
    //消费确认背压时保留完整结果，用同一身份重试。
    result = client->ops->send_ack(client->port, execution_result);
    if (!result)
        client->active = 0U;
    return leave(client, result);
}

// 初始化对应执行端点并绑定其所有者和端口。
SysResult_t CSExecServer_Init(CSExecServer *server, CSExecutionEndpoint *endpoint, const CSExecLinkOps *link_ops,
                              void *port_context)
{
    if (!server || server->magic || !endpoint || !valid(link_ops))
        return SYS_ERR_INVALID_ARG;
    if (!link_ops->execution_owner(port_context))
        return SYS_ERR_INVALID_STATE;
    memset(server, 0, sizeof(*server));
    server->magic = SERVER_MAGIC;
    server->endpoint = endpoint;
    server->ops = link_ops;
    server->port = port_context;
    return SYS_OK;
}

// 推进执行端对已受理事务的回复和确认处理。
static SysResult_t progress(CSExecServer *server, uint32_t now)
{
    SysResult_t result;
    CSExecResult ack;

    if (server->phase == LINK_SENT)
    {
        //收到确认后核对原结果，入队本身不能释放接收窗口。
        result = server->ops->receive_ack(server->port, &ack);
        if (result)
            return result;
        if (memcmp(&ack, &server->result, sizeof(ack)))
            return SYS_ERR_NOT_FOUND;
        if (server->result.state != CS_EXEC_REFUSED)
        {
            result = CSExec_Consume(server->endpoint, &ack);
            if (result)
                return result;
        }
        server->phase = 0U;
    }
    if (!server->phase)
    {
        //旧窗口空闲后才接收下一项。
        result = server->ops->receive_work(server->port, &server->work);
        if (result)
            return result;
        if (server->work.id.boot != server->endpoint->boot || server->work.id.ticket <= server->highest)
            return SYS_ERR_NOT_FOUND;
        server->highest = server->work.id.ticket;
        server->phase = LINK_RECEIVED;
    }
    if (server->phase == LINK_RECEIVED)
    {
        //首次装载计划，背压时保留原工作。
        result = CSExec_Submit(server->endpoint, &server->work);
        if (result == SYS_ERR_BUSY || result == SYS_ERR_NO_RESOURCE)
            return result;
        if (result)
        {
            server->result = (CSExecResult){server->work.id, CS_EXEC_REFUSED, result, SYS_OK};
            server->phase = LINK_RESULT;
        }
        else
            server->phase = LINK_RUNNING;
        return SYS_OK;
    }
    if (server->phase == LINK_RUNNING)
    {
        //推进已经受理的执行，取回真实完成。
        result = CSExec_Process(server->endpoint, now);
        if (!result)
            result = CSExec_Read(server->endpoint, &server->work.id, &server->result);
        if (result)
            return result;
        server->phase = LINK_RESULT;
    }
    //Reply满时只重试冻结的结果；消费确认前保留接收端状态。
    //只重试冻结结果，不能再次装载计划。
    result = server->ops->send_result(server->port, &server->result);
    if (!result)
        server->phase = LINK_SENT;
    return result;
}

// 在执行所有者上下文推进请求及结果运输。
SysResult_t CSExecServer_Process(CSExecServer *server, uint32_t now)
{
    SysResult_t result;
    if (!server || server->magic != SERVER_MAGIC || !server->ops->execution_owner(server->port))
        return SYS_ERR_INVALID_STATE;
    if (server->busy)
        return SYS_ERR_BUSY;
    server->busy = 1U;
    result = progress(server, now);
    server->busy = 0U;
    return result;
}
