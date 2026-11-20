#include "e7_camera_provider.h"
#include "e6_jpeg_fixtures/e6_jpeg_fixtures.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t s_fifo[100000], s_network_copy[E7_JPEG_CAPACITY];
static CameraFrame_t s_ready;
static SysResult_t s_release_result, s_stop_result, s_capture_result;
static bool s_available, s_stopped = true, s_loan;
static unsigned s_releases, s_stops, s_process;

bool CameraCapture_StreamGetFrame(CameraFrame_t *frame)
{
	if (!s_available)
		return false;
	assert(!s_loan);
	*frame = s_ready;
	s_available = false;
	s_loan = true;
	return true;
}

SysResult_t CameraCapture_StreamReleaseFrame(void)
{
	assert(s_loan);
	++s_releases;
	if (s_release_result == SYS_OK)
		s_loan = false;
	return s_release_result;
}

SysResult_t CameraCapture_StartStream(void) { s_stopped = false; return SYS_OK; }
bool CameraCapture_IsStopped(void) { return s_stopped; }
SysResult_t CameraCapture_Stop(void)
{
	assert(!s_loan);
	++s_stops;
	s_stopped = s_stop_result == SYS_OK;
	return s_stop_result;
}

SysResult_t CameraCapture_GetStopFacts(CameraCaptureStopFacts_t *facts)
{
	facts->hardware_result = s_stop_result;
	facts->attempt_sequence = s_stops;
	facts->flags = s_stopped ? CAMERA_STOP_COMPLETE : CAMERA_STOP_ERROR;
	return SYS_OK;
}

void CameraCapture_Process(void) { ++s_process; }
SysResult_t CameraCapture_GetLastResult(void) { return s_capture_result; }
SysResult_t CameraCapture_GetStats(CameraCaptureStats_t *stats)
{
	stats->completed_frames = s_process;
	stats->dropped_frames = 0U;
	return SYS_OK;
}

static void Ready(const EthMjpegFrame_t *jpeg, bool wrap)
{
	s_ready.width = E7_CAMERA_WIDTH;
	s_ready.height = E7_CAMERA_HEIGHT;
	s_ready.format = CAMERA_PIXEL_FORMAT_JPEG;
	s_ready.data = wrap ? s_fifo + sizeof(s_fifo) - 1U : s_fifo;
	s_ready.data2 = wrap ? s_fifo : NULL;
	s_ready.size_bytes = jpeg->length;
	s_ready.size_bytes2 = wrap ? jpeg->length - 1U : 0U;
	if (wrap)
	{
		s_fifo[sizeof(s_fifo) - 1U] = jpeg->data[0];
		memcpy(s_fifo, jpeg->data + 1U, jpeg->length - 1U);
	}
	else
		memcpy(s_fifo, jpeg->data, jpeg->length);
	s_available = true;
}

int main(void)
{
	E7CameraProvider_t provider = {.buffer = s_network_copy, .fifo = s_fifo, .fifo_size = sizeof(s_fifo)};
	EthMjpegFrame_t frame;
	uint32_t lease, old;
	unsigned i;
	assert(E7Camera_Start(&provider) == SYS_OK);
	assert(E7Camera_Acquire(&provider, &frame, &lease) == ETH_MJPEG_UNAVAILABLE);
	Ready(&e6_jpeg_frames[0], true);
	assert(E7Camera_Acquire(&provider, &frame, &lease) == ETH_MJPEG_ACQUIRED && !s_loan && s_releases == 1U);
	assert(frame.crc32 == e6_jpeg_frames[0].crc32 && frame.length == e6_jpeg_frames[0].length);
	assert(!memcmp(frame.data, e6_jpeg_frames[0].data, frame.length));
	Ready(&e6_jpeg_frames[1], false);
	for (i = 0; i < 10000U; ++i)
		E7Camera_Poll(&provider, i);
	assert(s_process == 10000U);
	assert(E7Camera_Acquire(&provider, &frame, &old) == ETH_MJPEG_UNAVAILABLE);
	assert(!memcmp(frame.data, e6_jpeg_frames[0].data, frame.length));
	old = lease;
	E7Camera_Release(&provider, lease);
	E7Camera_Release(&provider, lease);
	assert(E7Camera_Acquire(&provider, &frame, &lease) == ETH_MJPEG_ACQUIRED && old != lease);
	E7Camera_Release(&provider, old);
	assert(provider.pinned && provider.stale_releases == 2U);
	assert(E7Camera_Stop(&provider) == SYS_OK);
	assert(E7Camera_Start(&provider) == SYS_ERR_BUSY);
	E7Camera_Release(&provider, lease);
	assert(E7Camera_Start(&provider) == SYS_OK);
	Ready(&e6_jpeg_frames[2], false);
	s_ready.width++;
	assert(E7Camera_Acquire(&provider, &frame, &lease) == ETH_MJPEG_INVALID && !s_loan);
	Ready(&e6_jpeg_frames[2], false);
	s_ready.size_bytes = UINT16_MAX + 1U;
	assert(E7Camera_Acquire(&provider, &frame, &lease) == ETH_MJPEG_INVALID && provider.oversize == 1U && !s_loan);
	Ready(&e6_jpeg_frames[2], true);
	s_ready.size_bytes2++;
	assert(E7Camera_Acquire(&provider, &frame, &lease) == ETH_MJPEG_INVALID && !s_loan);
	Ready(&e6_jpeg_frames[2], false);
	s_fifo[0] = 0;
	assert(E7Camera_Acquire(&provider, &frame, &lease) == ETH_MJPEG_INVALID && !s_loan);
	Ready(&e6_jpeg_frames[2], false);
	s_fifo[e6_jpeg_frames[2].length - 1U] = 0;
	assert(E7Camera_Acquire(&provider, &frame, &lease) == ETH_MJPEG_INVALID && !s_loan);
	s_capture_result = SYS_ERR_IO;
	E7Camera_Poll(&provider, 10001U);
	assert(!provider.running && provider.media_result == SYS_ERR_IO);
	s_stop_result = SYS_ERR_TIMEOUT;
	assert(E7Camera_Stop(&provider) == SYS_ERR_TIMEOUT && E7Camera_Start(&provider) == SYS_ERR_BUSY);
	s_stop_result = s_capture_result = SYS_OK;
	assert(E7Camera_Stop(&provider) == SYS_OK && E7Camera_Start(&provider) == SYS_OK);
	Ready(&e6_jpeg_frames[3], false);
	s_release_result = SYS_ERR_INVALID_STATE;
	assert(E7Camera_Acquire(&provider, &frame, &lease) == ETH_MJPEG_INVALID);
	assert(!provider.running && s_loan && !provider.pinned && provider.release_errors == 1U);
	assert(E7Camera_Stop(&provider) == SYS_ERR_BUSY && E7Camera_Start(&provider) == SYS_ERR_BUSY);
	puts("PASS: E7 JPEG wrap/CRC/bounds, private lease, fast producer, camera failure and Stop quarantine");
	return 0;
}
