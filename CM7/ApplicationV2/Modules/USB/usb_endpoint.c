// 在执行端与 USB 任务间运输请求及完成确认。

#include "usb_endpoint.h"
#include <string.h>
#define CLIENT_MAGIC 0x56554335U
#define ENDPOINT_MAGIC 0x56554535U
// 校验异步执行请求的身份字段。
static int valid(const VUSBTransport *transport)
{
    return transport && transport->ops && transport->ops->is_usb && transport->ops->is_supervisor && transport->ops->send_request &&
           transport->ops->receive_request && transport->ops->send_reply && transport->ops->receive_reply &&
           transport->ops->send_consumed && transport->ops->receive_consumed && transport->ops->notify;
}
// 校验所有者调用并设置重入保护，调用方仍负责跨任务串行化。
static SysResult_t client_enter(VUSBClient *client)
{
    if (!client || client->magic != CLIENT_MAGIC || !client->transport.ops->is_supervisor(client->transport.port))
        return SYS_ERR_INVALID_STATE;
    if (client->busy)
        return SYS_ERR_BUSY;
    client->busy = 1;
    return 0;
}
// 退出当前调用的重入保护。
static SysResult_t client_leave(VUSBClient *client, SysResult_t result)
{
    client->busy = 0;
    return result;
}
// USB：初始化执行侧客户端并绑定运输端口。
SysResult_t VUSBClient_Init(VUSBClient *client, uint32_t module, uint32_t session,
                            const VUSBTransport *transport)
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
    VUSBClient *client = port_context;
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
        client->transport.ops->notify(client->transport.port, VU_ACTOR_USB);
    }
    return client_leave(client, result);
}
// 读取与当前待处理身份匹配的完成消息。
static SysResult_t read_reply(void *port_context, const V2ExecId *id, LCCompletion *out)
{
    VUSBClient *client = port_context;
    VUSBReply reply;
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
            return client_leave(client, SYS_ERR_NOT_FOUND); // 取走迟到回复，避免进入当前事务。
        if (reply.delivery_error > 0 ||
            (!reply.delivery_error && (reply.completion.result > 0 || reply.completion.actual_result > 0 ||
                                   !LC_Stable(reply.completion.fact))))
            return client_leave(client, SYS_ERR_PROTOCOL);
        client->reply = reply;
        client->has_reply = 1;
    }
    if (client->reply.delivery_error)
    {
        /* Execution Manager不会消费拒绝执行业务的回复，由传输层确认。 */
        if (client->active)
        {
            result = client->transport.ops->send_consumed(client->transport.port, &client->reply.completion);
            if (result)
                return client_leave(client, result == SYS_ERR_NO_RESOURCE ? SYS_ERR_BUSY : result);
            client->active = 0;
            client->transport.ops->notify(client->transport.port, VU_ACTOR_USB);
        }
        return client_leave(client, client->reply.delivery_error);
    }
    *out = client->reply.completion;
    return client_leave(client, 0);
}
// 确认指定完成已被客户端实际消费。
static SysResult_t consumed(void *port_context, const LCCompletion *completion)
{
    VUSBClient *client = port_context;
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
        client->transport.ops->notify(client->transport.port, VU_ACTOR_USB);
    }
    return client_leave(client, result);
}
// USB：返回供 Execution Manager 使用的模块端口。
SysResult_t VUSBClient_Endpoint(VUSBClient *client, EMEndpoint *out)
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
// USB：初始化模块任务端并绑定请求及回复运输端口。
SysResult_t VUSBEndpoint_Init(VUSBEndpoint *endpoint, VUSB *usb, const VUSBTransport *transport)
{
    if (!endpoint || endpoint->magic || !usb || !valid(transport))
        return SYS_ERR_INVALID_ARG;
    if (!transport->ops->is_usb(transport->port))
        return SYS_ERR_INVALID_STATE;
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->usb = usb;
    endpoint->transport = *transport;
    endpoint->magic = ENDPOINT_MAGIC;
    return 0;
}
// USB：推进模块轮次和可靠消息，区分回复入队与消费确认。
SysResult_t VUSBEndpoint_RunRound(VUSBEndpoint *endpoint, uint32_t now)
{
    SysResult_t result;
    LCRequest incoming;
    if (!endpoint || endpoint->magic != ENDPOINT_MAGIC || !endpoint->transport.ops->is_usb(endpoint->transport.port))
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
        if (endpoint->ack_pending && (!endpoint->accepted || VUSB_ConfirmCompletion(endpoint->usb, &endpoint->ack) == SYS_OK))
        {
            endpoint->active = 0;
            endpoint->ack_pending = 0;
        }
    }
    if (!endpoint->active && endpoint->transport.ops->receive_request(endpoint->transport.port, &incoming) == SYS_OK &&
        incoming.id.module == endpoint->usb->lifecycle.module &&
        incoming.id.session == endpoint->usb->lifecycle.session &&
        incoming.id.request > endpoint->request.id.request)
    {
        endpoint->request = incoming;
        endpoint->active = 1;
        endpoint->accepted = endpoint->ready = endpoint->sent = 0;
        memset(&endpoint->reply, 0, sizeof(endpoint->reply));
        endpoint->reply.completion.id = incoming.id;
        result = VUSB_Request(endpoint->usb, &incoming, now);
        if (!result)
            endpoint->accepted = 1;
        else
        {
            endpoint->reply.delivery_error = result;
            endpoint->ready = 1;
        }
    }
    /* 运输背压不影响UVC推进、帧归还或后台停止。 */
    result = VUSB_RunRound(endpoint->usb, now);
    if (!result && endpoint->active && endpoint->accepted && !endpoint->ready &&
        VUSB_ReadCompletion(endpoint->usb, &endpoint->request.id, &endpoint->reply.completion) == SYS_OK)
        endpoint->ready = 1;
    if (!result && endpoint->active && endpoint->ready && !endpoint->sent)
    {
        result = endpoint->transport.ops->send_reply(endpoint->transport.port, &endpoint->reply);
        if (!result)
        {
            endpoint->sent = 1;
            endpoint->transport.ops->notify(endpoint->transport.port, VU_ACTOR_SUPERVISOR);
        }
    }
    endpoint->busy = 0;
    return result;
}
