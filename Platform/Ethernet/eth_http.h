#ifndef ETH_HTTP_H
#define ETH_HTTP_H

#include "eth_mjpeg.h"
#include "eth_transport.h"

#define ETH_HTTP_REQUEST_MAX 512U

typedef struct
{
	uint32_t sessions, requests, home, health, streams, favicon;
	uint32_t bad_request, method_rejected, not_found, headers_too_large, request_timeout;
	uint32_t peer_fin, closed, connection_errors, response_busy, response_errors, extra_rx;
	EthMjpegState_t stream_state;
	EthMjpegStats_t stream;
} EthHttpStats_t;

err_t eth_http_init(EthNetwork_t *network, const EthMjpegFrame_t *frames,
	uint16_t frame_count, uint16_t frame_period_ms);
void eth_http_poll(void);
void eth_http_get_statistics(EthHttpStats_t *stats);

#endif
