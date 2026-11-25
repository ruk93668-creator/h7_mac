#ifndef ETH_TCP_PORT_H
#define ETH_TCP_PORT_H

#include "lwip/netif.h"

#define ETH_TCP_QUEUE_BYTES 2920U

typedef enum
{
    ETH_TCP_SERVER = 0,
    ETH_TCP_CLIENT,
    ETH_TCP_ROLES
} EthTcpRole_t;

typedef enum
{
    ETH_TCP_IDLE = 0,
    ETH_TCP_CONNECTING,
    ETH_TCP_OPEN,
    ETH_TCP_DRAINING
} EthTcpState_t;

typedef struct
{
    EthTcpRole_t role;
    //连接分配时更新，旧句柄不能操作新连接
    uint32_t generation;
} EthTcpHandle_t;

enum
{
    ETH_TCP_EVENT_OPEN = 1,
    ETH_TCP_EVENT_READ = 2,
    ETH_TCP_EVENT_ACK = 4,
    ETH_TCP_EVENT_FIN = 8,
    ETH_TCP_EVENT_CLOSED = 16
};

typedef void (*EthTcpEvent_t)(EthTcpHandle_t handle, unsigned events, void *arg);
typedef struct
{
    uint32_t opens;
    uint32_t closes;
    uint32_t errors;
    uint32_t timeouts;
    uint32_t rejected;
    uint32_t submitted;
    uint32_t queued_lwip;
    //对端确认的累计字节数
    uint32_t acked;
    uint32_t received;
    uint32_t read;
    uint32_t send_full;
    uint32_t write_mem;
    uint32_t recv_full;
    uint32_t close_mem;
    uint32_t output_errors;
    //应用发送环待提交字节数
    uint16_t tx_pending;
    uint16_t rx_pending;
    //已交给lwIP但仍待确认的字节数
    uint16_t unacked;
    uint16_t tx_peak;
    uint16_t rx_peak;
    uint16_t unacked_peak;
    err_t last_error;
    EthTcpState_t state;
} EthTcpStats_t;

//配置连接事件回调和空闲超时，超时单位ms
err_t eth_tcp_configure(EthTcpEvent_t callback, void *arg, uint32_t idle_timeout_ms);

//在当前IPv4地址上建立监听
err_t eth_tcp_server_start(struct netif *netif, uint16_t port);

//向指定IPv4地址和端口发起异步连接
err_t eth_tcp_client_connect(struct netif *netif, const ip4_addr_t *peer, uint16_t port);

//停止监听并终止现有连接
void eth_tcp_stop(void);

//推进发送、关闭及超时，在网口轮询的同一上下文调用
void eth_tcp_poll(void);

//读取当前连接代次，连接重建后旧句柄失效
EthTcpHandle_t eth_tcp_handle(EthTcpRole_t role);

//读取连接状态，旧句柄返回空闲
EthTcpState_t eth_tcp_get_state(EthTcpHandle_t handle);

//允许当前连接在成功提交后继续发送，资源不足时保留重试间隔
#if defined(ETH_MJPEG_FAST_SEND_TEST)
err_t eth_tcp_enable_fast_send(EthTcpHandle_t handle);
#endif

//复制数据到内部环，成功返回后调用者可立即复用源缓冲
err_t eth_tcp_send(EthTcpHandle_t handle, const void *data, uint16_t length);

//复制可用数据并归还接收窗口，返回实际读取字节数
uint16_t eth_tcp_read(EthTcpHandle_t handle, void *data, uint16_t capacity);

//申请有序关闭，已提交的数据留待排空
err_t eth_tcp_close(EthTcpHandle_t handle);

//立即终止有效连接
void eth_tcp_abort(EthTcpHandle_t handle);

//读取指定角色的计数快照
void eth_tcp_get_statistics(EthTcpRole_t role, EthTcpStats_t *stats);

//查询服务端是否已有监听PCB
int eth_tcp_listening(void);

#endif
