// Camera 客户端与任务端的请求、完成和消费适配。

#include "camera_endpoint.h"
#include <string.h>
#define CLIENT_MAGIC 0x56434334U
#define ENDPOINT_MAGIC 0x56434534U
// 校验异步执行请求的身份字段。
static int valid(const VCameraTransport *transport)
{
    return transport && transport->ops && transport->ops->is_camera && transport->ops->is_supervisor && transport->ops->send_request &&
           transport->ops->receive_request && transport->ops->send_reply && transport->ops->receive_reply &&
           transport->ops->send_consumed && transport->ops->receive_consumed && transport->ops->notify;
}
// 校验所有者调用并设置重入保护，调用方仍负责跨任务串行化。
static SysResult_t client_enter(VCameraClient *client)
{
    if (!client || client->magic != CLIENT_MAGIC || !client->transport.ops->is_supervisor(client->transport.port))
        return SYS_ERR_INVALID_STATE;
    if (client->busy)
        return SYS_ERR_BUSY;
    client->busy = 1;
    return 0;
}
// 退出当前调用的重入保护。
static SysResult_t client_leave(VCameraClient *client, SysResult_t result)
{
    client->busy = 0;
    return result;
}
// Camera：初始化执行侧客户端并绑定运输端口。
SysResult_t VCameraClient_Init(VCameraClient *client, uint32_t module, uint32_t session,
                               const VCameraTransport *transport)
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
    VCameraClient *client = port_context;
    SysResult_t result = client_enter(client);
    if (result)
        return result;
    if (!request || request->id.module != client->module || request->id.session != client->session || !request->id.request ||
        !LC_Target(request->id.operation) || !LC_Stable(request->expected) || !request->timeout ||
        request->timeout > 0x7FFFFFFFU)
        return client_leave(client, SYS_ERR_PROTOCOL);
    if (request->id.request == client->request.id.request)
        return client_leave(client, !memcmp(request, &client->request, sizeof(*request)) ? 0 : SYS_ERR_PROTOCOL);
    if (request->id.request < client->request.id.request)
        return client_leave(client, SYS_ERR_PROTOCOL);
    if (client->active)
        return client_leave(client, SYS_ERR_BUSY);
    result = client->transport.ops->send_request(client->transport.port, request);
    if (!result)
    {
        client->request = *request;
        client->active = 1;
        client->has_reply = 0;
        client->transport.ops->notify(client->transport.port, VC_ACTOR_CAMERA);
    }
    return client_leave(client, result);
}
// 读取与当前待处理身份匹配的完成消息。
static SysResult_t read_reply(void *port_context, const V2ExecId *id, LCCompletion *out)
{
    VCameraClient *client = port_context;
    VCameraReply reply;
    SysResult_t result = client_enter(client);
    if (result)
        return result;
    if (!out || V2_Overlap(client, sizeof(*client), out, sizeof(*out)) || !LC_IdEqual(id, &client->request.id))
        return client_leave(client, SYS_ERR_PROTOCOL);
    if (!client->has_reply)
    {
        result = client->transport.ops->receive_reply(client->transport.port, &reply);
        if (result)
            return client_leave(client, result);
        if (!LC_IdEqual(&reply.completion.id, id))
            return client_leave(client, SYS_ERR_NOT_FOUND); /* stale is drained, never applied */
        if (reply.delivery_error > 0 ||
            (!reply.delivery_error && (reply.completion.result > 0 || reply.completion.actual_result > 0 ||
                                   !LC_Stable(reply.completion.fact))))
            return client_leave(client, SYS_ERR_PROTOCOL);
        client->reply = reply;
        client->has_reply = 1;
    }
    if (client->reply.delivery_error)
        return client_leave(client, client->reply.delivery_error);
    *out = client->reply.completion;
    return client_leave(client, 0);
}
// 确认指定完成已被客户端实际消费。
static SysResult_t consumed(void *port_context, const LCCompletion *completion)
{
    VCameraClient *client = port_context;
    SysResult_t result = client_enter(client);
    if (result)
        return result;
    if (!completion || !client->has_reply || client->reply.delivery_error ||
        memcmp(completion, &client->reply.completion, sizeof(*completion)))
        return client_leave(client, SYS_ERR_PROTOCOL);
    if (!client->active)
        return client_leave(client, 0);
    result = client->transport.ops->send_consumed(client->transport.port, completion);
    if (!result)
    {
        client->active = 0;
        client->transport.ops->notify(client->transport.port, VC_ACTOR_CAMERA);
    }
    return client_leave(client, result);
}
// Camera：返回供 Execution Manager 使用的模块端口。
SysResult_t VCameraClient_Endpoint(VCameraClient *client, EMEndpoint *out)
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
// Camera：初始化模块任务端并绑定请求及回复运输端口。
SysResult_t VCameraEndpoint_Init(VCameraEndpoint *endpoint, VCamera *camera, const VCameraTransport *transport)
{
    if (!endpoint || endpoint->magic || !camera || !valid(transport))
        return SYS_ERR_INVALID_ARG;
    if (!transport->ops->is_camera(transport->port))
        return SYS_ERR_INVALID_STATE;
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->camera = camera;
    endpoint->transport = *transport;
    endpoint->magic = ENDPOINT_MAGIC;
    return 0;
}
// Camera：推进模块轮次和可靠消息，区分回复入队与消费确认。
SysResult_t VCameraEndpoint_RunRound(VCameraEndpoint *endpoint, uint32_t now)
{
    SysResult_t status_result;
    if (!endpoint || endpoint->magic != ENDPOINT_MAGIC || !endpoint->transport.ops->is_camera(endpoint->transport.port))
        return SYS_ERR_INVALID_STATE;
    if (endpoint->busy)
        return SYS_ERR_BUSY;
    endpoint->busy = 1;
    status_result = VCamera_BeginRound(endpoint->camera);
    if (status_result)
    {
        endpoint->busy = 0;
        return status_result;
    }
    /* ACK consumption is handled before the next command, but does not clear a
     * native control result still awaiting its owner-local consume. */
    if (endpoint->active && endpoint->sent)
    {
        if (!endpoint->ack_pending && endpoint->transport.ops->receive_consumed(endpoint->transport.port, &endpoint->ack) == 0)
        {
            if (!memcmp(&endpoint->ack, &endpoint->reply.completion, sizeof(endpoint->ack)))
                endpoint->ack_pending = 1;
        }
        if (endpoint->ack_pending)
        {
            status_result = endpoint->accepted ? VCamera_ConfirmCompletion(endpoint->camera, &endpoint->ack) : SYS_OK;
            if (!status_result)
            {
                endpoint->active = 0;
                endpoint->ack_pending = 0;
                endpoint->ready = 0;
                endpoint->sent = 0;
            }
        }
    }
    if (!endpoint->active && endpoint->transport.ops->receive_request(endpoint->transport.port, &endpoint->request) == 0)
    {
        endpoint->active = 1;
        endpoint->accepted = 0;
        endpoint->ready = 0;
        endpoint->sent = 0;
        endpoint->ack_pending = 0;
        memset(&endpoint->reply, 0, sizeof(endpoint->reply));
        endpoint->reply.completion.id = endpoint->request.id;
        status_result = VCamera_Request(endpoint->camera, &endpoint->request, now);
        if (!status_result)
            endpoint->accepted = 1;
        else
        {
            endpoint->reply.delivery_error = status_result;
            endpoint->ready = 1;
        }
    }
    status_result = VCamera_Process(endpoint->camera, now);
    if (!status_result)
        status_result = VCamera_Returns(endpoint->camera);
    if (!status_result)
        status_result = VCamera_FinishRound(endpoint->camera, now);
    if (!status_result && endpoint->active && endpoint->accepted && !endpoint->ready &&
        VCamera_ReadCompletion(endpoint->camera, &endpoint->request.id, &endpoint->reply.completion) == 0)
    {
        endpoint->reply.delivery_error = 0;
        endpoint->ready = 1;
    }
    if (!status_result && endpoint->active && endpoint->ready && !endpoint->sent)
    {
        status_result = endpoint->transport.ops->send_reply(endpoint->transport.port, &endpoint->reply);
        if (!status_result)
        {
            endpoint->sent = 1;
            endpoint->transport.ops->notify(endpoint->transport.port, VC_ACTOR_SUPERVISOR);
        }
    }
    endpoint->busy = 0;
    return status_result;
}
