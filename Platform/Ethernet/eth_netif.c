#include "eth_execution.h"
//pbuf 和 DMA 内存的所有权始终独立
#include "eth_netif.h"
#include "lwip/etharp.h"
#include "lwip/prot/ethernet.h"

typedef char eth_header_size_check[(sizeof(struct eth_hdr) == 14U) ? 1 : -1];

static err_t EthNetif_Output(struct netif *netif, struct pbuf *packet)
{
    if (!eth_execution_allowed())
        return ERR_IF;
	EthNetif_t *context = netif->state;
	EthResult_t result;
	uint32_t ticket;
	u16_t length;

	if (!netif_is_up(netif) || !netif_is_link_up(netif))
		return ERR_RTE;
	if (packet == NULL || packet->tot_len < 14U || packet->tot_len > ETH_NETIF_FRAME_MAX)
	{
		++context->statistics.tx_rejected;
		return ERR_BUF;
	}
	length = packet->tot_len;
	if (pbuf_copy_partial(packet, context->tx_frame, length, 0U) != length)
	{
		++context->statistics.tx_rejected;
		return ERR_BUF;
	}
	//提交时驱动同步复制，适配层不持有 pbuf，也不等待完成
	result = eth_mac_tx_submit(context->tx_frame, length, &ticket);
	if (result == ETH_OK)
	{
		++context->statistics.tx_accepted;
		return ERR_OK;
	}
	if (result == ETH_BUSY)
	{
		++context->statistics.tx_busy;
		return ERR_MEM;
	}
	++context->statistics.tx_errors;
	return result == ETH_LINK_DOWN ? ERR_RTE : ERR_IF;
}

err_t eth_netif_init(struct netif *netif)
{
    if (!eth_execution_allowed())
        return ERR_IF;

	if (netif == NULL || netif->state == NULL)
		return ERR_ARG;
	if (eth_mac_get_address(netif->hwaddr) != ETH_OK)
		return ERR_IF;
	netif->name[0] = 'e';
	netif->name[1] = 'n';
	netif->hwaddr_len = 6U;
	netif->mtu = ETH_NETIF_MTU;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
	netif->output = etharp_output;
	netif->linkoutput = EthNetif_Output;
	return ERR_OK;
}

void eth_netif_poll(struct netif *netif)
{
    if (!eth_execution_allowed())
        return;

	EthNetif_t *context = netif->state;
	unsigned index;

	if (!netif_is_up(netif) || !netif_is_link_up(netif))
		return;
	//先回收上一轮 TX，为本轮的 ARP/ICMP 应答腾出槽位
	for (index = 0U; index < ETH_NETIF_BUDGET; ++index)
	{
		uint32_t ticket;
		EthResult_t result = eth_mac_tx_reclaim(&ticket);

		if (result == ETH_OK)
			++context->statistics.tx_completed;
		else if (result == ETH_EMPTY)
			break;
		else
		{
			++context->statistics.tx_errors;
			if (result != ETH_FRAME_ERROR)
				break;
		}
	}
	for (index = 0U; index < ETH_NETIF_BUDGET; ++index)
	{
		struct pbuf *packet;
		size_t length;
		EthResult_t result = eth_mac_rx_poll(context->rx_frame, sizeof(context->rx_frame), &length);

		if (result == ETH_EMPTY)
			break;
		if (result != ETH_OK)
		{
			++context->statistics.rx_errors;
			if (result != ETH_FRAME_ERROR)
				break;
			continue;
		}
		if (length < 14U || length > ETH_NETIF_FRAME_MAX)
		{
			++context->statistics.rx_rejected;
			continue;
		}
		packet = pbuf_alloc(PBUF_RAW, (u16_t)length, PBUF_POOL);
		if (packet == NULL)
		{
			++context->statistics.rx_no_pbuf;
			continue;
		}
		if (pbuf_take(packet, context->rx_frame, (u16_t)length) != ERR_OK ||
			netif->input(packet, netif) != ERR_OK)
		{
			//输入拒绝时所有权仍由适配层持有
			pbuf_free(packet);
			++context->statistics.rx_rejected;
		}
		else
			++context->statistics.rx_delivered;
	}
}
