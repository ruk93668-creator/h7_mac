// 在执行端与 Storage 任务间运输请求及完成确认。

#include "storage_endpoint.h"
#include <string.h>
#define CLIENT_MAGIC 0x53564336U
#define ENDPOINT_MAGIC 0x53564536U
// 校验异步执行请求的身份字段。
static int valid(const VStorageTransport *transport)
{
    return transport && transport->ops && transport->ops->is_storage && transport->ops->is_supervisor && transport->ops->send_request &&
           transport->ops->receive_request && transport->ops->send_reply && transport->ops->receive_reply &&
           transport->ops->send_consumed && transport->ops->receive_consumed && transport->ops->notify;
}
// 校验所有者调用并设置重入保护，调用方仍负责跨任务串行化。
static SysResult_t client_enter(VStorageClient *client)
{
    if (!client || client->magic != CLIENT_MAGIC || !client->transport.ops->is_supervisor(client->transport.port))
        return SYS_ERR_INVALID_STATE;
    if (client->busy)
        return SYS_ERR_BUSY;
    client->busy = 1;
    return 0;
}
// 退出当前调用的重入保护。
static SysResult_t client_leave(VStorageClient *client, SysResult_t result)
{
    client->busy = 0;
    return result;
}
// Storage：初始化执行侧客户端并绑定运输端口。
SysResult_t VStorageClient_Init(VStorageClient *client, uint32_t module, uint32_t session,
                                const VStorageTransport *transport)
{
    if (!client || client->magic || !module || !session || !valid(transport))
        return SYS_ERR_INVALID_ARG;
    if (!transport->ops->is_supervisor(transport->port))
        return SYS_ERR_INVALID_STATE;
    memset(client, 0, sizeof(*client));
    client->module = module;
    client->session = session;
    client->transport = *transport;
    client->magic = CLIENT_MAGIC;
    return 0;
}
// 提交异步请求，运输受理与操作完成分别记录。
static SysResult_t submit(void *port_context, const LCRequest *request)
{
    VStorageClient *client = port_context;
    SysResult_t result = client_enter(client);
    if (result)
        return result;
    //先核对模块、会话和操作，避免把旧指令交给当前端口。
    if (!request || request->id.module != client->module || request->id.session != client->session || !request->id.request ||
        !LC_Target(request->id.operation) || !LC_Stable(request->expected) || !request->timeout ||
        request->timeout > 0x7FFFFFFFU)
        return client_leave(client, SYS_ERR_PROTOCOL);
    //同编号只允许重复完全相同的请求。
    if (request->id.request == client->request.id.request)
        return client_leave(client, !memcmp(request, &client->request, sizeof(*request)) ? 0 : SYS_ERR_PROTOCOL);
    if (request->id.request < client->request.id.request)
        return client_leave(client, SYS_ERR_PROTOCOL);
    //未消费的旧事务继续占用窗口。
    if (client->active)
        return client_leave(client, SYS_ERR_BUSY);
    //只有入队成功才登记提交，通知只用于唤醒。
    result = client->transport.ops->send_request(client->transport.port, request);
    if (!result)
    {
        client->request = *request;
        client->active = 1;
        client->has_reply = 0;
        client->transport.ops->notify(client->transport.port, VST_ACTOR_STORAGE);
    }
    return client_leave(client, result);
}
// 读取与当前待处理身份匹配的完成消息。
static SysResult_t read_reply(void *port_context, const V2ExecId *id, LCCompletion *out)
{
    VStorageClient *client = port_context;
    VStorageReply reply;
    SysResult_t result = client_enter(client);
    if (result)
        return result;
    if (!out || V2_Overlap(client, sizeof(*client), out, sizeof(*out)) || !LC_IdEqual(id, &client->request.id))
        return client_leave(client, SYS_ERR_PROTOCOL);
    //没有缓存时才从队列取回复，失败可继续使用原请求重试。
    if (!client->has_reply)
    {
        result = client->transport.ops->receive_reply(client->transport.port, &reply);
        if (result)
            return client_leave(client, result);
        //迟到或错身份的回复不得推进当前事务。
        if (!LC_IdEqual(&reply.completion.id, id))
            return client_leave(client, SYS_ERR_NOT_FOUND); // 取走迟到回复，避免进入当前事务。
        if (reply.delivery_error > 0 ||
            (!reply.delivery_error && (reply.completion.result > 0 || reply.completion.actual_result > 0 ||
                                   !LC_Stable(reply.completion.fact))))
            return client_leave(client, SYS_ERR_PROTOCOL);
        //保存匹配结果，消费确认成功之前可重读。
        client->reply = reply;
        client->has_reply = 1;
    }
    if (client->reply.delivery_error)
    {
        //Execution Manager不会消费拒绝执行业务的回复，由传输层确认。
        //未消费的旧事务继续占用窗口。
        if (client->active)
        {
            //确认队列满时保留活动事务，稍后重发同一确认。
            result = client->transport.ops->send_consumed(client->transport.port, &client->reply.completion);
            if (result)
                return client_leave(client, result == SYS_ERR_NO_RESOURCE ? SYS_ERR_BUSY : result);
            client->active = 0;
            client->transport.ops->notify(client->transport.port, VST_ACTOR_STORAGE);
        }
        return client_leave(client, client->reply.delivery_error);
    }
    *out = client->reply.completion;
    return client_leave(client, 0);
}
// 确认指定完成已被客户端实际消费。
static SysResult_t consumed(void *port_context, const LCCompletion *completion)
{
    VStorageClient *client = port_context;
    SysResult_t result = client_enter(client);
    if (result)
        return result;
    //消费确认必须对应先前读取的完整完成记录。
    if (!completion || !client->has_reply || client->reply.delivery_error ||
        memcmp(completion, &client->reply.completion, sizeof(*completion)))
        return client_leave(client, SYS_ERR_PROTOCOL);
    if (!client->active)
        return client_leave(client, 0);
    //确认队列满时保留活动事务，稍后重发同一确认。
    result = client->transport.ops->send_consumed(client->transport.port, completion);
    if (!result)
    {
        client->active = 0;
        client->transport.ops->notify(client->transport.port, VST_ACTOR_STORAGE);
    }
    return client_leave(client, result);
}
// Storage：返回供 Execution Manager 使用的模块端口。
SysResult_t VStorageClient_Endpoint(VStorageClient *client, EMEndpoint *out)
{
    SysResult_t result;
    if (!out || !client || V2_Overlap(client, sizeof(*client), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    result = client_enter(client);
    if (result)
        return result;
    out->module = client->module;
    out->port = client;
    out->submit = submit;
    out->read = read_reply;
    out->consume = consumed;
    return client_leave(client, 0);
}
// Storage：初始化模块任务端并绑定请求及回复运输端口。
SysResult_t VStorageEndpoint_Init(VStorageEndpoint *endpoint, VStorage *storage, const VStorageTransport *transport)
{
    if (!endpoint || endpoint->magic || !storage || !valid(transport))
        return SYS_ERR_INVALID_ARG;
    if (!transport->ops->is_storage(transport->port))
        return SYS_ERR_INVALID_STATE;
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->storage = storage;
    endpoint->transport = *transport;
    endpoint->magic = ENDPOINT_MAGIC;
    return 0;
}
// Storage：推进模块轮次和可靠消息，区分回复入队与消费确认。
SysResult_t VStorageEndpoint_RunRound(VStorageEndpoint *endpoint, uint32_t now)
{
    SysResult_t result;
    LCRequest incoming;
    if (!endpoint || endpoint->magic != ENDPOINT_MAGIC || !endpoint->transport.ops->is_storage(endpoint->transport.port))
        return SYS_ERR_INVALID_STATE;
    if (endpoint->busy)
        return SYS_ERR_BUSY;
    endpoint->busy = 1;

    if (endpoint->active && endpoint->sent)
    {
        if (!endpoint->ack_pending &&
            endpoint->transport.ops->receive_consumed(endpoint->transport.port, &endpoint->ack) == 0 &&
            !memcmp(&endpoint->ack, &endpoint->reply.completion, sizeof(endpoint->ack)))
            endpoint->ack_pending = 1;
        if (endpoint->ack_pending &&
            (!endpoint->accepted || VStorage_ConfirmCompletion(endpoint->storage, &endpoint->ack) == SYS_OK))
        {
            endpoint->active = 0;
            endpoint->ack_pending = 0;
        }
    }
    if (!endpoint->active && endpoint->transport.ops->receive_request(endpoint->transport.port, &incoming) == SYS_OK &&
        incoming.id.module == endpoint->storage->lifecycle.module &&
        incoming.id.session == endpoint->storage->lifecycle.session &&
        incoming.id.request > endpoint->request.id.request)
    {
        endpoint->request = incoming;
        endpoint->active = 1;
        endpoint->accepted = endpoint->ready = endpoint->sent = 0;
        memset(&endpoint->reply, 0, sizeof(endpoint->reply));
        endpoint->reply.completion.id = incoming.id;
        result = VStorage_Request(endpoint->storage, &incoming, now);
        if (!result)
            endpoint->accepted = 1;
        else
        {
            endpoint->reply.delivery_error = result;
            endpoint->ready = 1;
        }
    }
    //回复队列背压期间继续推进 Storage 文件收尾和在途帧归还。
    result = VStorage_RunRound(endpoint->storage, now);
    if (!result && endpoint->active && endpoint->accepted && !endpoint->ready &&
        VStorage_ReadCompletion(endpoint->storage, &endpoint->request.id, &endpoint->reply.completion) == SYS_OK)
        endpoint->ready = 1;
    if (!result && endpoint->active && endpoint->ready && !endpoint->sent)
    {
        result = endpoint->transport.ops->send_reply(endpoint->transport.port, &endpoint->reply);
        if (!result)
        {
            endpoint->sent = 1;
            endpoint->transport.ops->notify(endpoint->transport.port, VST_ACTOR_SUPERVISOR);
        }
    }
    endpoint->busy = 0;
    return result;
}
