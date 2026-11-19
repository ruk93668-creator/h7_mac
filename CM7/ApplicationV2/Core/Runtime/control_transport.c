// 将可信传输端的请求交给统一控制服务。

#include "control_transport.h"
#include <string.h>

#define MAGIC 0x43544137U

// 绑定传输适配器、控制服务及运输端口。
SysResult_t CSAdapter_Init(CSAdapter *adapter, CSService *service, uint32_t channel, const CSAdapterOps *adapter_ops,
                           void *port_context)
{
    if (!adapter || adapter->magic || !service || channel < CS_IPC || channel > CS_ETHERNET || !adapter_ops || !adapter_ops->owner ||
        !adapter_ops->receive || !adapter_ops->send)
        return SYS_ERR_INVALID_ARG;
    if (!adapter_ops->owner(port_context))
        return SYS_ERR_INVALID_STATE;
    memset(adapter, 0, sizeof(*adapter));
    adapter->service = service;
    adapter->channel = channel;
    adapter->ops = adapter_ops;
    adapter->port = port_context;
    adapter->magic = MAGIC;
    return SYS_OK;
}

// 按操作类型把运输输入交给统一控制服务。
static SysResult_t dispatch(CSAdapter *adapter, const CSIngress *message, uint32_t now)
{
    CSService *service = adapter->service;
    CSEgress *out = &adapter->output;
    const CSPeer *peer = &message->route.peer;

    switch (message->type)
    {
    case CSA_OPEN:
        return CS_Open(service, peer, &out->session);
    case CSA_SUBMIT:
        return CS_Submit(service, peer, &message->body.request, now, &out->reply);
    case CSA_QUERY:
        return CS_Query(service, peer, &message->body.id, &out->reply);
    case CSA_CONFIRM:
        return CS_Confirm(service, peer, &message->body.reply);
    case CSA_CANCEL:
        return CS_Cancel(service, peer, &message->body.id);
    case CSA_CLOSE:
        return CS_Close(service, peer, message->body.id.boot, message->body.id.session);
    default:
        return SYS_ERR_PROTOCOL;
    }
}

// 处理传输入口并续送尚未交付的响应。
SysResult_t CSAdapter_Process(CSAdapter *adapter, uint32_t now)
{
    CSIngress message;
    SysResult_t result;
    if (!adapter || adapter->magic != MAGIC || !adapter->ops->owner(adapter->port))
        return SYS_ERR_INVALID_STATE;
    if (adapter->busy)
        return SYS_ERR_BUSY;
    adapter->busy = 1U;
    if (!adapter->pending)
    {
        //没有待发送回复时才接收下一个输入。
        result = adapter->ops->receive(adapter->port, &message);
        if (result || message.route.peer.transport != adapter->channel || !message.route.exchange)
        {
            adapter->busy = 0U;
            return result ? result : SYS_ERR_PROTOCOL;
        }
        memset(&adapter->output, 0, sizeof(adapter->output));
        adapter->output.route = message.route;
        adapter->output.type = message.type;
        adapter->output.boot = adapter->service->boot;
        //使用运输端提供的身份进入统一控制服务。
        adapter->output.result = dispatch(adapter, &message, now);
        adapter->pending = 1U;
    }
    //回复背压保留同一个exchange，不重新受理，不代表客户端消费。
    //只发送已缓存的回复，背压不重新派发请求。
    result = adapter->ops->send(adapter->port, &adapter->output);
    if (!result)
        adapter->pending = 0U;
    adapter->busy = 0U;
    return result;
}
