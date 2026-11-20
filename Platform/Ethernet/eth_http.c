#include "eth_http.h"
#include "lwip/sys.h"
#include <stdio.h>
#include <string.h>

typedef enum { HTTP_IDLE, HTTP_REQUEST, HTTP_RESPONSE, HTTP_STREAM, HTTP_DRAIN } HttpState_t;
enum { HTTP_ROUTE_STREAM = 1000, HTTP_ROUTE_HEALTH };

static struct
{
	EthNetwork_t *network;
	const EthMjpegFrame_t *frames;
	EthMjpegSource_t source;
	EthTcpHandle_t handle;
	EthMjpeg_t mjpeg;
	EthHttpStats_t stats;
	HttpState_t state;
	unsigned events;
	uint32_t started;
	uint16_t count, period, used, response_length;
	uint8_t stream_fast_send;
	char request[ETH_HTTP_REQUEST_MAX];
	char response[1024];
} s_http;

static const char s_home[] = "<!doctype html><html lang=en><meta charset=utf-8>"
	"<meta name=viewport content=\"width=device-width,initial-scale=1\">"
	"<link rel=icon href=\"data:,\"><title>STM32H747 Ethernet Camera</title>"
	"<h1>STM32H747 Ethernet Camera</h1><p>IPv4 %s | Connected</p>"
	"<img src=\"/stream.mjpg\" width=320 height=240 alt=\"MJPEG test stream\" "
	"onerror=\"if((this.n=this.n+1||1)&lt;=3)setTimeout(()=>{this.removeAttribute('src');this.src='/stream.mjpg'},500)\">";

static int EthHttp_Token(char ch)
{
	return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
		(ch >= 'a' && ch <= 'z') || (ch && strchr("!#$%&'*+-.^_`|~", ch) != NULL);
}

static int EthHttp_Equal(const char *text, size_t len, const char *word)
{
	return strlen(word) == len && memcmp(text, word, len) == 0;
}

static const char *EthHttp_LineEnd(const char *p, const char *end)
{
	for (; end - p >= 2; ++p)
		if (p[0] == '\r' && p[1] == '\n')
			return p;
	return NULL;
}

static unsigned EthHttp_Parse(const char *request, const char *end)
{
	const char *line = EthHttp_LineEnd(request, end);
	const char *method, *path, *p, *next;
	if (!line)
		return 400U;
	method = memchr(request, ' ', (size_t)(line - request));
	if (!method || method == request)
		return 400U;
	for (p = request; p < method; ++p)
		if (!EthHttp_Token(*p))
			return 400U;
	path = memchr(method + 1, ' ', (size_t)(line - method - 1));
	if (!path || method[1] != '/' ||
		(!EthHttp_Equal(path + 1, (size_t)(line - path - 1), "HTTP/1.1") &&
		 !EthHttp_Equal(path + 1, (size_t)(line - path - 1), "HTTP/1.0")))
		return 400U;
	for (p = method + 1; p < path; ++p)
		if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') ||
			(*p >= 'A' && *p <= 'Z') || *p == '/' || *p == '.' || *p == '-' || *p == '_'))
			return 400U;
	for (p = line + 2; p < end - 2; p = next + 2)
	{
		const char *colon;
		next = EthHttp_LineEnd(p, end);
		if (!next || next == p)
			return 400U;
		colon = p;
		while (colon < next && EthHttp_Token(*colon))
			++colon;
		if (colon == p || colon == next || *colon != ':')
			return 400U;
		for (++colon; colon < next; ++colon)
			if (((unsigned char)*colon < 32U && *colon != '\t') || *colon == 127)
				return 400U;
	}
	if (!EthHttp_Equal(request, (size_t)(method - request), "GET"))
		return 405U;
	++method;
	if (EthHttp_Equal(method, (size_t)(path - method), "/"))
		return 200U;
	if (EthHttp_Equal(method, (size_t)(path - method), "/stream.mjpg"))
		return HTTP_ROUTE_STREAM;
	if (EthHttp_Equal(method, (size_t)(path - method), "/health"))
		return HTTP_ROUTE_HEALTH;
	if (EthHttp_Equal(method, (size_t)(path - method), "/favicon.ico"))
		return 204U;
	return 404U;
}

