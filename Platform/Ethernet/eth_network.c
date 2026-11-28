#include "eth_execution.h"
//NO_SYS：同一个循环管理 MAC 和 lwIP 定时器
#include "eth_network.h"
#include <string.h>
#include "lwip/init.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#if LWIP_DHCP
#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"
#endif

#define ETH_NETWORK_SERVICE_MS 10U

static uint8_t s_lwip_initialized;
static EthNetwork_t *s_owner;
static uint8_t s_mac_initialized;
static uint8_t s_mac_attempted;

#if LWIP_DHCP
#define ETH_DHCP_RETRY_MS 1000U

static void EthNetwork_ClearAddress(EthNetwork_t *network)
{
	netif_set_addr(&network->interface, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4);
}

static void EthNetwork_StopDhcp(EthNetwork_t *network)
{
	if (netif_dhcp_data(&network->interface) != NULL)
	{
		dhcp_release_and_stop(&network->interface);
		dhcp_cleanup(&network->interface);
	}
	network->dhcp_running = 0U;
}

static EthNetworkDhcpState_t EthNetwork_DhcpState(uint8_t state)
{
	switch (state)
	{
	case DHCP_STATE_INIT:
		return ETH_DHCP_STARTING;
	case DHCP_STATE_SELECTING:
	case DHCP_STATE_BACKING_OFF:
		return ETH_DHCP_SELECTING;
	case DHCP_STATE_REQUESTING:
	case DHCP_STATE_CHECKING:
	case DHCP_STATE_REBOOTING:
		return ETH_DHCP_REQUESTING;
	case DHCP_STATE_BOUND:
		return ETH_DHCP_BOUND;
	case DHCP_STATE_RENEWING:
		return ETH_DHCP_RENEWING;
	case DHCP_STATE_REBINDING:
		return ETH_DHCP_REBINDING;
	default:
		return ETH_DHCP_ERROR;
	}
}

static void EthNetwork_AddressPoll(EthNetwork_t *network, uint32_t now)
{
	struct netif *interface = &network->interface;
	struct dhcp *dhcp = netif_dhcp_data(interface);
	EthNetworkAddressStatus_t *status = &network->address;
	const EthNetworkAddressConfig_t *config = &network->address_config;
	uint8_t valid = (uint8_t)(netif_is_link_up(interface) && dhcp_supplied_address(interface));

	if (config->mode != ETH_ADDRESS_STATIC && netif_is_link_up(interface) &&
		status->state != ETH_DHCP_FALLBACK)
	{
		if (status->lease_valid && !valid)
		{
			//租约到期或 NAK 后，上游继续发现；开启新的等待窗口
			network->acquisition_tick = now;
			network->timeout_reported = 0U;
		}
		if (network->dhcp_running && (dhcp == NULL || dhcp->state == DHCP_STATE_OFF))
			network->dhcp_running = 0U;
		if (!valid && !network->timeout_reported &&
			(uint32_t)(now - network->acquisition_tick) >= config->timeout_ms)
		{
			network->timeout_reported = 1U;
			++status->timeouts;
			if (config->mode == ETH_ADDRESS_DHCP_FALLBACK)
			{
				//此处尚无有效租约，不会尝试 Release
				EthNetwork_StopDhcp(network);
				etharp_cleanup_netif(interface);
				netif_set_addr(interface, &config->ip, &config->mask, &config->gateway);
				status->state = ETH_DHCP_FALLBACK;
				++status->fallbacks;
			}
		}
		if (status->state != ETH_DHCP_FALLBACK)
		{
			if (!network->dhcp_running && (!network->dhcp_attempted ||
				(uint32_t)(now - network->last_start_tick) >= ETH_DHCP_RETRY_MS))
			{
				network->dhcp_attempted = 1U;
				network->last_start_tick = now;
				status->state = ETH_DHCP_STARTING;
				++status->starts;
				status->start_result = dhcp_start(interface);
				network->dhcp_running = (uint8_t)(status->start_result == ERR_OK);
				dhcp = netif_dhcp_data(interface);
			}
			status->state = network->dhcp_running ? EthNetwork_DhcpState(dhcp->state) : ETH_DHCP_ERROR;
		}
	}
	dhcp = netif_dhcp_data(interface);
	status->lease_valid = valid;
	status->lwip_state = dhcp ? dhcp->state : DHCP_STATE_OFF;
	status->ip = *netif_ip4_addr(interface);
	status->mask = *netif_ip4_netmask(interface);
	status->gateway = *netif_ip4_gw(interface);
	status->server = valid && dhcp ? *ip_2_ip4(&dhcp->server_ip_addr) : *IP4_ADDR_ANY4;
	status->lease_seconds = valid && dhcp ? dhcp->offered_t0_lease : 0U;
	status->t1_seconds = valid && dhcp ? dhcp->offered_t1_renew : 0U;
	status->t2_seconds = valid && dhcp ? dhcp->offered_t2_rebind : 0U;
}
#endif

