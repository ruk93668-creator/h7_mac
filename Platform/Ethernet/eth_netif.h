//lwIP 与复制式 Ethernet MAC 之间的适配
#ifndef ETH_NETIF_H
#define ETH_NETIF_H

#include "eth_mac.h"
#include "lwip/netif.h"

#define ETH_NETIF_MTU 1500U
#define ETH_NETIF_FRAME_MAX (ETH_NETIF_MTU + 14U)
//RX 可以先消费驱动支持的更长帧，再按本 netif 的边界丢弃
#define ETH_NETIF_RX_BUFFER 1536U
#define ETH_NETIF_BUDGET 4U

typedef struct
{
	uint32_t tx_accepted;
	uint32_t tx_completed;
	uint32_t tx_busy;
	uint32_t tx_rejected;
	uint32_t tx_errors;
	uint32_t rx_delivered;
	uint32_t rx_no_pbuf;
	uint32_t rx_rejected;
	uint32_t rx_errors;
} EthNetifStatistics_t;

typedef struct
{
	EthNetifStatistics_t statistics;
#if defined(_MSC_VER) && !defined(__clang__)
	__declspec(align(32)) uint8_t tx_frame[ETH_NETIF_FRAME_MAX];
	__declspec(align(32)) uint8_t rx_frame[ETH_NETIF_RX_BUFFER];
#else
	uint8_t tx_frame[ETH_NETIF_FRAME_MAX] __attribute__((aligned(32)));
	uint8_t rx_frame[ETH_NETIF_RX_BUFFER] __attribute__((aligned(32)));
#endif
} EthNetif_t;

//netif->state 由调用者设置为独立上下文；以下操作均在 lwIP 的唯一执行上下文
err_t eth_netif_init(struct netif *netif);
void eth_netif_poll(struct netif *netif);

#endif
