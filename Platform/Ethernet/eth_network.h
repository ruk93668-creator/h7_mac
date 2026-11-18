//单一执行上下文管理 lwIP、MAC 与 PHY 链路
#ifndef ETH_NETWORK_H
#define ETH_NETWORK_H

#include "eth_netif.h"

typedef struct
{
	struct netif interface;
	EthNetif_t port;
	EthLinkState_t phy;
	EthResult_t last_driver_result;
	uint32_t link_ups;
	uint32_t link_downs;
	uint32_t service_tick;
	uint8_t initialized;
} EthNetwork_t;

//系统只初始化一个默认接口。上下文需零初始化并在程序期间保持有效
err_t eth_network_init(EthNetwork_t *network, const uint8_t mac[6],
	const ip4_addr_t *ip, const ip4_addr_t *mask, const ip4_addr_t *gateway);
void eth_network_poll(EthNetwork_t *network);

#endif