static void EthNetwork_LinkDown(EthNetwork_t *network)
{
	if (netif_is_link_up(&network->interface))
	{
		netif_set_link_down(&network->interface);
#if LWIP_DHCP
		if (network->address_config.mode != ETH_ADDRESS_STATIC)
		{
			//上游 stop 可能尝试 Release；先关闭 linkoutput，无报文能提交给 MAC
			EthNetwork_StopDhcp(network);
			EthNetwork_ClearAddress(network);
			network->address.state = ETH_DHCP_WAIT_LINK;
		}
#endif
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
#if LWIP_DHCP
				if (network->address_config.mode != ETH_ADDRESS_STATIC)
				{
					network->acquisition_tick = sys_now();
					network->dhcp_attempted = 0U;
					network->timeout_reported = 0U;
					network->address.state = ETH_DHCP_STARTING;
				}
#endif
			}
		}
		else if (result == ETH_OK)
			result = ETH_LINK_DOWN;
	}
	else if (result != ETH_MDIO && (result != ETH_OK || statistics.state != ETH_MAC_RUNNING))
	{
		EthNetwork_LinkDown(network);
		network->phy.up = 0U;
		if (result == ETH_OK)
			result = ETH_STATE;
	}
	if (result != ETH_MDIO)
		network->mdio_streak = 0U;
	else if (network->mdio_streak < UINT8_MAX)
		++network->mdio_streak;
	network->last_driver_result = result;
}

static err_t EthNetwork_Init(EthNetwork_t *network, const uint8_t mac[6],
	const ip4_addr_t *ip, const ip4_addr_t *mask, const ip4_addr_t *gateway)
{
	if (network == NULL || mac == NULL || ip == NULL || mask == NULL || gateway == NULL)
		return ERR_ARG;
	if (s_owner != NULL)
		return ERR_USE;

    s_owner = network;
    if (!s_mac_initialized)
    {
        network->last_driver_result = s_mac_attempted ? eth_mac_recover(mac) : eth_mac_init(mac);
        s_mac_attempted = 1U;
        if (network->last_driver_result == ETH_OK)
            s_mac_initialized = 1U;
    }
    else
    {
        EthStatistics_t state;
        uint8_t current[6];
        if (eth_mac_get_statistics(&state) != ETH_OK || state.state != ETH_MAC_READY ||
            eth_mac_get_address(current) != ETH_OK ||
            memcmp(current, mac, sizeof(current)) != 0)
            return ERR_IF;
        network->last_driver_result = ETH_OK;
    }
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
	network->mdio_streak = 0U;
	network->service_tick = sys_now() - ETH_NETWORK_SERVICE_MS;
	return ERR_OK;
}

#if LWIP_DHCP
static int EthNetwork_FallbackValid(const EthNetworkAddressConfig_t *config)
{
	uint32_t ip = lwip_ntohl(ip4_addr_get_u32(&config->ip));
	uint32_t mask = lwip_ntohl(ip4_addr_get_u32(&config->mask));
	uint32_t host = ip & ~mask;

	return ip != 0U && ip != UINT32_MAX && mask != 0U &&
		ip4_addr_netmask_valid(ip4_addr_get_u32(&config->mask)) &&
		!ip4_addr_ismulticast(&config->ip) && !ip4_addr_isloopback(&config->ip) &&
		(~mask < 3U || (host != 0U && host != ~mask));
}