static void EthHttp_Event(EthTcpHandle_t handle, unsigned events, void *arg)
{
	(void)arg;
	if (handle.role != ETH_TCP_SERVER || handle.generation != eth_tcp_handle(ETH_TCP_SERVER).generation)
		return;
	if (events & ETH_TCP_EVENT_OPEN)
	{
		eth_mjpeg_release_frame(&s_http.mjpeg);
		s_http.handle = handle;
		s_http.events = 0U;
		s_http.used = 0U;
		s_http.started = sys_now();
		s_http.state = HTTP_REQUEST;
		s_http.mjpeg.state = ETH_MJPEG_IDLE;
		++s_http.stats.sessions;
	}
	if (s_http.state != HTTP_IDLE && handle.generation == s_http.handle.generation)
		s_http.events |= events;
}

static err_t EthHttp_Init(EthNetwork_t *network, uint16_t frame_period_ms)
{
	EthTransportConfig_t config = {0};
	err_t result;
	if (frame_period_ms < 50U || frame_period_ms > 5000U)
		return ERR_ARG;
	config.listen_port = 80U;
	config.tcp_event = EthHttp_Event;
	result = eth_transport_init(network, &config);
	if (result != ERR_OK)
		return result;
	s_http.network = network;
	s_http.period = frame_period_ms;
	s_http.stream_fast_send = 0U;
	return ERR_OK;
}

err_t eth_http_init(EthNetwork_t *network, const EthMjpegFrame_t *frames,
	uint16_t frame_count, uint16_t frame_period_ms)
{
	uint16_t i;
	err_t result;
	if (!frames || !frame_count)
		return ERR_ARG;
	for (i = 0U; i < frame_count; ++i)
		if (!frames[i].data || frames[i].length < 4U)
			return ERR_ARG;
	result = EthHttp_Init(network, frame_period_ms);
	if (result == ERR_OK)
	{
		s_http.frames = frames;
		s_http.count = frame_count;
	}
	return result;
}

err_t eth_http_init_source(EthNetwork_t *network, const EthMjpegSource_t *source,
	uint16_t frame_period_ms)
{
	err_t result;
	if (!source || !source->acquire || !source->release)
		return ERR_ARG;
	result = EthHttp_Init(network, frame_period_ms);
	if (result == ERR_OK)
		s_http.source = *source;
	return result;
}

static void EthHttp_Abort(void)
{
	eth_mjpeg_release_frame(&s_http.mjpeg);
	eth_tcp_abort(s_http.handle);
	s_http.state = HTTP_DRAIN;
}

static void EthHttp_Respond(unsigned code)
{
	const char *status, *body, *type = "text/plain; charset=utf-8", *allow = "";
	char page[512], ip[16];
	int length;
	switch (code)
	{
	case 200U:
		++s_http.stats.home;
		ip4addr_ntoa_r(netif_ip4_addr(&s_http.network->interface), ip, sizeof(ip));
		length = snprintf(page, sizeof(page), s_home, ip);
		if (length < 0 || (size_t)length >= sizeof(page))
		{
			++s_http.stats.response_errors;
			EthHttp_Abort();
			return;
		}
		status = "200 OK";
		body = page;
		type = "text/html; charset=utf-8";
		break;
	case HTTP_ROUTE_HEALTH: ++s_http.stats.health; status = "200 OK"; body = "ok\n"; break;
	case 204U: ++s_http.stats.favicon; status = "204 No Content"; body = ""; break;
	case 405U:
		++s_http.stats.method_rejected; status = "405 Method Not Allowed";
		body = "GET only\n"; allow = "Allow: GET\r\n"; break;
	case 404U: ++s_http.stats.not_found; status = "404 Not Found"; body = "not found\n"; break;
	case 408U: ++s_http.stats.request_timeout; status = "408 Request Timeout"; body = "timeout\n"; break;
	case 431U: ++s_http.stats.headers_too_large; status = "431 Request Header Fields Too Large"; body = "header too large\n"; break;
	default: ++s_http.stats.bad_request; status = "400 Bad Request"; body = "bad request\n"; break;
	}
	if (code == 204U)
		length = snprintf(s_http.response, sizeof(s_http.response),
			"HTTP/1.1 204 No Content\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n");
	else
		length = snprintf(s_http.response, sizeof(s_http.response),
			"HTTP/1.1 %s\r\nConnection: close\r\nCache-Control: no-store\r\n%s"
			"Content-Type: %s\r\nContent-Length: %u\r\n\r\n%s",
			status, allow, type, (unsigned)strlen(body), body);
	if (length <= 0 || (size_t)length >= sizeof(s_http.response))
	{
		++s_http.stats.response_errors;
		EthHttp_Abort();
		return;
	}
	s_http.response_length = (uint16_t)length;
	s_http.state = HTTP_RESPONSE;
}

