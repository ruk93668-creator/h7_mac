#include "eth_http.h"
#define ETH_HOST_APP_POLL eth_http_poll
#define ETH_HOST_STEP_MS 1U
#include "eth_host_wire.h"
#include "e6_jpeg_fixtures/e6_jpeg_fixtures.h"
#include "lwip/priv/tcp_priv.h"
#include <stdbool.h>

static char s_body[100000];
static size_t s_length;
static unsigned s_cursor;
static unsigned s_fin;
static unsigned s_fixture_start;

static Peer_t Open(unsigned port)
{
	Peer_t peer = WireSyn(80U, (uint16_t)port, 1000U + port);
	Tcp(&peer, ACK, 4096U, NULL, 0U);
	assert(eth_tcp_get_state(eth_tcp_handle(ETH_TCP_SERVER)) == ETH_TCP_OPEN);
	s_length = 0U;
	s_fin = 0U;
	s_cursor = s_wire_count;
	s_body[0] = '\0';
	return peer;
}

static void Send(Peer_t *peer, uint16_t window, const char *request)
{
	Tcp(peer, PSH | ACK, window, request, (uint16_t)strlen(request));
}

static void Collect(Peer_t *peer, unsigned ms)
{
	uint32_t started = s_tick;
	while ((uint32_t)(s_tick - started) < ms)
	{
		uint32_t prior = peer->board_ack;
		unsigned end;
		Pump(ETH_HOST_STEP_MS);
		end = s_wire_count;
		assert(end - s_cursor <= 128U);
		for (; s_cursor < end; ++s_cursor)
		{
			const Wire_t *w = &s_wire[s_cursor % 128U];
			if (w->dest != peer->pc_port || w->seq != peer->board_ack || w->protocol != 6U)
				continue;
			assert(s_length + w->length < sizeof(s_body));
			memcpy(s_body + s_length, w->data, w->length);
			s_length += w->length;
			s_body[s_length] = '\0';
			peer->board_ack += w->length;
			if (w->flags & FIN)
			{
				++peer->board_ack;
				++s_fin;
			}
		}
		if (prior != peer->board_ack)
			Tcp(peer, ACK, 4096U, NULL, 0U);
	}
}

static void CheckResponse(Peer_t *peer, const char *status, const char *body)
{
	const char *header_end;
	Collect(peer, 360U);
	assert(strncmp(s_body, status, strlen(status)) == 0);
	assert(s_fin == 1U);
	header_end = strstr(s_body, "\r\n\r\n");
	assert(header_end);
	if (body)
		assert(strstr(header_end + 4, body));
	Tcp(peer, FIN | ACK, 4096U, NULL, 0U);
	Pump(100U);
	assert(eth_tcp_get_state(eth_tcp_handle(ETH_TCP_SERVER)) == ETH_TCP_IDLE);
}

static uint32_t Crc(const uint8_t *data, uint16_t length)
{
	uint32_t crc = UINT32_MAX;
	while (length--)
	{
		unsigned bit;
		crc ^= *data++;
		for (bit = 0U; bit < 8U; ++bit)
			crc = (crc >> 1U) ^ ((crc & 1U) ? UINT32_C(0xEDB88320) : 0U);
	}
	return ~crc;
}

static void CheckParts(unsigned count, unsigned first)
{
	const char *part = strstr(s_body, "\r\n\r\n");
	unsigned i;
	assert(strncmp(s_body, "HTTP/1.1 200 OK", 15U) == 0);
	assert(strstr(s_body, "multipart/x-mixed-replace; boundary=h7frame") && part);
	part += 4;
	for (i = 0; i < count; ++i)
	{
		const EthMjpegFrame_t *image = &e6_jpeg_frames[(first + i) % E6_FRAME_COUNT];
		char expected[128];
		int header_size = snprintf(expected, sizeof(expected), "--h7frame\r\nContent-Type: image/jpeg\r\n"
			"Content-Length: %u\r\nX-Frame-Sequence: %u\r\nX-Frame-CRC32: %08lX\r\n\r\n",
			image->length, i + 1U, (unsigned long)image->crc32);
		assert(header_size > 0 && (size_t)(part - s_body) + (size_t)header_size + image->length + 2U <= s_length);
		assert(memcmp(part, expected, (size_t)header_size) == 0);
		part += header_size;
		assert(memcmp(part, image->data, image->length) == 0 && Crc((const uint8_t *)part, image->length) == image->crc32);
		assert((uint8_t)part[0] == 255U && (uint8_t)part[1] == 216U &&
			(uint8_t)part[image->length - 2U] == 255U && (uint8_t)part[image->length - 1U] == 217U);
		part += image->length;
		assert(memcmp(part, "\r\n", 2U) == 0);
		part += 2U;
	}
}

