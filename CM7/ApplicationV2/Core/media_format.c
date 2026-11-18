// 校验中立格式的帧布局、容量和帧率。

#include "media_format.h"
// 检查帧率表示的分子、分母和取值范围。
SysResult_t CoreRate_Validate(const CoreFrameRate *frame_rate)
{
    if (!frame_rate || !frame_rate->num || !frame_rate->den)
        return SYS_ERR_INVALID_ARG;
    return SYS_OK;
}
// 验证中立媒体格式、帧布局和容量。
SysResult_t CoreMedia_Validate(const CoreMediaFormat *format)
{
    uint32_t bpp, row;
    if (!format || format->media_type != CORE_MEDIA_VIDEO || !format->width || !format->height || format->width > 65535U ||
        format->height > 65535U || format->flags || format->plane_count != 1U || !format->dma_alignment ||
        (format->dma_alignment & (format->dma_alignment - 1U)) || !format->frame_alignment ||
        (format->frame_alignment & (format->frame_alignment - 1U)))
        return SYS_ERR_INVALID_ARG;
    if (format->pixel_format == CORE_PIXEL_JPEG)
    {
        return format->backend == CORE_BACKEND_JPEG && format->stream_kind == CORE_STREAM_JPEG_FRAMES &&
                       !format->bytes_per_line && format->size_image >= 4U &&
                       format->max_payload == format->size_image && format->max_spans == 2U
                   ? SYS_OK
                   : SYS_ERR_INVALID_ARG;
    }
    if (format->pixel_format < CORE_PIXEL_Y8 || format->pixel_format > CORE_PIXEL_YUY2)
        return SYS_ERR_NOT_SUPPORTED;
    if (format->pixel_format == CORE_PIXEL_YUY2 && (format->width & 1U))
        return SYS_ERR_INVALID_ARG;
    bpp = format->pixel_format == CORE_PIXEL_Y8 ? 1U : 2U;
    row = format->width * bpp;
    if (format->height > UINT32_MAX / row)
        return SYS_ERR_INVALID_ARG;
    return format->backend == CORE_BACKEND_RAW && format->stream_kind == CORE_STREAM_RAW_FRAMES &&
                   format->bytes_per_line == row && format->size_image == row * format->height &&
                   format->max_payload == format->size_image && format->max_spans == 1U
               ? SYS_OK
               : SYS_ERR_INVALID_ARG;
}
