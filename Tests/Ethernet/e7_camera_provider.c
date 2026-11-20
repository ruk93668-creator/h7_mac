#include "e7_camera_provider.h"
#include <string.h>

static uint32_t E7Camera_Crc(const uint8_t *data, uint32_t length)
{
	static const uint32_t table[16] = {
		0x00000000U, 0x1db71064U, 0x3b6e20c8U, 0x26d930acU,
		0x76dc4190U, 0x6b6b51f4U, 0x4db26158U, 0x5005713cU,
		0xedb88320U, 0xf00f9344U, 0xd6d6a3e8U, 0xcb61b38cU,
		0x9b64c2b0U, 0x86d3d2d4U, 0xa00ae278U, 0xbdbdf21cU
	};
	uint32_t crc = UINT32_MAX;
	while (length--)
	{
		crc ^= *data++;
		crc = table[crc & 15U] ^ (crc >> 4U);
		crc = table[crc & 15U] ^ (crc >> 4U);
	}
	return ~crc;
}

//对照模式限制单次SDRAM复制长度，原始帧的所有权贯穿整次复制
static void E7Camera_CopySpan(uint8_t *dst, const uint8_t *src, uint32_t length,
	void (*barrier)(void))
{
	if (barrier == NULL)
	{
		memcpy(dst, src, length);
		return;
	}
	while (length != 0U)
	{
		uint32_t amount = length;
		if (amount > E7_JPEG_COPY_SEGMENT_BYTES)
			amount = E7_JPEG_COPY_SEGMENT_BYTES;
		memcpy(dst, src, amount);
		barrier();
		dst += amount;
		src += amount;
		length -= amount;
	}
}

static bool E7Camera_Copy(E7CameraProvider_t *provider, const CameraFrame_t *frame, uint32_t *crc)
{
	uint32_t first, tail = frame->size_bytes2, total = frame->size_bytes;
	uintptr_t base = (uintptr_t)provider->fifo, start = (uintptr_t)frame->data;
	if (total > E7_JPEG_CAPACITY || total > UINT16_MAX)
	{
		++provider->oversize;
		return false;
	}
	if (frame->format != CAMERA_PIXEL_FORMAT_JPEG || frame->width != E7_CAMERA_WIDTH || frame->height != E7_CAMERA_HEIGHT ||
		total < 4U || tail >= total || start < base || start - base >= provider->fifo_size)
		return false;
	first = total - tail;
	if (first > provider->fifo_size - (start - base) ||
		(tail && (first != provider->fifo_size - (start - base) ||
		 frame->data2 != provider->fifo || tail > start - base)) ||
		(!tail && frame->data2))
		return false;
	E7Camera_CopySpan(provider->buffer, frame->data, first, provider->copy_barrier);
	if (tail)
		E7Camera_CopySpan(provider->buffer + first, frame->data2, tail, provider->copy_barrier);
	if (provider->buffer[0] != 0xffU || provider->buffer[1] != 0xd8U ||
		provider->buffer[total - 2U] != 0xffU || provider->buffer[total - 1U] != 0xd9U)
		return false;
	*crc = E7Camera_Crc(provider->buffer, total);
	return true;
}

EthMjpegAcquire_t E7Camera_Acquire(void *context, EthMjpegFrame_t *out, uint32_t *lease)
{
	E7CameraProvider_t *provider = context;
	CameraFrame_t frame;
	uint32_t crc = 0U;
	bool valid;
	if (!provider->running || provider->original_loan || provider->pinned || provider->lease == UINT32_MAX ||
		!CameraCapture_StreamGetFrame(&frame))
	{
		++provider->no_frame;
		return ETH_MJPEG_UNAVAILABLE;
	}
	provider->original_loan = true;
	valid = E7Camera_Copy(provider, &frame, &crc);
	provider->media_result = CameraCapture_StreamReleaseFrame();
	if (provider->media_result != SYS_OK)
	{
		++provider->release_errors;
		provider->running = false;
		return ETH_MJPEG_INVALID;
	}
	provider->original_loan = false;
	if (!valid)
	{
		++provider->invalid;
		return ETH_MJPEG_INVALID;
	}
	provider->pinned = true;
	*lease = ++provider->lease;
	out->data = provider->buffer;
	out->length = (uint16_t)frame.size_bytes;
	out->crc32 = crc;
	++provider->copies;
	return ETH_MJPEG_ACQUIRED;
}

void E7Camera_Release(void *context, uint32_t lease)
{
	E7CameraProvider_t *provider = context;
	if (!provider->pinned || lease != provider->lease)
	{
		++provider->stale_releases;
		return;
	}
	provider->pinned = false;
}

SysResult_t E7Camera_Start(E7CameraProvider_t *provider)
{
	if (provider->running || provider->pinned || provider->original_loan || !CameraCapture_IsStopped())
		return SYS_ERR_BUSY;
	provider->media_result = CameraCapture_StartStream();
	provider->running = provider->media_result == SYS_OK;
	provider->last_sample = 0U;
	return provider->media_result;
}

SysResult_t E7Camera_Stop(E7CameraProvider_t *provider)
{
	provider->running = false;
	if (provider->original_loan)
		return provider->stop_result = SYS_ERR_BUSY;
	provider->stop_result = CameraCapture_Stop();
	(void)CameraCapture_GetStopFacts(&provider->stop_facts);
	if (provider->stop_result == SYS_OK &&
		(!CameraCapture_IsStopped() || provider->stop_facts.hardware_result != SYS_OK))
		provider->stop_result = SYS_ERR_INVALID_STATE;
	return provider->stop_result;
}

void E7Camera_Poll(E7CameraProvider_t *provider, uint32_t now)
{
	uint32_t elapsed = now - provider->last_sample;
	if (provider->running)
	{
		CameraCapture_Process();
		provider->media_result = CameraCapture_GetLastResult();
		if (provider->media_result != SYS_OK || CameraCapture_IsStopped())
		{
			++provider->capture_errors;
			provider->running = false;
			(void)CameraCapture_GetStopFacts(&provider->stop_facts);
		}
	}
	if (elapsed >= 1000U && CameraCapture_GetStats(&provider->capture) == SYS_OK)
	{
		provider->capture_fps_milli = provider->running && provider->last_sample &&
			provider->capture.completed_frames >= provider->last_completed ?
			(provider->capture.completed_frames - provider->last_completed) * 1000000U / elapsed : 0U;
		provider->copy_fps_milli = provider->running && provider->last_sample ?
			(provider->copies - provider->last_copies) * 1000000U / elapsed : 0U;
		provider->last_completed = provider->capture.completed_frames;
		provider->last_copies = provider->copies;
		provider->last_sample = now;
	}
}