static struct
{
	uint32_t generation, index, acquisitions, releases;
	bool pinned;
} s_dynamic;

static EthMjpegAcquire_t Acquire(void *context, EthMjpegFrame_t *frame, uint32_t *lease)
{
	uint32_t generation = eth_tcp_handle(ETH_TCP_SERVER).generation;
	(void)context;
	assert(!s_dynamic.pinned);
	if (generation != s_dynamic.generation)
	{
		s_dynamic.generation = generation;
		s_dynamic.index = s_fixture_start;
	}
	*frame = e6_jpeg_frames[s_dynamic.index++ % E6_FRAME_COUNT];
	*lease = ++s_dynamic.acquisitions;
	s_dynamic.pinned = true;
	return ETH_MJPEG_ACQUIRED;
}

static void Release(void *context, uint32_t lease)
{
	(void)context;
	assert(s_dynamic.pinned && lease == s_dynamic.acquisitions);
	++s_dynamic.releases;
	s_dynamic.pinned = false;
}

int main(int argc, char **argv)
{
	static const char stream[] = "GET /stream.mjpg HTTP/1.1\r\nHost: test\r\n\r\n";
	static const struct { const char *request, *status, *body; } cases[] = {
		{"GET /health HTTP/1.1\r\n\r\nGET /missing HTTP/1.0\r\n\r\n", "HTTP/1.1 200", "ok\n"},
		{"GET /missing HTTP/1.1\r\n\r\n", "HTTP/1.1 404", NULL},
		{"GET /favicon.ico HTTP/1.1\r\n\r\n", "HTTP/1.1 204", NULL},
		{"POST / HTTP/1.1\r\n\r\n", "HTTP/1.1 405", NULL},
		{"G?T / HTTP/1.1\r\n\r\n", "HTTP/1.1 400", NULL},
		{"GET /%2f HTTP/1.1\r\n\r\n", "HTTP/1.1 400", NULL},
		{"GET / HTTP/1.1\r\nBad Header: test\r\n\r\n", "HTTP/1.1 400", NULL},
	};
	EthNetworkAddressConfig_t address = {.mode = ETH_ADDRESS_DHCP, .timeout_ms = 30000U};
	EthTransportConfig_t invalid = {0};
	EthHttpStats_t result, before;
	EthTcpHandle_t stale;
	ip4_addr_t other, original;
	Peer_t peer, old;
	char oversized[540];
	unsigned i;
	const EthMjpegSource_t source = {NULL, Acquire, Release};
	bool fast_send = argc == 2 && strcmp(argv[1], "dynamic-fast") == 0;
	bool dynamic = fast_send || (argc == 2 && strcmp(argv[1], "dynamic") == 0);
	(void)setvbuf(stdout, NULL, _IONBF, 0);
	assert(eth_network_init_address(&s_network, s_mac, &address) == ERR_OK);
	invalid.udp_port = 5001U;
	assert(eth_transport_init(&s_network, &invalid) == ERR_ARG);
	if (dynamic)
		assert(eth_http_init_source(&s_network, &source, 200U) == ERR_OK);
	else
		assert(eth_http_init(&s_network, e6_jpeg_frames, E6_FRAME_COUNT, 200U) == ERR_OK);
	eth_http_set_stream_fast_send(fast_send);
	s_link = 1U;
	Advance(100U);
	Bind();
	Pump(80U);
	assert(eth_tcp_listening() && lwip_stats.memp[MEMP_UDP_PCB]->used == 1U);
	peer = Open(51000U);
	Send(&peer, 4096U, "GE");
	Collect(&peer, 60U);
	assert(!s_length);
	Send(&peer, 4096U, "T / HTTP/1.1\r\nHost: test\r\nOther:");
	Collect(&peer, 60U);
	assert(!s_length);
	Send(&peer, 4096U, " ok\r\n\r");
	Send(&peer, 4096U, "\n");
	CheckResponse(&peer, "HTTP/1.1 200", "STM32H747 Ethernet Camera");
	assert(strstr(s_body, "src=\"/stream.mjpg\"") && strstr(s_body, "192.168.50.2"));
	for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		peer = Open(51001U + i);
		Send(&peer, 4096U, cases[i].request);
		CheckResponse(&peer, cases[i].status, cases[i].body);
	}
	peer = Open(51012U);
	memset(oversized, 'a', sizeof(oversized) - 1U);
	oversized[sizeof(oversized) - 1U] = '\0';
	Send(&peer, 4096U, oversized);
	CheckResponse(&peer, "HTTP/1.1 431", NULL);
	peer = Open(51013U);
	Send(&peer, 4096U, "GET / HTTP/1.1\r\n");
	Pump(5100U);
	CheckResponse(&peer, "HTTP/1.1 408", NULL);
	puts("PASS: HTTP complete, fragmented, pipelined, malformed, 404/405/431, favicon and timeout");
	peer = Open(51014U);
	Send(&peer, 4096U, stream);
	Collect(&peer, 2100U);
	CheckParts(8U, 0U);
	eth_http_get_statistics(&result);
	printf("ACKed frames=%lu; FPS(milli)=%lu\n", (unsigned long)result.stream.frames_completed,
		(unsigned long)result.stream.current_fps_milli);
	assert(result.stream.frames_completed >= 8U && result.stream.current_fps_milli >= 4500U &&
		result.stream.current_fps_milli <= 5500U);
	stale = eth_tcp_handle(ETH_TCP_SERVER);
	Tcp(&peer, FIN | ACK, 4096U, NULL, 0U);
	eth_http_get_statistics(&before);
	Collect(&peer, 340U);
	eth_http_get_statistics(&result);
	assert(s_fin == 1U && result.stream.bytes_submitted == before.stream.bytes_submitted);
	old = peer;
	peer = Open(51015U);
	assert(eth_tcp_send(stale, "x", 1U) == ERR_CONN);
	assert(eth_tcp_enable_fast_send(stale) == ERR_CONN);
	Tcp(&old, RST, 4096U, NULL, 0U);
	//快路径可在归零窗口时填满两级队列，使用超过总容量的JPEG验证背压
	if (fast_send)
	{
		s_fixture_start = 2U;
		assert(e6_jpeg_frames[s_fixture_start].length > ETH_TCP_QUEUE_BYTES + TCP_SND_BUF);
	}
	Send(&peer, 4096U, "GET ");
	Send(&peer, 0U, stream + 4U);
	Pump(2000U);
	eth_http_get_statistics(&result);
	assert(result.stream.send_busy > before.stream.send_busy && result.stream.frames_completed == before.stream.frames_completed);
	//A zero-window probe is not accepted by the simulated receiver.
	s_cursor = s_wire_count;
	Tcp(&peer, ACK, 4096U, NULL, 0U);
	Collect(&peer, 640U);
	CheckParts(2U, s_fixture_start);
	s_fixture_start = 0U;
	eth_http_get_statistics(&result);
	assert(result.stream.frame_skipped > before.stream.frame_skipped);
	Tcp(&peer, RST | ACK, 4096U, NULL, 0U);
	assert(eth_tcp_get_state(eth_tcp_handle(ETH_TCP_SERVER)) == ETH_TCP_IDLE);
	peer = Open(51016U);
	Send(&peer, 4096U, "GET ");
	Send(&peer, 0U, stream + 4U);
	Pump(16000U);
	eth_http_get_statistics(&result);
	assert(result.stream_state == ETH_MJPEG_ERROR && eth_tcp_get_state(eth_tcp_handle(ETH_TCP_SERVER)) == ETH_TCP_IDLE);
	puts("PASS: exact JPEG bytes and CRC, ACK cadence, zero-window retry, stall timeout, FIN/RST and stale generation");
	peer = Open(51017U);
	Send(&peer, 4096U, stream);
	s_link = 0U;
	Pump(100U);
	assert(!eth_tcp_listening() && !eth_transport_ready());
	s_link = 1U;
	Advance(100U);
	Bind();
	Pump(80U);
	{
		EthTransportStats_t transport;
		eth_transport_get_statistics(&transport);
		printf("DHCP relink ready=%u listen=%u bind=%d old TIME_WAIT port=%u\n",
			(unsigned)transport.ready, (unsigned)transport.listening, (int)transport.server_start,
			(unsigned)(tcp_tw_pcbs ? tcp_tw_pcbs->local_port : 0U));
		assert(transport.ready && transport.listening && transport.server_start == ERR_OK);
		assert(tcp_tw_pcbs && tcp_tw_pcbs->local_port == 80U);
	}
	peer = Open(51018U);
	Send(&peer, 4096U, stream);
	original = *netif_ip4_addr(&s_network.interface);
	IP4_ADDR(&other, 192, 168, 50, 22);
	netif_set_ipaddr(&s_network.interface, &other); //有效地址变更的 netif 边界
	Pump(100U);
	assert(eth_tcp_listening() && eth_tcp_get_state(eth_tcp_handle(ETH_TCP_SERVER)) == ETH_TCP_IDLE);
	netif_set_ipaddr(&s_network.interface, &original);
	Pump(100U);
	peer = Open(51019U);
	Send(&peer, 4096U, "GET /health HTTP/1.0\r\n\r\n");
	CheckResponse(&peer, "HTTP/1.1 200", "ok\n");
	for (i = 0U; i < 50U; ++i)
	{
		peer = Open(51200U + i);
		Send(&peer, 4096U, "GET / HTTP/1.1\r\n\r\n");
		CheckResponse(&peer, "HTTP/1.1 200", "192.168.50.2");
	}
	peer = Open(51250U);
	eth_http_get_statistics(&before);
	Send(&peer, 4096U, stream);
	{
		unsigned requests = s_dhcp_packets[DHCP_REQUEST], renewals = 0U;
		for (i = 0U; i < 605U; ++i)
		{
			if (i % 60U == 0U)
			{
				//The simulated PC refreshes ARP, as a real peer does over a ten-minute run.
				BuildArp();
				s_pending_rx = 1U;
				Pump(1U);
			}
			s_length = 0U;
			Collect(&peer, 1000U);
			if (s_dhcp_packets[DHCP_REQUEST] != requests)
			{
				requests = s_dhcp_packets[DHCP_REQUEST];
				Reply(DHCP_ACK);
				++renewals;
			}
			if (eth_tcp_get_state(eth_tcp_handle(ETH_TCP_SERVER)) != ETH_TCP_OPEN)
			{
				EthTcpStats_t tcp;
				eth_tcp_get_statistics(ETH_TCP_SERVER, &tcp);
				fprintf(stderr, "stream at second %u: DHCP state=%u renewals=%u TCP error=%d\n",
					i, (unsigned)s_network.address.state, renewals, (int)tcp.last_error);
				assert(tcp.state == ETH_TCP_OPEN);
			}
			assert(s_network.address.lease_valid);
		}
		eth_http_get_statistics(&result);
		assert(renewals >= 4U && result.stream.frames_completed - before.stream.frames_completed >= 3000U);
		assert(result.stream.send_errors == before.stream.send_errors);
		printf("PASS: virtual 605 s, %lu frames TCP-ACKed, %u DHCP renewals\n",
			(unsigned long)(result.stream.frames_completed - before.stream.frames_completed), renewals);
	}
	Tcp(&peer, FIN | ACK, 4096U, NULL, 0U);
	Collect(&peer, 340U);
	assert(s_fin == 1U);
	s_link = 0U;
	Pump(121000U);
	assert(lwip_stats.memp[MEMP_PBUF_POOL]->used == 0U &&
		lwip_stats.memp[MEMP_TCP_PCB]->used == 0U && lwip_stats.memp[MEMP_UDP_PCB]->used == 0U);
	puts("PASS: physical link transitions, DHCP reacquisition, address change, 50 repeated pages and resource recovery");
	if (dynamic)
	{
		assert(s_dynamic.acquisitions >= 3025U && s_dynamic.releases == s_dynamic.acquisitions && !s_dynamic.pinned);
		puts("PASS: dynamic source lease balance across backpressure, FIN/RST, DHCP and link recovery");
	}
	return 0;
}
