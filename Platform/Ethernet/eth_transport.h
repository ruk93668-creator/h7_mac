#ifndef ETH_TRANSPORT_H
#define ETH_TRANSPORT_H

#include "eth_network.h"
#include "eth_tcp.h"
#include "eth_udp.h"

typedef struct
{
	uint16_t udp_port, listen_port, client_port;
	ip4_addr_t client_ip;
	uint32_t reconnect_ms, idle_ms; //重连间隔和空闲超时，单位 ms；0 禁用对应策略
	EthUdpReceive_t udp_receive;
	EthTcpEvent_t tcp_event;
	void *arg;
} EthTransportConfig_t;

typedef struct
{
	uint32_t link_ready, link_lost, client_attempts;
	err_t udp_start, server_start, client_start;
	uint8_t ready, listening, connections;
	EthTcpStats_t tcp[ETH_TCP_ROLES];
	EthUdpStats_t udp;
} EthTransportStats_t;

//单实例，须在 eth_network_init 成功后初始化，并在同一循环轮询
err_t eth_transport_init(EthNetwork_t *network, const EthTransportConfig_t *config);
void eth_transport_poll(void);
//返回非零表示地址和链路可用，端点的建立结果查询统计。
int eth_transport_ready(void);
//同一执行上下文读取快照，空输出指针无操作。
void eth_transport_get_statistics(EthTransportStats_t *stats);

//停止端点准入，推进 TCP 排空后由 finish 确认资源退休。
void eth_transport_begin_stop(void);
//ERR_INPROGRESS 表示仍有 PCB，包括 TIME_WAIT，必须继续轮询。
//ERR_OK 后解除旧 Network 绑定，才能接受新的 init。
err_t eth_transport_finish_stop(void);

#endif
