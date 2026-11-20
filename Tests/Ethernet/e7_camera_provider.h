#ifndef E7_CAMERA_PROVIDER_H
#define E7_CAMERA_PROVIDER_H

#include "camera_capture.h"
#include "eth_mjpeg.h"

#define E7_JPEG_CAPACITY (60U * 1024U)

typedef struct
{
	uint8_t *buffer;
	const uint8_t *fifo;
	uint32_t fifo_size, lease;
	uint32_t copies, invalid, oversize, no_frame, release_errors, stale_releases;
	uint32_t capture_errors;
	uint32_t capture_fps_milli, copy_fps_milli, last_sample, last_completed, last_copies;
	CameraCaptureStats_t capture;
	CameraCaptureStopFacts_t stop_facts;
	SysResult_t media_result, stop_result;
	bool running, pinned, original_loan;
} E7CameraProvider_t;

EthMjpegAcquire_t E7Camera_Acquire(void *context, EthMjpegFrame_t *frame, uint32_t *lease);
void E7Camera_Release(void *context, uint32_t lease);
SysResult_t E7Camera_Start(E7CameraProvider_t *provider);
SysResult_t E7Camera_Stop(E7CameraProvider_t *provider);
void E7Camera_Poll(E7CameraProvider_t *provider, uint32_t now);

#endif
