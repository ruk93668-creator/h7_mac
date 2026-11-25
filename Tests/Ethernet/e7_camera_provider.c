#include "e7_camera_provider.h"
#include <string.h>
#include "eth_jpeg_copy.h"

//内部功能函数声明
static bool E7Camera_Copy(
    E7CameraProvider_t *provider,
    const CameraFrame_t *frame,
    uint32_t *crc
);

//对外接口实现
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

//内部功能函数实现

//格式由 Camera 描述核对，范围与分段复制统一交给公共实现。
static bool E7Camera_Copy(E7CameraProvider_t *provider, const CameraFrame_t *frame, uint32_t *crc)
{
    EthJpegView_t view;
    if (frame->size_bytes > E7_JPEG_CAPACITY || frame->size_bytes > UINT16_MAX)
    {
        ++provider->oversize;
        return false;
    }
    if (frame->format != CAMERA_PIXEL_FORMAT_JPEG ||
        frame->width != E7_CAMERA_WIDTH || frame->height != E7_CAMERA_HEIGHT)
        return false;
    view.data = frame->data;
    view.data2 = frame->data2;
    view.length = frame->size_bytes;
    view.tail = frame->size_bytes2;
    view.fifo = provider->fifo;
    view.fifo_size = provider->fifo_size;
    return eth_jpeg_copy(&view, provider->buffer, E7_JPEG_CAPACITY,
        provider->copy_barrier, crc);
}
