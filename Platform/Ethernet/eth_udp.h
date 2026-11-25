#ifndef ETH_UDP_PORT_H
#define ETH_UDP_PORT_H

#include "lwip/netif.h"

#define ETH_UDP_MAX 1472U

typedef struct
{
	uint32_t tx_packets, tx_bytes, tx_errors, tx_no_memory;
	uint32_t rx_packets, rx_bytes, rx_errors, rx_invalid, rx_no_memory, peer_errors;
} EthUdpStats_t;

//数据和地址只在回调期间借用；返回错误则记录丢弃原因
typedef err_t (*EthUdpReceive_t)(const ip4_addr_t *ip, uint16_t port,
	const uint8_t *data, uint16_t length, void *arg);

//在拥有 lwIP Raw API 的上下文绑定端口，netif 和回调参数须保持有效至停止。
err_t eth_udp_start(struct netif *netif, uint16_t port, EthUdpReceive_t receive, void *arg);
//同步撤销 PCB 和接收回调，由同一拥有者调用。
void eth_udp_stop(void);
//同步复制 length 字节。ERR_OK 表示已交给 lwIP，发送者可复用 data。
err_t eth_udp_send(const ip4_addr_t *ip, uint16_t port, const void *data, uint16_t length);
//由同一拥有者获取累计统计，空输出指针不执行写入。
void eth_udp_get_statistics(EthUdpStats_t *stats);

#endif
