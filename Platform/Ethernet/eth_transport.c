#include "eth_execution.h"
#include "eth_transport.h"
#include "lwip/sys.h"

static EthNetwork_t *s_network;
static EthTransportConfig_t s_config;
static EthTransportStats_t s_stats;
static ip4_addr_t s_address;
static uint32_t s_service_tick, s_client_tick;
static uint8_t s_udp, s_server, s_client_wait;

err_t eth_transport_init(EthNetwork_t *network, const EthTransportConfig_t *config)
{
    if (!eth_execution_allowed())
        return ERR_IF;

	err_t result;
	if (s_network)
		return ERR_USE;
	if (!network || !network->initialized || !config ||
		(config->udp_port && !config->udp_receive) ||
		(config->client_port && ip4_addr_isany(&config->client_ip)) ||
		(config->reconnect_ms && (config->reconnect_ms < 100U || config->reconnect_ms > 3600000U)))
		return ERR_ARG;
	result = eth_tcp_configure(config->tcp_event, config->arg, config->idle_ms);
	if (result != ERR_OK)
		return result;
	s_config = *config;
	s_network = network;
	return ERR_OK;
}

void eth_transport_poll(void)
{
    if (!eth_execution_allowed())
        return;

	struct netif *interface;
	uint32_t now;
	int ready;
	if (!s_network)
		return;
	interface = &s_network->interface;
	now = sys_now();
	ready = netif_is_up(interface) && netif_is_link_up(interface) &&
		!ip4_addr_isany(netif_ip4_addr(interface));
	if (s_stats.ready && (!ready || !ip4_addr_cmp(&s_address, netif_ip4_addr(interface))))
	{
		eth_udp_stop();
		eth_tcp_stop();
		s_udp = s_server = 0U;
		s_stats.ready = 0U;
		++s_stats.link_lost;
	}
	eth_tcp_poll();
	if (!ready)
		return;
	if (!s_stats.ready)
	{
		s_stats.ready = 1U;
		s_address = *netif_ip4_addr(interface);
		++s_stats.link_ready;
		s_service_tick = now - 1000U;
		s_client_wait = 0U;
	}
	if ((uint32_t)(now - s_service_tick) >= 1000U)
	{
		s_service_tick = now;
		if (s_config.udp_port && !s_udp)
		{
			s_stats.udp_start = eth_udp_start(interface, s_config.udp_port,
				s_config.udp_receive, s_config.arg);
			s_udp = (uint8_t)(s_stats.udp_start == ERR_OK);
		}
		if (s_config.listen_port && !s_server)
		{
			s_stats.server_start = eth_tcp_server_start(interface, s_config.listen_port);
			s_server = (uint8_t)(s_stats.server_start == ERR_OK);
		}
	}
	if (s_config.client_port && eth_tcp_get_state(eth_tcp_handle(ETH_TCP_CLIENT)) == ETH_TCP_IDLE)
	{
		if (!s_client_wait)
		{
			++s_stats.client_attempts;
			s_stats.client_start = eth_tcp_client_connect(interface, &s_config.client_ip,
				s_config.client_port);
			s_client_tick = now;
			s_client_wait = 1U;
		}
		else if (s_config.reconnect_ms && (uint32_t)(now - s_client_tick) >= s_config.reconnect_ms)
			s_client_wait = 0U;
	}
	else
		s_client_tick = now; //重连间隔从连接结束计算
}

void eth_transport_get_statistics(EthTransportStats_t *stats)
{
    if (!eth_execution_allowed())
        return;

	unsigned role;
	if (!stats)
		return;
	*stats = s_stats;
	stats->listening = (uint8_t)eth_tcp_listening();
	for (role = 0U; role < ETH_TCP_ROLES; ++role)
	{
		eth_tcp_get_statistics((EthTcpRole_t)role, &stats->tcp[role]);
		if (stats->tcp[role].state != ETH_TCP_IDLE)
			++stats->connections;
	}
	eth_udp_get_statistics(&stats->udp);
}

int eth_transport_ready(void)
{
    if (!eth_execution_allowed())
        return 0;

	return s_stats.ready != 0U;
}
