#ifndef ETH_MJPEG_H
#define ETH_MJPEG_H

#include "eth_tcp.h"

typedef struct
{
	const uint8_t *data;
	uint16_t length;
	uint32_t crc32;
} EthMjpegFrame_t;

typedef enum
{
	ETH_MJPEG_IDLE, ETH_MJPEG_RESPONSE_HEADER, ETH_MJPEG_FRAME_HEADER,
	ETH_MJPEG_FRAME_DATA, ETH_MJPEG_FRAME_TRAILER, ETH_MJPEG_WAIT_NEXT_FRAME,
	ETH_MJPEG_STOPPING, ETH_MJPEG_CLOSED, ETH_MJPEG_ERROR
} EthMjpegState_t;

typedef struct
{
	uint32_t frames_submitted, frames_completed, bytes_submitted;
	uint32_t send_busy, send_errors, frame_skipped, current_fps_milli, max_send_latency_ms;
} EthMjpegStats_t;

typedef struct
{
	EthMjpegState_t state;
	EthMjpegStats_t stats;
	const EthMjpegFrame_t *frames;
	uint32_t ack_base, submitted, frame_end, frame_started, next_frame;
	uint32_t fps_started, fps_frames, last_progress, last_ack, sequence, retry_due;
	uint16_t count, period_ms, offset, header_length;
	uint8_t waiting_ack;
	char header[128];
} EthMjpeg_t;

void eth_mjpeg_start(EthMjpeg_t *stream, const EthMjpegFrame_t *frames, uint16_t count,
	uint16_t period_ms, uint32_t now, uint32_t ack_base);
void eth_mjpeg_ack(EthMjpeg_t *stream, uint32_t now, uint32_t total_acked);
void eth_mjpeg_poll(EthMjpeg_t *stream, EthTcpHandle_t handle, uint32_t now);

#endif
