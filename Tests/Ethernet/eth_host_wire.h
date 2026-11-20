#ifndef ETH_HOST_WIRE_H
#define ETH_HOST_WIRE_H

//复用不改动的 E4 模拟 MAC/时钟，截取实际 lwIP 输出的线帧
#define main E4FixtureMain
#define eth_mac_tx_submit E4FixtureSubmit
#include "test_e4_network.c"
#undef eth_mac_tx_submit
#undef main

#include "lwip/tcp.h"
#include "eth_transport.h"

#ifndef ETH_HOST_STEP_MS
#define ETH_HOST_STEP_MS 20U
#endif

enum { FIN = 1, SYN = 2, RST = 4, PSH = 8, ACK = 16 };
typedef struct
{
	uint16_t source, dest, length, window;
	uint32_t seq, ack;
	uint8_t flags, protocol;
	uint8_t data[1472];
} Wire_t;
typedef struct { uint16_t board_port, pc_port; uint32_t pc_seq, board_ack; } Peer_t;

static Wire_t s_wire[128];
static unsigned s_wire_count;
static uint8_t s_checksum[1536];

static uint16_t Get16(const uint8_t *data)
{
	return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t Get32(const uint8_t *data)
{
	return ((uint32_t)Get16(data) << 16) | Get16(data + 2U);
}

static void Put32(uint8_t *data, uint32_t value)
{
	Put16(data, (uint16_t)(value >> 16));
	Put16(data + 2U, (uint16_t)value);
}

EthResult_t eth_mac_tx_submit(const void *data, size_t length, uint32_t *ticket)
{
	EthResult_t result = E4FixtureSubmit(data, length, ticket);
	const uint8_t *p = data;
	Wire_t *w;
	uint16_t bytes;
	if (result != ETH_OK || length < 42U || p[12] != 8U || p[13] != 0U ||
		(p[23] != 6U && p[23] != 17U) || p[14] != 0x45U)
		return result;
	w = &s_wire[s_wire_count++ % 128U];
	memset(w, 0, sizeof(*w));
	w->protocol = p[23];
	w->source = Get16(p + 34U);
	w->dest = Get16(p + 36U);
	if (w->protocol == 6U)
	{
		uint16_t header = (uint16_t)((p[46] >> 4) * 4U);
		bytes = (uint16_t)(Get16(p + 16U) - 20U - header);
		w->seq = Get32(p + 38U);
		w->ack = Get32(p + 42U);
		w->flags = p[47];
		w->window = Get16(p + 48U);
		assert(bytes <= sizeof(w->data) && (size_t)(34U + header + bytes) <= length);
		memcpy(w->data, p + 34U + header, bytes);
	}
	else
	{
		bytes = (uint16_t)(Get16(p + 38U) - 8U);
		assert(bytes <= sizeof(w->data) && (size_t)(42U + bytes) <= length);
		memcpy(w->data, p + 42U, bytes);
	}
	w->length = bytes;
	return result;
}

static void Pump(uint32_t ms)
{
	do
	{
		uint32_t step = ms > ETH_HOST_STEP_MS ? ETH_HOST_STEP_MS : ms;
		s_tick += step;
		ms -= step;
		eth_network_poll(&s_network);
		eth_transport_poll();
#ifdef ETH_HOST_APP_POLL
		ETH_HOST_APP_POLL();
#endif
	} while (ms != 0U);
}

static void Inbound(uint8_t protocol, uint16_t transport_length, int broadcast)
{
	memset(s_in, 0, 34U);
	if (broadcast)
		memset(s_in, 0xFF, 6U);
	else
		memcpy(s_in, s_mac, 6U);
	memcpy(s_in + 6U, s_peer, 6U);
	Put16(s_in + 12U, 0x0800U);
	s_in[14] = 0x45U;
	s_in[22] = 64U;
	s_in[23] = protocol;
	Put16(s_in + 16U, (uint16_t)(20U + transport_length));
	memcpy(s_in + 26U, s_peer_ip, 4U);
	memcpy(s_in + 30U, broadcast ? s_broadcast : s_board_ip, 4U);
	Put16(s_in + 24U, Checksum(s_in + 14U, 20U));
	s_in_length = 34U + transport_length;
	s_pending_rx = 1U;
	Pump(ETH_HOST_STEP_MS);
}

static void Tcp(Peer_t *peer, uint8_t flags, uint16_t window, const void *data, uint16_t bytes)
{
	uint8_t *p = s_in + 34U;
	uint16_t header = (flags & SYN) ? 24U : 20U;
	uint16_t length = (uint16_t)(header + bytes);
	memset(p, 0, header);
	Put16(p, peer->pc_port);
	Put16(p + 2U, peer->board_port);
	Put32(p + 4U, peer->pc_seq);
	Put32(p + 8U, peer->board_ack);
	p[12] = (uint8_t)((header / 4U) << 4);
	p[13] = flags;
	Put16(p + 14U, window);
	if (flags & SYN)
	{
		p[20] = 2U;
		p[21] = 4U;
		Put16(p + 22U, 1460U);
	}
	if (bytes)
		memcpy(p + header, data, bytes);
	memcpy(s_checksum, s_peer_ip, 4U);
	memcpy(s_checksum + 4U, s_board_ip, 4U);
	s_checksum[8] = 0U;
	s_checksum[9] = 6U;
	Put16(s_checksum + 10U, length);
	memcpy(s_checksum + 12U, p, length);
	Put16(p + 16U, Checksum(s_checksum, 12U + length));
	Inbound(6U, length, 0);
	peer->pc_seq += bytes + (uint32_t)((flags & (SYN | FIN)) != 0U);
}

static const Wire_t *Last(unsigned from, uint16_t dest, uint8_t flags, int with_data)
{
	unsigned i = s_wire_count;
	assert(i - from <= 128U);
	while (i > from)
	{
		const Wire_t *w = &s_wire[--i % 128U];
		if (w->dest == dest && (w->flags & flags) == flags && (!with_data || w->length))
			return w;
	}
	return NULL;
}

static Peer_t WireSyn(uint16_t board_port, uint16_t port, uint32_t seq)
{
	Peer_t p = {board_port, port, seq, 0U};
	unsigned n = s_wire_count;
	const Wire_t *synack;
	Tcp(&p, SYN, 4096U, NULL, 0U);
	synack = Last(n, port, SYN | ACK, 0);
	if (!synack)
		fprintf(stderr, "no SYN-ACK for local port %u, peer port %u, listener=%d, MAC=%d\n",
			(unsigned)board_port, (unsigned)port, eth_tcp_listening(), (int)s_state);
	assert(synack && synack->ack == p.pc_seq && synack->source == board_port);
	p.board_ack = synack->seq + 1U;
	return p;
}

#endif