err_t eth_network_init_address(EthNetwork_t *network, const uint8_t mac[6],
	const EthNetworkAddressConfig_t *config)
{
    if (!eth_execution_allowed())
        return ERR_IF;

	err_t result;
	int use_static;

	if (network == NULL || config == NULL || (config->mode != ETH_ADDRESS_STATIC &&
		config->mode != ETH_ADDRESS_DHCP && config->mode != ETH_ADDRESS_DHCP_FALLBACK))
		return ERR_ARG;
	use_static = config->mode == ETH_ADDRESS_STATIC;
	if ((!use_static && (!config->timeout_ms || config->timeout_ms > UINT32_C(0x7FFFFFFF))) ||
		(config->mode == ETH_ADDRESS_DHCP_FALLBACK && !EthNetwork_FallbackValid(config)))
		return ERR_ARG;
	result = EthNetwork_Init(network, mac, use_static ? &config->ip : IP4_ADDR_ANY4,
		use_static ? &config->mask : IP4_ADDR_ANY4, use_static ? &config->gateway : IP4_ADDR_ANY4);
	if (result == ERR_OK)
	{
		network->address_config = *config;
		network->address.state = use_static ? ETH_DHCP_DISABLED : ETH_DHCP_WAIT_LINK;
	}
	return result;
}
#endif

err_t eth_network_init(EthNetwork_t *network, const uint8_t mac[6],
	const ip4_addr_t *ip, const ip4_addr_t *mask, const ip4_addr_t *gateway)
{
    if (!eth_execution_allowed())
        return ERR_IF;

#if LWIP_DHCP
	EthNetworkAddressConfig_t config = {.mode = ETH_ADDRESS_STATIC};
	if (ip == NULL || mask == NULL || gateway == NULL)
		return ERR_ARG;
	config.ip = *ip;
	config.mask = *mask;
	config.gateway = *gateway;
	return eth_network_init_address(network, mac, &config);
#else
	return EthNetwork_Init(network, mac, ip, mask, gateway);
#endif
}

void eth_network_poll(EthNetwork_t *network)
{
    if (!eth_execution_allowed())
        return;

	uint32_t now;

	if (network == NULL || network != s_owner || !network->initialized)
		return;
	now = sys_now();
	if ((uint32_t)(now - network->service_tick) >= ETH_NETWORK_SERVICE_MS)
	{
		network->service_tick = now;
		EthNetwork_Service(network);
	}
	eth_netif_poll(&network->interface);
	sys_check_timeouts();
#if LWIP_DHCP
	EthNetwork_AddressPoll(network, sys_now());
#endif
}

void eth_network_drain_poll(EthNetwork_t *network)
{
	if (!eth_execution_allowed() || network == NULL || network != s_owner)
		return;
	if (network->initialized)
		eth_netif_poll(&network->interface);
	eth_network_timers_poll();
}

//netif 生命周期结束后，全局 cyclic timer 仍由同一拥有者推进。
void eth_network_timers_poll(void)
{
    if (eth_execution_allowed() && s_lwip_initialized)
        sys_check_timeouts();
}

err_t eth_network_stop(EthNetwork_t *network)
{
    if (!eth_execution_allowed())
        return ERR_IF;
    if (network == NULL || network != s_owner)
        return ERR_ARG;
    if (network->initialized)
    {
        netif_set_link_down(&network->interface);
        netif_set_down(&network->interface);
#if LWIP_DHCP
        if (netif_dhcp_data(&network->interface) != NULL)
        {
            //先撤销在线状态；停止不发送 Release。
            dhcp_stop(&network->interface);
            dhcp_cleanup(&network->interface);
        }
        network->dhcp_running = 0U;
#endif
        etharp_cleanup_netif(&network->interface);
    }
    {
        EthStatistics_t statistics;
        network->last_driver_result = eth_mac_get_statistics(&statistics);
        if (network->last_driver_result != ETH_OK)
            return ERR_IF;
        if (statistics.state != ETH_MAC_OFF)
        {
            network->last_driver_result = eth_mac_stop();
            if (network->last_driver_result != ETH_OK)
                return ERR_IF;
        }
    }
    if (network->initialized)
        netif_remove(&network->interface);
    network->initialized = 0U;
#if LWIP_DHCP
    //停止状态不得把旧租约作为仍可用地址发布；诊断累计计数保留。
    network->address.state = ETH_DHCP_DISABLED;
    network->address.lease_valid = 0U;
    network->address.lwip_state = DHCP_STATE_OFF;
    network->address.ip = *IP4_ADDR_ANY4;
    network->address.mask = *IP4_ADDR_ANY4;
    network->address.gateway = *IP4_ADDR_ANY4;
    network->address.server = *IP4_ADDR_ANY4;
    network->address.lease_seconds = 0U;
    network->address.t1_seconds = 0U;
    network->address.t2_seconds = 0U;
#endif
    s_owner = NULL;
    return ERR_OK;
}
