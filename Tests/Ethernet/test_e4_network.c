//真实 lwIP 和适配层，MAC 复制接口由确定性的主机桩替代
#include "eth_network.h"
#include "lwip/stats.h"
#include "lwip/timeouts.h"
#include "lwip/pbuf.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t s_mac[6] = {2, 0x47, 0x47, 0, 0, 1};
static const uint8_t s_peer[6] = {0xEC, 0x1A, 0xC3, 0x3E, 0xDB, 0xB2};
static const uint8_t s_board_ip[4] = {192, 168, 50, 2};
static const uint8_t s_peer_ip[4] = {192, 168, 50, 1};
static EthNetwork_t s_network;
static EthMacState_t s_state;
static EthResult_t s_negotiation;
static EthResult_t s_start_result;
static EthResult_t s_tx_result;
static EthResult_t s_reclaim_result;
static EthResult_t s_rx_result;
static uint8_t s_link;
static uint32_t s_tick;
static unsigned s_inflight;
static unsigned s_pending_rx;
static unsigned s_starts;
static unsigned s_timer_calls;
static unsigned s_tx_count;
static uint8_t s_in[1536];
static uint8_t s_out[1514];
static size_t s_in_length;
static size_t s_out_length;

void eth_lwip_assert(const char *message, const char *file, unsigned line)
{
	fprintf(stderr, "lwIP assertion: %s (%s:%u)\n", message, file, line);
	abort();
}

u32_t sys_now(void)
{
	return s_tick;
}

EthResult_t eth_mac_init(const uint8_t mac[6])
{
	assert(memcmp(mac, s_mac, 6U) == 0);
	s_state = ETH_MAC_READY;
	return ETH_OK;
}

EthResult_t eth_mac_get_address(uint8_t mac[6])
{
	memcpy(mac, s_mac, 6U);
	return ETH_OK;
}

EthResult_t eth_mac_get_statistics(EthStatistics_t *statistics)
{
	memset(statistics, 0, sizeof(*statistics));
	statistics->state = s_state;
	return ETH_OK;
}

EthResult_t eth_mac_get_link(EthLinkState_t *link)
{
	memset(link, 0, sizeof(*link));
	link->up = s_link;
	link->autoneg_complete = (uint8_t)(s_link && s_negotiation == ETH_OK);
	link->speed_mbps = s_link ? 100U : 0U;
	return s_negotiation;
}

EthResult_t eth_mac_start(void)
{
	assert(s_state == ETH_MAC_READY && s_link);
	++s_starts;
	if (s_start_result == ETH_OK)
		s_state = ETH_MAC_RUNNING;
	return s_start_result;
}

EthResult_t eth_mac_service(void)
{
	if (!s_link)
	{
		s_state = ETH_MAC_READY;
		s_inflight = 0U;
		return ETH_LINK_DOWN;
	}
	return ETH_OK;
}

EthResult_t eth_mac_tx_submit(const void *data, size_t length, uint32_t *ticket)
{
	if (s_tx_result != ETH_OK)
		return s_tx_result;
	if (s_inflight == 3U)
		return ETH_BUSY;
	assert(s_state == ETH_MAC_RUNNING && length <= sizeof(s_out));
	assert(((uintptr_t)data & 31U) == 0U);
	memcpy(s_out, data, length);
	s_out_length = length;
	++s_inflight;
	*ticket = ++s_tx_count;
	return ETH_OK;
}

EthResult_t eth_mac_tx_reclaim(uint32_t *ticket)
{
	EthResult_t result = s_reclaim_result;

	if (s_inflight == 0U)
		return ETH_EMPTY;
	--s_inflight;
	*ticket = s_tx_count - s_inflight;
	s_reclaim_result = ETH_OK;
	return result;
}

EthResult_t eth_mac_rx_poll(void *data, size_t capacity, size_t *length)
{
	EthResult_t result = s_rx_result;

	assert(s_state == ETH_MAC_RUNNING);
	assert(((uintptr_t)data & 31U) == 0U);
	if (result != ETH_OK)
	{
		s_rx_result = ETH_OK;
		return result;
	}
	if (s_pending_rx == 0U)
		return ETH_EMPTY;
	if (capacity < s_in_length)
		return ETH_CAPACITY;
	--s_pending_rx;
	memcpy(data, s_in, s_in_length);
	*length = s_in_length;
	return ETH_OK;
}

static void Put16(uint8_t *data, uint16_t value)
{
	data[0] = (uint8_t)(value >> 8);
	data[1] = (uint8_t)value;
}

static uint16_t Checksum(const uint8_t *data, size_t length)
{
	uint32_t sum = 0U;
	size_t i;

	for (i = 0U; i < length; i += 2U)
	{
		sum += (uint32_t)data[i] << 8;
		if (i + 1U < length)
			sum += data[i + 1U];
	}
	while (sum >> 16)
		sum = (sum & 0xFFFFU) + (sum >> 16);
	return (uint16_t)~sum;
}