//在测试入口初始化后配置，仅用于后续MJPEG连接
void eth_http_set_stream_fast_send(int enabled)
{
	s_http.stream_fast_send = enabled != 0;
}

static void EthHttp_Request(uint32_t now, uint32_t acked)
{
	uint16_t old = s_http.used, i;
	unsigned code;
	s_http.used += eth_tcp_read(s_http.handle, s_http.request + old, (uint16_t)(sizeof(s_http.request) - old));
	for (i = old < 3U ? 0U : (uint16_t)(old - 3U); i + 4U <= s_http.used; ++i)
	{
		if (memcmp(s_http.request + i, "\r\n\r\n", 4U) != 0)
			continue;
		++s_http.stats.requests;
		s_http.stats.extra_rx += s_http.used - i - 4U;
		code = EthHttp_Parse(s_http.request, s_http.request + i + 4U);
		if (code == HTTP_ROUTE_STREAM)
		{
			++s_http.stats.streams;
			if (s_http.stream_fast_send)
				(void)eth_tcp_enable_fast_send(s_http.handle);
			eth_mjpeg_start(&s_http.mjpeg, s_http.frames, s_http.count, s_http.period, now, acked);
			if (s_http.source.acquire)
				eth_mjpeg_set_source(&s_http.mjpeg, &s_http.source);
			s_http.state = HTTP_STREAM;
		}
		else
			EthHttp_Respond(code);
		return;
	}
	if (s_http.used == sizeof(s_http.request))
		EthHttp_Respond(431U);
	else if ((uint32_t)(now - s_http.started) >= 5000U)
		EthHttp_Respond(408U);
}

void eth_http_poll(void)
{
	char discard[128];
	EthTcpStats_t tcp;
	err_t result;
	unsigned events = s_http.events;
	uint32_t now = sys_now();
	s_http.events = 0U;
	if (s_http.state == HTTP_IDLE)
		return;
	eth_tcp_get_statistics(ETH_TCP_SERVER, &tcp);
	if (s_http.mjpeg.state >= ETH_MJPEG_RESPONSE_HEADER && s_http.mjpeg.state <= ETH_MJPEG_STOPPING)
		eth_mjpeg_ack(&s_http.mjpeg, now, tcp.acked);
	if (events & ETH_TCP_EVENT_FIN)
	{
		eth_mjpeg_release_frame(&s_http.mjpeg);
		++s_http.stats.peer_fin;
		if (s_http.state == HTTP_STREAM)
			s_http.mjpeg.state = ETH_MJPEG_STOPPING;
		s_http.state = HTTP_DRAIN;
	}
	if ((events & ETH_TCP_EVENT_CLOSED) || eth_tcp_get_state(s_http.handle) == ETH_TCP_IDLE)
	{
		eth_mjpeg_release_frame(&s_http.mjpeg);
		++s_http.stats.closed;
		if (tcp.last_error != ERR_OK)
			++s_http.stats.connection_errors;
		if (s_http.mjpeg.state != ETH_MJPEG_IDLE && s_http.mjpeg.state != ETH_MJPEG_ERROR)
			s_http.mjpeg.state = ETH_MJPEG_CLOSED;
		s_http.mjpeg.stats.current_fps_milli = 0U;
		s_http.state = HTTP_IDLE;
		return;
	}
	if (s_http.state == HTTP_REQUEST)
	{
		EthHttp_Request(now, tcp.acked);
		return;
	}
	//一个连接只处理一次请求，持续归还 RX 窗口以允许 E5 正常排空。
	s_http.stats.extra_rx += eth_tcp_read(s_http.handle, discard, sizeof(discard));
	if (s_http.state == HTTP_RESPONSE)
	{
		result = eth_tcp_send(s_http.handle, s_http.response, s_http.response_length);
		if (result == ERR_OK)
		{
			eth_tcp_close(s_http.handle);
			s_http.state = HTTP_DRAIN;
		}
		else if (result == ERR_MEM)
			++s_http.stats.response_busy;
		else
		{
			++s_http.stats.response_errors;
			EthHttp_Abort();
		}
	}
	else if (s_http.state == HTTP_STREAM)
	{
		eth_mjpeg_poll(&s_http.mjpeg, s_http.handle, now);
		if (s_http.mjpeg.state == ETH_MJPEG_ERROR)
			EthHttp_Abort();
	}
}

void eth_http_get_statistics(EthHttpStats_t *stats)
{
	if (stats)
	{
		*stats = s_http.stats;
		stats->stream_state = s_http.mjpeg.state;
		stats->stream = s_http.mjpeg.stats;
	}
}
