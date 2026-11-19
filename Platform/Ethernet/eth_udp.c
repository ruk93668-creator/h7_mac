#include "eth_udp.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"

static struct udp_pcb *s_udp;
static struct netif *s_interface;
static EthUdpReceive_t s_receive;
static void *s_arg;
static EthUdpStats_t s_stats;
static uint8_t s_receive_data[ETH_UDP_MAX];

static void EthUdp_Receive(void *arg, struct udp_pcb *pcb, struct pbuf *packet,
	const ip_addr_t *ip, u16_t port)
{
	err_t result = ERR_VAL;
	uint16_t length = packet->tot_len;
	(void)arg;
	(void)pcb;

	if (!port || ip_addr_isany(ip) || ip_addr_ismulticast(ip))
	{
		++s_stats.peer_errors;
	}
	else if (length && length <= ETH_UDP_MAX &&
		pbuf_copy_partial(packet, s_receive_data, length, 0) == length)
	{
		result = s_receive(ip_2_ip4(ip), port, s_receive_data, length, s_arg);
		if (result == ERR_OK)
		{
			++s_stats.rx_packets;
			s_stats.rx_bytes += length;
		}
		else if (result == ERR_MEM)
			++s_stats.rx_no_memory;
		else
			++s_stats.rx_invalid;
	}
	else
		++s_stats.rx_invalid;
	if (result != ERR_OK)
		++s_stats.rx_errors;
	pbuf_free(packet);
}

err_t eth_udp_start(struct netif *netif, uint16_t port, EthUdpReceive_t receive, void *arg)
{
	err_t result;
	if (!netif || !port || !receive)
		return ERR_ARG;
	if (s_udp)
		return ERR_USE;
	s_udp = udp_new_ip_type(IPADDR_TYPE_V4);
	if (!s_udp)
		return ERR_MEM;
	result = udp_bind(s_udp, IP_ADDR_ANY, port);
	if (result != ERR_OK)
	{
		udp_remove(s_udp);
		s_udp = NULL;
		return result;
	}
	s_interface = netif;
	s_receive = receive;
	s_arg = arg;
	ip_set_option(s_udp, SOF_BROADCAST);
	udp_bind_netif(s_udp, netif);
	udp_recv(s_udp, EthUdp_Receive, NULL);
	return ERR_OK;
}

void eth_udp_stop(void)
{
	if (s_udp)
	{
		udp_remove(s_udp);
		s_udp = NULL;
	}
}

err_t eth_udp_send(const ip4_addr_t *ip, uint16_t port, const void *data, uint16_t length)
{
	struct pbuf *packet;
	err_t result;
	if (!ip || ip4_addr_isany(ip) || !port || !data || !length || length > ETH_UDP_MAX)
	{
		++s_stats.tx_errors;
		return ERR_ARG;
	}
	if (!s_udp || !netif_is_link_up(s_interface) || !netif_is_up(s_interface) ||
		ip4_addr_isany(netif_ip4_addr(s_interface)))
	{
		++s_stats.tx_errors;
		return ERR_RTE;
	}
	packet = pbuf_alloc(PBUF_TRANSPORT, length, PBUF_RAM);
	if (!packet)
		result = ERR_MEM;
	else
	{
		result = pbuf_take(packet, data, length);
		if (result == ERR_OK)
			result = udp_sendto_if(s_udp, packet, ip, port, s_interface);
		pbuf_free(packet);
	}
	if (result == ERR_OK)
	{
		++s_stats.tx_packets;
		s_stats.tx_bytes += length;
	}
	else
	{
		++s_stats.tx_errors;
		if (result == ERR_MEM)
			++s_stats.tx_no_memory;
	}
	return result;
}

void eth_udp_get_statistics(EthUdpStats_t *stats)
{
	*stats = s_stats;
}
