//NO_SYS：同一个循环管理 MAC 和 lwIP 定时器
#include "eth_network.h"
#include "lwip/init.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"

#define ETH_NETWORK_SERVICE_MS 10U

static uint8_t s_lwip_initialized;
static EthNetwork_t *s_owner;

static void EthNetwork_LinkDown(EthNetwork_t *network)
{
	if (netif_is_link_up(&network->interface))
	{
		netif_set_link_down(&network->interface);
		etharp_cleanup_netif(&network->interface);
		++network->link_downs;
	}
}

static void EthNetwork_Service(EthNetwork_t *network)
{
	EthStatistics_t statistics;
	EthResult_t result = eth_mac_get_statistics(&statistics);

	if (result == ETH_OK && (statistics.state == ETH_MAC_RUNNING ||
		(statistics.state == ETH_MAC_FAULT && netif_is_link_up(&network->interface))))
		result = eth_mac_service();

	if (result == ETH_OK && statistics.state == ETH_MAC_READY)
	{
		//get_link 返回 BUSY 表示协商未完成；仅在 Start 成功后开通 netif
		result = eth_mac_get_link(&network->phy);
		if (result == ETH_OK && network->phy.up)
		{
			result = eth_mac_start();
			if (result == ETH_OK)
			{
				netif_set_link_up(&network->interface);
				++network->link_ups;
			}
		}
		else if (result == ETH_OK)
			result = ETH_LINK_DOWN;
	}
	else if (result != ETH_OK || statistics.state != ETH_MAC_RUNNING)
	{
		EthNetwork_LinkDown(network);
		(void)eth_mac_get_link(&network->phy);
		if (result == ETH_OK)
			result = ETH_STATE;
	}
	network->last_driver_result = result;
}

err_t eth_network_init(EthNetwork_t *network, const uint8_t mac[6],
	const ip4_addr_t *ip, const ip4_addr_t *mask, const ip4_addr_t *gateway)
{
	if (network == NULL || mac == NULL || ip == NULL || mask == NULL || gateway == NULL)
		return ERR_ARG;
	if (s_owner != NULL)
		return ERR_USE;

	network->last_driver_result = eth_mac_init(mac);
	if (network->last_driver_result != ETH_OK)
		return ERR_IF;
	if (!s_lwip_initialized)
	{
		lwip_init();
		s_lwip_initialized = 1U;
	}
	if (netif_add(&network->interface, ip, mask, gateway, &network->port,
		eth_netif_init, ethernet_input) == NULL)
		return ERR_IF;
	s_owner = network;
	netif_set_default(&network->interface);
	netif_set_up(&network->interface);
	network->initialized = 1U;
	network->service_tick = sys_now() - ETH_NETWORK_SERVICE_MS;
	return ERR_OK;
}

void eth_network_poll(EthNetwork_t *network)
{
	uint32_t now;

	if (network == NULL || network != s_owner)
		return;
	now = sys_now();
	if ((uint32_t)(now - network->service_tick) >= ETH_NETWORK_SERVICE_MS)
	{
		network->service_tick = now;
		EthNetwork_Service(network);
	}
	eth_netif_poll(&network->interface);
	sys_check_timeouts();
}