static void BuildArp(void)
{
	memset(s_in, 0, sizeof(s_in));
	memset(s_in, 0xFF, 6U);
	memcpy(s_in + 6U, s_peer, 6U);
	Put16(s_in + 12U, 0x0806U);
	Put16(s_in + 14U, 1U);
	Put16(s_in + 16U, 0x0800U);
	s_in[18] = 6U;
	s_in[19] = 4U;
	Put16(s_in + 20U, 1U);
	memcpy(s_in + 22U, s_peer, 6U);
	memcpy(s_in + 28U, s_peer_ip, 4U);
	memcpy(s_in + 38U, s_board_ip, 4U);
	s_in_length = 60U;
}

static void BuildEcho(size_t frame_size)
{
	size_t i;

	memset(s_in, 0, sizeof(s_in));
	memcpy(s_in, s_mac, 6U);
	memcpy(s_in + 6U, s_peer, 6U);
	Put16(s_in + 12U, 0x0800U);
	s_in[14] = 0x45U;
	Put16(s_in + 16U, (uint16_t)(frame_size - 14U));
	s_in[22] = 64U;
	s_in[23] = 1U;
	memcpy(s_in + 26U, s_peer_ip, 4U);
	memcpy(s_in + 30U, s_board_ip, 4U);
	Put16(s_in + 24U, Checksum(s_in + 14U, 20U));
	s_in[34] = 8U;
	Put16(s_in + 38U, 0x1234U);
	Put16(s_in + 40U, 7U);
	for (i = 42U; i < frame_size; ++i)
		s_in[i] = (uint8_t)i;
	Put16(s_in + 36U, Checksum(s_in + 34U, frame_size - 34U));
	s_in_length = frame_size;
}

static void CheckEcho(size_t frame_size)
{
	BuildEcho(frame_size);
	s_pending_rx = 1U;
	eth_network_poll(&s_network);
	assert(s_out_length == frame_size && s_out[34] == 0U);
	assert(memcmp(s_out, s_peer, 6U) == 0 && memcmp(s_out + 6U, s_mac, 6U) == 0);
	assert(memcmp(s_out + 26U, s_board_ip, 4U) == 0);
	assert(memcmp(s_out + 30U, s_peer_ip, 4U) == 0);
	assert(memcmp(s_out + 38U, s_in + 38U, frame_size - 38U) == 0);
	assert(Checksum(s_out + 14U, 20U) == 0U);
	assert(Checksum(s_out + 34U, frame_size - 34U) == 0U);
}

static err_t RejectRx(struct pbuf *packet, struct netif *netif)
{
	(void)netif;
	assert(packet->next != NULL && packet->tot_len == 1514U);
	return ERR_IF;
}

static void Timer(void *context)
{
	assert(context == &s_network);
	++s_timer_calls;
}

