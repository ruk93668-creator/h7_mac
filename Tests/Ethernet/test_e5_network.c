//复用不改动的 E4 模拟 MAC/时钟，截取实际 lwIP 输出的线帧
#define main E4FixtureMain
#define eth_mac_tx_submit E4FixtureSubmit
#include "test_e4_network.c"
#undef eth_mac_tx_submit
#undef main

#include "e5_bench.h"
#include "e5_protocol.h"
#include "lwip/mem.h"
#include "lwip/tcp.h"
#include "lwip/priv/tcp_priv.h"
#include "netif/ppp/ppp_opts.h"

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
static uint8_t s_payload[ETH_TCP_QUEUE_BYTES];

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
		uint32_t step = ms > 20U ? 20U : ms;
		s_tick += step;
		ms -= step;
		eth_network_poll(&s_network);
		eth_transport_poll();
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
	Pump(20U);
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

static void Udp(const void *data, uint16_t bytes, int broadcast)
{
	uint8_t *p = s_in + 34U;
	Put16(p, 5002U);
	Put16(p + 2U, 5001U);
	Put16(p + 4U, (uint16_t)(8U + bytes));
	Put16(p + 6U, 0U);
	memcpy(p + 8U, data, bytes);
	Inbound(17U, (uint16_t)(8U + bytes), broadcast);
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

static Peer_t Syn(uint16_t port, uint32_t seq)
{
	Peer_t p = {5003U, port, seq, 0U};
	unsigned n = s_wire_count;
	const Wire_t *synack;
	Tcp(&p, SYN, 4096U, NULL, 0U);
	synack = Last(n, port, SYN | ACK, 0);
	assert(synack && synack->ack == p.pc_seq && synack->source == 5003U);
	p.board_ack = synack->seq + 1U;
	return p;
}

static Peer_t ConnectOutbound(unsigned start_wire)
{
	const Wire_t *syn = Last(start_wire, 5004U, SYN, 0);
	Peer_t p;
	assert(syn && !(syn->flags & ACK));
	p.board_port = syn->source;
	p.pc_port = 5004U;
	p.pc_seq = 20000U;
	p.board_ack = syn->seq + 1U;
	start_wire = s_wire_count;
	Tcp(&p, SYN | ACK, 4096U, NULL, 0U);
	assert(Last(start_wire, 5004U, ACK, 0));
	assert(eth_tcp_get_state(eth_tcp_handle(ETH_TCP_CLIENT)) == ETH_TCP_OPEN);
	return p;
}

static void ShortEcho(Peer_t *peer)
{
	unsigned mark;
	uint8_t frames[160];
	uint16_t a = e5_frame_build(frames, E5_REQUEST, 1U, 32U);
	uint16_t b = e5_frame_build(frames + a, E5_REQUEST, 2U, 16U);
	const Wire_t *w;
	Tcp(peer, PSH | ACK, 4096U, frames, 5U);
	e5_bench_poll(s_tick, 0U);
	mark = s_wire_count;
	Tcp(peer, PSH | ACK, 4096U, frames + 5U, (uint16_t)(a + b - 5U));
	e5_bench_poll(s_tick, 0U);
	Pump(60U);
	w = Last(mark, peer->pc_port, ACK, 1);
	assert(w && w->length == a + b && w->data[5] == E5_REPLY && w->data[a + 5U] == E5_REPLY);
	assert(e5_frame_valid(w->data, a) && e5_frame_valid(w->data + a, b));
	assert(memcmp(w->data + E5_HEADER, frames + E5_HEADER, a - E5_HEADER) == 0);
	peer->board_ack = w->seq + w->length;
	Tcp(peer, ACK, 4096U, NULL, 0U);
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	EthNetworkAddressConfig_t config = {.mode = ETH_ADDRESS_DHCP};
	ip4_addr_t ip;
	ip4_addr_t broadcast;
	EthTransportStats_t stats;
	EthTcpHandle_t server_handle;
	Peer_t passive, active, rejected;
	const Wire_t *w;
	void *segments[MEMP_NUM_TCP_SEG];
	void *heap[64];
	struct pbuf *held[PBUF_POOL_SIZE];
	struct tcp_pcb *pcbs[MEMP_NUM_TCP_PCB];
	void *timers[MEMP_NUM_SYS_TIMEOUT];
	unsigned n, i, count, mark;
	uint32_t seq;
	uint16_t len;

	config.timeout_ms = 30000U;
	assert(eth_network_init_address(&s_network, s_mac, &config) == ERR_OK);
	s_link = 1U;
	Advance(100U);
	Bind(); //真实 DHCP DORA 和 ACD，确认 DHCP UDP PCB 持有
	IP4_ADDR(&ip, 192, 168, 50, 1);
	IP4_ADDR(&broadcast, 255, 255, 255, 255);
	assert(e5_bench_init(&s_network, &ip, 5004U, 2000U) == ERR_OK);
	assert(e5_bench_init(&s_network, &ip, 5004U, 2000U) == ERR_USE);
	mark = s_wire_count;
	Pump(20U);
	active = ConnectOutbound(mark);
	passive = Syn(51000U, 30000U);
	Tcp(&passive, ACK, 4096U, NULL, 0U);
	server_handle = eth_tcp_handle(ETH_TCP_SERVER);
	assert(eth_tcp_get_state(server_handle) == ETH_TCP_OPEN);
	assert(eth_tcp_client_connect(&s_network.interface, &ip, 5004U) == ERR_ISCONN);
	rejected = Syn(51001U, 31000U);
	mark = s_wire_count;
	Tcp(&rejected, ACK, 4096U, NULL, 0U);
	assert(Last(mark, 51001U, RST, 0));
	assert(eth_tcp_get_state(server_handle) == ETH_TCP_OPEN);
	ShortEcho(&passive);
	ShortEcho(&active);
	eth_transport_get_statistics(&stats);
	assert(stats.connections == 2U && stats.listening && stats.tcp[0].acked &&
		stats.tcp[1].acked && stats.tcp[0].rejected == 1U);
	assert(lwip_stats.memp[MEMP_UDP_PCB]->used == 2U && udp_new() == NULL);

	//双连接活动期间，真实 UDP 双向、发送边界与广播。
	{
		static const uint16_t lengths[] = {1, 16, 64, 256, 512, 1200, 1472};
		memset(s_payload, 0x5A, sizeof(s_payload));
		for (i = 0U; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
		{
			mark = s_wire_count;
			assert(eth_udp_send(&ip, 5002U, s_payload, lengths[i]) == ERR_OK);
			w = Last(mark, 5002U, 0U, 1);
			assert(w && w->protocol == 17U && w->length == lengths[i]);
			assert(memcmp(w->data, s_payload, lengths[i]) == 0);
			Pump(20U);
		}
	}
	assert(eth_udp_send(&ip, 5002U, s_payload, 1473U) == ERR_ARG);
	assert(eth_udp_send(&broadcast, 5002U, s_payload, 16U) == ERR_OK);
	Pump(20U);
	len = e5_frame_build(s_payload, E5_REQUEST, 1U, E5_LIMIT - E5_HEADER);
	mark = s_wire_count;
	Udp(s_payload, len, 0);
	w = Last(mark, 5002U, 0U, 1);
	assert(w && w->length == E5_LIMIT && w->data[5] == E5_REPLY && e5_frame_valid(w->data, w->length));
	len = e5_frame_build(s_payload, E5_REQUEST, 2U, 64U);
	mark = s_wire_count;
	Udp(s_payload, len, 1);
	assert(Last(mark, 5002U, 0U, 1));
	puts("PASS: DHCP + 2 TCP roles + UDP; SYN/SYN-ACK/ACK, second accept rejected, fragmented/coalesced echo, MTU/UDP broadcast");

	//接收大帧跨 pbuf，应用暂不读则窗口归零；读后重新通告窗口。
	memset(s_payload, 0xA7, sizeof(s_payload));
	Tcp(&passive, PSH | ACK, 4096U, s_payload, 1460U);
	Tcp(&passive, PSH | ACK, 4096U, s_payload + 1460U, 1460U);
	Pump(300U);
	eth_transport_get_statistics(&stats);
	assert(stats.tcp[0].rx_pending == ETH_TCP_QUEUE_BYTES && stats.tcp[0].rx_peak == ETH_TCP_QUEUE_BYTES);
	w = Last(s_wire_count > 8U ? s_wire_count - 8U : 0U, passive.pc_port, ACK, 0);
	assert(w && w->window == 0U);
	memset(s_payload, 0, sizeof(s_payload));
	assert(eth_tcp_read(server_handle, s_payload, sizeof(s_payload)) == sizeof(s_payload));
	for (i = 0; i < sizeof(s_payload); ++i)
		assert(s_payload[i] == 0xA7);
	Pump(300U);
	w = Last(s_wire_count > 8U ? s_wire_count - 8U : 0U, passive.pc_port, ACK, 0);
	assert(w && w->window != 0U);

	//耗尽真实 TCP_SEG 池；APP 环保持不变，释放后只提交一次。
	for (n = 0U; n < MEMP_NUM_TCP_SEG; ++n)
	{
		segments[n] = memp_malloc(MEMP_TCP_SEG);
		if (!segments[n])
			break;
	}
	assert(n && memp_malloc(MEMP_TCP_SEG) == NULL);
	assert(eth_tcp_send(server_handle, s_payload, sizeof(s_payload)) == ERR_OK);
	assert(eth_tcp_send(server_handle, s_payload, 1U) == ERR_MEM);
	Pump(100U);
	eth_transport_get_statistics(&stats);
	assert(stats.tcp[0].write_mem && stats.tcp[0].tx_pending == sizeof(s_payload));
	for (i = 0U; i < n; ++i)
		memp_free(MEMP_TCP_SEG, segments[i]);
	mark = s_wire_count;
	Pump(100U);
	w = Last(mark, passive.pc_port, ACK, 1);
	assert(w);
	seq = w->seq;
	count = w->length;
	//不给 ACK，先观察相同序号重传；计数仍未完成。
	mark = s_wire_count;
	Pump(5000U);
	w = Last(mark, passive.pc_port, ACK, 1);
	assert(w && w->seq <= seq && w->length);
	eth_transport_get_statistics(&stats);
	assert(stats.tcp[0].unacked == sizeof(s_payload));
	passive.board_ack = seq + count;
	Tcp(&passive, ACK, 4096U, NULL, 0U);
	//累积 ACK 可能尚未覆盖两段，确认到发送尾后才完成应用 ACK。
	eth_transport_get_statistics(&stats);
	passive.board_ack += stats.tcp[0].unacked;
	Tcp(&passive, ACK, 4096U, NULL, 0U);
	eth_transport_get_statistics(&stats);
	assert(stats.tcp[0].unacked == 0U && stats.tcp[0].submitted == stats.tcp[0].acked);
	for (n = 0; n < 64U; ++n)
	{
		heap[n] = mem_malloc(256U);
		if (!heap[n])
			break;
	}
	assert(n < 64U && eth_udp_send(&ip, 5002U, s_payload, 1472U) == ERR_MEM);
	for (i = 0; i < n; ++i)
		mem_free(heap[i]);
	assert(eth_udp_send(&ip, 5002U, s_payload, 16U) == ERR_OK);
	Pump(20U);
	puts("PASS: chained RX/window zero and reopen, app queue full, actual TCP segment/heap exhaustion, delayed ACK and retransmission recovery");

	//对端仍连接但通告零窗口，模拟暂不读取；有界排队直到窗口重新开放。
	Tcp(&passive, ACK, 4096U, s_payload, 1U);
	Tcp(&passive, ACK, 0U, NULL, 0U); //不同的 SEG.SEQ 让缩窗公告生效
	assert(eth_tcp_read(server_handle, s_payload, 1U) == 1U);
	mark = s_wire_count;
	assert(eth_tcp_send(server_handle, s_payload, sizeof(s_payload)) == ERR_OK);
	Pump(100U);
	assert(!Last(mark, passive.pc_port, ACK, 1));
	assert(eth_tcp_send(server_handle, s_payload, sizeof(s_payload)) == ERR_OK);
	assert(eth_tcp_send(server_handle, s_payload, 1U) == ERR_MEM);
	eth_transport_get_statistics(&stats);
	assert(stats.tcp[0].unacked == sizeof(s_payload) && stats.tcp[0].tx_pending == sizeof(s_payload));
	seq = passive.board_ack;
	for (i = 0; i < 8U && passive.board_ack - seq < 2U * sizeof(s_payload); ++i)
	{
		mark = s_wire_count;
		Tcp(&passive, ACK, 4096U, NULL, 0U);
		Pump(100U);
		w = Last(mark, passive.pc_port, ACK, 1);
		if (!w || w->seq + w->length <= passive.board_ack ||
			w->seq + w->length - seq > 2U * sizeof(s_payload))
			fprintf(stderr, "window recovery: iteration=%u wire=%u frame=%u ack=%u end=%u snd_wnd=%u cwnd=%u\n",
				i, s_wire_count - mark, w != NULL, passive.board_ack, w ? w->seq + w->length : 0U,
				(unsigned)tcp_active_pcbs->snd_wnd, (unsigned)tcp_active_pcbs->cwnd);
		assert(w && w->seq + w->length > passive.board_ack &&
			w->seq + w->length - seq <= 2U * sizeof(s_payload));
		passive.board_ack = w->seq + w->length;
	}
	assert(passive.board_ack - seq == 2U * sizeof(s_payload));
	Tcp(&passive, ACK, 4096U, NULL, 0U);
	eth_transport_get_statistics(&stats);
	assert(!stats.tcp[0].unacked && !stats.tcp[0].tx_pending && stats.tcp[0].acked == stats.tcp[0].submitted);
	for (i = 0; i < PBUF_POOL_SIZE; ++i)
	{
		held[i] = pbuf_alloc(PBUF_RAW, 1U, PBUF_POOL);
		assert(held[i]);
	}
	assert(pbuf_alloc(PBUF_RAW, 1U, PBUF_POOL) == NULL);
	len = e5_frame_build(s_payload, E5_REQUEST, 3U, 16U);
	n = s_network.port.statistics.rx_no_pbuf;
	Udp(s_payload, len, 0);
	assert(s_network.port.statistics.rx_no_pbuf == n + 1U);
	for (i = 0; i < PBUF_POOL_SIZE; ++i)
		pbuf_free(held[i]);
	mark = s_wire_count;
	Udp(s_payload, len, 0);
	assert(Last(mark, 5002U, 0U, 1));
	puts("PASS: peer zero-window backpressure and recovery; PBUF_POOL exhaustion/drop/recovery");

	//正常对端 FIN 后由本端发送 FIN；另一角色 RST 清掉无效 PCB。
	mark = s_wire_count;
	Tcp(&passive, FIN | ACK, 4096U, NULL, 0U);
	Pump(100U);
	w = Last(mark, passive.pc_port, FIN, 0);
	assert(w && eth_tcp_get_state(server_handle) == ETH_TCP_IDLE);
	passive.board_ack = w->seq + 1U;
	Tcp(&passive, ACK, 4096U, NULL, 0U);
	Tcp(&active, RST | ACK, 4096U, NULL, 0U);
	assert(eth_tcp_get_state(eth_tcp_handle(ETH_TCP_CLIENT)) == ETH_TCP_IDLE);
	passive = Syn(51002U, 32000U);
	Tcp(&passive, ACK, 4096U, NULL, 0U);
	assert(eth_tcp_send(server_handle, s_payload, 1U) == ERR_CONN);
	server_handle = eth_tcp_handle(ETH_TCP_SERVER);
	assert(eth_tcp_get_state(server_handle) == ETH_TCP_OPEN);
	//lwIP 2.2.1 的 FIN 分配失败内部设 CLOSEPEND，并返回 OK 移交所有权。
	for (n = 0U; n < MEMP_NUM_TCP_SEG; ++n)
	{
		segments[n] = memp_malloc(MEMP_TCP_SEG);
		if (!segments[n])
			break;
	}
	assert(eth_tcp_close(server_handle) == ERR_OK);
	assert(eth_tcp_close(server_handle) == ERR_ALREADY);
	mark = s_wire_count;
	Pump(100U);
	assert(!Last(mark, passive.pc_port, FIN, 0) && eth_tcp_get_state(server_handle) == ETH_TCP_IDLE);
	assert(tcp_active_pcbs && (tcp_active_pcbs->flags & TF_CLOSEPEND));
	for (i = 0U; i < n; ++i)
		memp_free(MEMP_TCP_SEG, segments[i]);
	Pump(400U);
	w = Last(mark, passive.pc_port, FIN, 0);
	assert(w);
	passive.board_ack = w->seq + 1U;
	Tcp(&passive, FIN | ACK, 4096U, NULL, 0U);
	assert(tcp_tw_pcbs != NULL && tcp_tw_pcbs->state == TIME_WAIT);
	//超过 PCB 容量的正常连接轮换；允许 lwIP 自行回收老 TIME_WAIT。
	for (i = 0U; i < MEMP_NUM_TCP_PCB + 2U; ++i)
	{
		passive = Syn((uint16_t)(51100U + i), 33000U + 100U * i);
		Tcp(&passive, ACK, 4096U, NULL, 0U);
		server_handle = eth_tcp_handle(ETH_TCP_SERVER);
		assert(eth_tcp_close(server_handle) == ERR_OK);
		mark = s_wire_count;
		Pump(60U);
		w = Last(mark, passive.pc_port, FIN, 0);
		assert(w);
		passive.board_ack = w->seq + 1U;
		Tcp(&passive, FIN | ACK, 4096U, NULL, 0U);
		assert(lwip_stats.memp[MEMP_TCP_PCB]->used <= MEMP_NUM_TCP_PCB);
	}
	mark = s_wire_count;
	Pump(2100U);
	w = Last(mark, 5004U, SYN, 0);
	if (!w)
	{ //允许上一段已进入 SYN_SENT
		assert(eth_tcp_get_state(eth_tcp_handle(ETH_TCP_CLIENT)) == ETH_TCP_CONNECTING);
		w = Last(s_wire_count > 25U ? s_wire_count - 25U : 0U, 5004U, SYN, 0);
	}
	assert(w);
	active.board_port = w->source;
	active.board_ack = w->seq + 1U;
	Tcp(&active, RST | ACK, 4096U, NULL, 0U);
	assert(eth_tcp_get_state(eth_tcp_handle(ETH_TCP_CLIENT)) == ETH_TCP_IDLE);
	Pump(2100U);
	Pump(10100U); //无 SYN-ACK，应用连接截止时间到
	eth_transport_get_statistics(&stats);
	assert(stats.tcp[ETH_TCP_CLIENT].timeouts >= 1U);
	s_link = 0U;
	Pump(100U);
	eth_transport_get_statistics(&stats);
	assert(!stats.ready && !stats.listening && !stats.connections && stats.link_lost == 1U);
	assert(lwip_stats.memp[MEMP_UDP_PCB]->used == 0U && lwip_stats.memp[MEMP_PBUF_POOL]->used == 0U);
	for (i = 0U; i < MEMP_NUM_TCP_PCB; ++i)
	{
		pcbs[i] = tcp_new();
		assert(pcbs[i]);
	}
	assert(tcp_new() == NULL);
	for (i = 0U; i < MEMP_NUM_TCP_PCB; ++i)
		tcp_abort(pcbs[i]);
	assert(lwip_stats.memp[MEMP_TCP_PCB]->used == 0U);
	//实际回收 TIME_WAIT 后，检查定时器池耗尽和恢复，不把断言接口当探测。
	for (n = 0U; n < MEMP_NUM_SYS_TIMEOUT; ++n)
	{
		timers[n] = memp_malloc(MEMP_SYS_TIMEOUT);
		if (!timers[n])
			break;
	}
	assert(n >= 2U && n < MEMP_NUM_SYS_TIMEOUT);
	assert(memp_malloc(MEMP_SYS_TIMEOUT) == NULL);
	for (i = 0U; i < n; ++i)
		memp_free(MEMP_SYS_TIMEOUT, timers[i]);
	sys_timeout(100U, Timer, &s_network);
	Pump(100U);
	assert(s_timer_calls == 1U);
	s_link = 1U;
	Pump(100U);
	Bind();
	Pump(20U);
	eth_transport_get_statistics(&stats);
	assert(stats.ready && stats.listening && stats.link_ready == 2U &&
		lwip_stats.memp[MEMP_UDP_PCB]->used == 2U);
	assert(lwip_stats.memp[MEMP_SYS_TIMEOUT]->err == 2U);
	puts("PASS: peer FIN/RST, CLOSEPEND retry, TIME_WAIT churn, PCB/timer exhaustion, connect refusal/timeout, DHCP relink");
	printf("Host injection peaks: heap=%u bytes; timeout=%u; TCP PCB=%u; TCP listen=%u; TCP segment=%u; UDP PCB=%u; PBUF_POOL=%u\n",
		(unsigned)lwip_stats.mem.max, (unsigned)lwip_stats.memp[MEMP_SYS_TIMEOUT]->max,
		(unsigned)lwip_stats.memp[MEMP_TCP_PCB]->max, (unsigned)lwip_stats.memp[MEMP_TCP_PCB_LISTEN]->max,
		(unsigned)lwip_stats.memp[MEMP_TCP_SEG]->max, (unsigned)lwip_stats.memp[MEMP_UDP_PCB]->max,
		(unsigned)lwip_stats.memp[MEMP_PBUF_POOL]->max);
	return 0;
}
