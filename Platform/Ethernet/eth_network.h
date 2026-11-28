//单一执行上下文管理 lwIP、MAC 与 PHY 链路
#ifndef ETH_NETWORK_H
#define ETH_NETWORK_H

#include "eth_netif.h"

#if LWIP_DHCP
typedef enum
{
	ETH_ADDRESS_STATIC = 0,
	ETH_ADDRESS_DHCP,
	ETH_ADDRESS_DHCP_FALLBACK
} EthNetworkAddressMode_t;

typedef enum
{
	ETH_DHCP_DISABLED = 0,
	ETH_DHCP_WAIT_LINK,
	ETH_DHCP_STARTING,
	ETH_DHCP_SELECTING,
	ETH_DHCP_REQUESTING,
	ETH_DHCP_BOUND,
	ETH_DHCP_RENEWING,
	ETH_DHCP_REBINDING,
	ETH_DHCP_FALLBACK,
	ETH_DHCP_ERROR
} EthNetworkDhcpState_t;

typedef struct
{
	EthNetworkAddressMode_t mode;
	//STATIC 的配置，或 DHCP_FALLBACK 的显式静态地址
	ip4_addr_t ip;
	ip4_addr_t mask;
	ip4_addr_t gateway;
	uint32_t timeout_ms;
} EthNetworkAddressConfig_t;

typedef struct
{
	EthNetworkDhcpState_t state;
	err_t start_result;
	uint8_t lease_valid;
	uint8_t lwip_state;
	ip4_addr_t ip;
	ip4_addr_t mask;
	ip4_addr_t gateway;
	ip4_addr_t server;
	uint32_t lease_seconds;
	uint32_t t1_seconds;
	uint32_t t2_seconds;
	uint32_t starts;
	uint32_t timeouts;
	uint32_t fallbacks;
} EthNetworkAddressStatus_t;
#endif

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
	uint8_t mdio_streak;//仅统计实际链路采样连续失败次数
#if LWIP_DHCP
	EthNetworkAddressConfig_t address_config;
	EthNetworkAddressStatus_t address;
	uint32_t acquisition_tick;
	uint32_t last_start_tick;
	uint8_t dhcp_running;
	uint8_t dhcp_attempted;
	uint8_t timeout_reported;
#endif
} EthNetwork_t;

//系统只初始化一个默认接口。上下文需零初始化并在程序期间保持有效
err_t eth_network_init(EthNetwork_t *network, const uint8_t mac[6],
	const ip4_addr_t *ip, const ip4_addr_t *mask, const ip4_addr_t *gateway);
#if LWIP_DHCP
err_t eth_network_init_address(EthNetwork_t *network, const uint8_t mac[6],
	const EthNetworkAddressConfig_t *config);
#endif
//只轮询已初始化的当前拥有者，推进 RX、DHCP 和 lwIP 定时器，不可用于 ISR。
void eth_network_poll(EthNetwork_t *network);

//关闭端点期间只处理在途 RX 和协议定时器，同一拥有者调用。
//禁止 PHY 探测、MAC 启动或新建 DHCP 客户端。
void eth_network_drain_poll(EthNetwork_t *network);

//由唯一拥有者推进全局定时器；STOP 后仍需调用，未初始化时无操作。
//不访问 netif、启动 MAC 或建立服务；拒绝 ISR 和其他任务。
void eth_network_timers_poll(void);

//同一拥有者停止接口，调用前须确认上层 PCB 已退休。
//成功才移除 netif 和解除实例绑定；失败保留实例以便查询和再次停止。
err_t eth_network_stop(EthNetwork_t *network);

#endif
