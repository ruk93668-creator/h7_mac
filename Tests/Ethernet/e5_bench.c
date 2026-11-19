#include "e5_bench.h"
#include "e5_protocol.h"
#include <string.h>

typedef struct
{
	uint8_t frame[E5_LIMIT];
	uint32_t generation, last_tick, sequence;
	uint32_t last_request, last_reply;
	uint64_t seen_requests, seen_replies;
	uint16_t have, want;
	uint8_t counted;
	E5ProtocolStats_t stats;
} E5Channel_t;

static E5Channel_t s_channels[E5_CHANNELS];
static uint8_t s_send[E5_LIMIT];
static ip4_addr_t s_peer;

static void E5Receive(E5Channel_t *channel, const uint8_t *frame)
{
	uint32_t seq = e5_frame_sequence(frame);
	uint32_t *last;
	uint64_t *seen;
	if (frame[5] == E5_REQUEST)
	{
		++channel->stats.request_rx;
		last = &channel->last_request;
		seen = &channel->seen_requests;
	}
	else
	{
		++channel->stats.reply_rx;
		last = &channel->last_reply;
		seen = &channel->seen_replies;
	}
	if (seq <= *last)
	{
		uint32_t delta = *last - seq;
		if (delta < 64U && (*seen & (UINT64_C(1) << delta)))
			++channel->stats.duplicates;
		else
		{
			++channel->stats.reordered;
			if (delta < 64U)
			{
				*seen |= UINT64_C(1) << delta;
				if (seq && channel->stats.missing)
					--channel->stats.missing;
			}
		}
	}
	else
	{
		uint32_t delta = seq - *last;
		channel->stats.missing += delta - 1U;
		*seen = (delta < 64U ? *seen << delta : 0U) | UINT64_C(1);
		*last = seq;
	}
}

static err_t E5UdpReceive(const ip4_addr_t *ip, uint16_t port, const uint8_t *data,
	uint16_t length, void *arg)
{
	E5Channel_t *channel = &s_channels[E5_UDP];
	err_t result;
	(void)arg;
	if (!e5_frame_valid(data, length))
	{
		++channel->stats.invalid;
		return ERR_VAL;
	}
	E5Receive(channel, data);
	if (data[5] == E5_REPLY)
		return ERR_OK;
	memcpy(channel->frame, data, length);
	channel->frame[5] = E5_REPLY;
	result = eth_udp_send(ip, port, channel->frame, length);
	if (result == ERR_OK)
		++channel->stats.reply_tx;
	else
		++channel->stats.blocked;
	return result;
}

err_t e5_bench_init(EthNetwork_t *net, const ip4_addr_t *peer, uint16_t client_port,
	uint32_t reconnect_ms)
{
	EthTransportConfig_t config = {0};
	if (!peer || ip4_addr_isany(peer))
		return ERR_ARG;
	config.udp_port = 5001U;
	config.listen_port = 5003U;
	config.client_port = client_port;
	config.client_ip = *peer;
	config.reconnect_ms = reconnect_ms;
	config.idle_ms = 30000U;
	config.udp_receive = E5UdpReceive;
	s_peer = *peer;
	return eth_transport_init(net, &config);
}

static void E5TcpReceive(E5Channel_t *channel, EthTcpHandle_t handle)
{
	unsigned budget;
	for (budget = 0U; budget < 4U; ++budget)
	{
		if (channel->have < E5_HEADER)
		{
			channel->have += eth_tcp_read(handle, channel->frame + channel->have,
				(uint16_t)(E5_HEADER - channel->have));
			if (channel->have != E5_HEADER)
				break;
			channel->want = e5_frame_length(channel->frame);
			if (!channel->want)
			{
				++channel->stats.invalid;
				eth_tcp_abort(handle);
				break;
			}
		}
		channel->have += eth_tcp_read(handle, channel->frame + channel->have,
			(uint16_t)(channel->want - channel->have));
		if (channel->have != channel->want)
			break;
		if (!e5_frame_valid(channel->frame, channel->want))
		{
			++channel->stats.invalid;
			eth_tcp_abort(handle);
			break;
		}
		if (!channel->counted)
		{
			E5Receive(channel, channel->frame);
			channel->counted = 1U;
		}
		if (channel->frame[5] == E5_REQUEST)
		{
			channel->frame[5] = E5_REPLY;
			if (eth_tcp_send(handle, channel->frame, channel->want) != ERR_OK)
			{
				channel->frame[5] = E5_REQUEST;
				++channel->stats.blocked;
				break;
			}
			++channel->stats.reply_tx;
		}
		channel->have = 0U;
		channel->want = 0U;
		channel->counted = 0U;
	}
}

void e5_bench_poll(uint32_t now, uint32_t interval)
{
	static const uint16_t sizes[] = {1U, 16U, 64U, 256U, 512U, 1200U, 1456U};
	unsigned index;
	for (index = 0U; index < E5_CHANNELS; ++index)
	{
		E5Channel_t *channel = &s_channels[index];
		EthTcpHandle_t handle = eth_tcp_handle((EthTcpRole_t)index);
		uint16_t length;
		err_t result;
		if (index != E5_UDP)
		{
			if (eth_tcp_get_state(handle) != ETH_TCP_OPEN)
				continue;
			if (channel->generation != handle.generation)
			{
				channel->generation = handle.generation;
				channel->have = channel->want = channel->counted = 0U;
				channel->sequence = channel->last_request = channel->last_reply = 0U;
				channel->seen_requests = channel->seen_replies = 0U;
			}
			E5TcpReceive(channel, handle);
		}
		if (!interval || (uint32_t)(now - channel->last_tick) < interval)
			continue;
		channel->last_tick = now;
		length = e5_frame_build(s_send, E5_REQUEST, channel->sequence + 1U,
			sizes[channel->sequence % (sizeof(sizes) / sizeof(sizes[0]))]);
		result = index == E5_UDP ? eth_udp_send(&s_peer, 5002U, s_send, length) :
			eth_tcp_send(handle, s_send, length);
		if (result == ERR_OK)
		{
			++channel->sequence;
			++channel->stats.request_tx;
		}
		else
			++channel->stats.blocked;
	}
}

void e5_bench_statistics(E5ProtocolStats_t stats[E5_CHANNELS])
{
	unsigned index;
	for (index = 0U; index < E5_CHANNELS; ++index)
		stats[index] = s_channels[index].stats;
}