int main(void)
{
	ip4_addr_t ip;
	ip4_addr_t mask;
	ip4_addr_t gateway;
	struct netif *interface = &s_network.interface;
	EthNetifStatistics_t *stats = &s_network.port.statistics;
	struct pbuf *packet;
	struct pbuf *held[PBUF_POOL_SIZE];
	netif_input_fn input;
	unsigned i;
	unsigned count;
	uint32_t rx_rejected;
	uint32_t tx_errors;
	uint32_t completed;

	(void)setvbuf(stdout, NULL, _IONBF, 0);

	IP4_ADDR(&ip, 192, 168, 50, 2);
	IP4_ADDR(&mask, 255, 255, 255, 0);
	IP4_ADDR(&gateway, 0, 0, 0, 0);
	assert(eth_network_init(&s_network, s_mac, &ip, &mask, &gateway) == ERR_OK);
	assert(netif_is_up(interface) && !netif_is_link_up(interface));
	assert(interface->mtu == 1500U && interface->hwaddr_len == 6U &&
		memcmp(interface->hwaddr, s_mac, 6U) == 0);
	eth_network_poll(&s_network);
	assert(s_starts == 0U && s_network.last_driver_result == ETH_LINK_DOWN);
	s_link = 1U;
	s_negotiation = ETH_BUSY;
	s_tick += 10U;
	eth_network_poll(&s_network);
	assert(s_starts == 0U && !netif_is_link_up(interface));
	s_negotiation = ETH_OK;
	s_start_result = ETH_IO;
	s_tick += 10U;
	eth_network_poll(&s_network);
	assert(s_starts == 1U && !netif_is_link_up(interface));
	s_start_result = ETH_OK;
	s_tick += 10U;
	eth_network_poll(&s_network);
	assert(netif_is_link_up(interface) && s_network.link_ups == 1U);
	sys_timeout(5U, Timer, &s_network);
	s_tick += 4U;
	eth_network_poll(&s_network);
	assert(s_timer_calls == 0U);
	++s_tick;
	eth_network_poll(&s_network);
	assert(s_timer_calls == 1U);

	BuildArp();
	s_pending_rx = 1U;
	count = s_tx_count;
	eth_network_poll(&s_network);
	assert(s_tx_count == count + 1U && s_out_length == 42U && s_out[21] == 2U);
	assert(memcmp(s_out, s_peer, 6U) == 0 && memcmp(s_out + 22U, s_mac, 6U) == 0);
	assert(memcmp(s_out + 28U, s_board_ip, 4U) == 0 &&
		memcmp(s_out + 38U, s_peer_ip, 4U) == 0);
	CheckEcho(61U);
	CheckEcho(1514U);
	eth_network_poll(&s_network);
	assert(lwip_stats.memp[MEMP_PBUF_POOL]->used == 0U);
	puts("PASS: real lwIP static netif, ARP, ICMP (odd and max MTU), NO_SYS timer");

	for (i = 0U; i < sizeof(s_in); ++i)
		s_in[i] = (uint8_t)i;
	packet = pbuf_alloc(PBUF_RAW, 1514U, PBUF_POOL);
	assert(packet && packet->next && pbuf_take(packet, s_in, 1514U) == ERR_OK);
	for (i = 0U; i < 3U; ++i)
	{
		assert(interface->linkoutput(interface, packet) == ERR_OK);
		assert(memcmp(s_out, s_in, 1514U) == 0 && packet->ref == 1U);
	}
	assert(interface->linkoutput(interface, packet) == ERR_MEM && stats->tx_busy == 1U);
	completed = stats->tx_completed;
	tx_errors = stats->tx_errors;
	s_reclaim_result = ETH_FRAME_ERROR;
	eth_network_poll(&s_network);
	assert(stats->tx_completed == completed + 2U && stats->tx_errors == tx_errors + 1U);
	assert(interface->linkoutput(interface, packet) == ERR_OK);
	s_tx_result = ETH_IO;
	assert(interface->linkoutput(interface, packet) == ERR_IF && packet->ref == 1U);
	s_tx_result = ETH_LINK_DOWN;
	assert(interface->linkoutput(interface, packet) == ERR_RTE && packet->ref == 1U);
	s_tx_result = ETH_OK;
	assert(pbuf_free(packet) == 3U);
	packet = pbuf_alloc(PBUF_RAW, 1515U, PBUF_POOL);
	assert(packet && interface->linkoutput(interface, packet) == ERR_BUF);
	pbuf_free(packet);
	assert(stats->tx_rejected == 1U);

	for (i = 0U; i < PBUF_POOL_SIZE; ++i)
	{
		held[i] = pbuf_alloc(PBUF_RAW, 60U, PBUF_POOL);
		assert(held[i]);
	}
	BuildArp();
	s_pending_rx = 1U;
	count = s_tx_count;
	eth_network_poll(&s_network);
	assert(stats->rx_no_pbuf == 1U && s_tx_count == count);
	for (i = 0U; i < PBUF_POOL_SIZE; ++i)
		assert(pbuf_free(held[i]) == 1U);
	input = interface->input;
	interface->input = RejectRx;
	rx_rejected = stats->rx_rejected;
	BuildEcho(1514U);
	s_pending_rx = ETH_NETIF_BUDGET + 1U;
	s_rx_result = ETH_FRAME_ERROR;
	eth_network_poll(&s_network);
	assert(s_pending_rx == 2U && stats->rx_errors == 1U);
	eth_network_poll(&s_network);
	assert(stats->rx_rejected == rx_rejected + ETH_NETIF_BUDGET + 1U);
	s_in_length = 1522U;
	s_pending_rx = 1U;
	eth_network_poll(&s_network);
	assert(s_pending_rx == 0U && stats->rx_rejected == rx_rejected + ETH_NETIF_BUDGET + 2U);
	assert(lwip_stats.memp[MEMP_PBUF_POOL]->used == 0U);
	interface->input = input;
	puts("PASS: chained TX, frame boundaries, ring BUSY/reclaim, input rejection, pool exhaustion, RX budget");

	s_link = 0U;
	s_tick += 10U;
	eth_network_poll(&s_network);
	assert(netif_is_up(interface) && !netif_is_link_up(interface) && s_network.link_downs == 1U);
	packet = pbuf_alloc(PBUF_RAW, 60U, PBUF_POOL);
	assert(packet && interface->linkoutput(interface, packet) == ERR_RTE && packet->ref == 1U);
	pbuf_free(packet);
	s_link = 1U;
	s_tick += 10U;
	eth_network_poll(&s_network);
	assert(netif_is_link_up(interface) && s_network.link_ups == 2U);
	BuildArp();
	s_pending_rx = 1U;
	eth_network_poll(&s_network);
	CheckEcho(61U);
	assert(lwip_stats.memp[MEMP_PBUF_POOL]->used == 0U);
	puts("PASS: netif stays administratively up, renegotiation and IPv4 recovery");
	return 0;
}
