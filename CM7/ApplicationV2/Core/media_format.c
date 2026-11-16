#include "media_format.h"
SysResult_t CoreRate_Validate(const CoreFrameRate *r)
{
    if (!r || !r->num || !r->den)
        return SYS_ERR_INVALID_ARG;
    return SYS_OK;
}
SysResult_t CoreMedia_Validate(const CoreMediaFormat *f)
{
    uint32_t bpp, row;
    if (!f || f->media_type != CORE_MEDIA_VIDEO || !f->width || !f->height || f->width > 65535U ||
        f->height > 65535U || f->flags || f->plane_count != 1U || !f->dma_alignment ||
        (f->dma_alignment & (f->dma_alignment - 1U)) || !f->frame_alignment ||
        (f->frame_alignment & (f->frame_alignment - 1U)))
        return SYS_ERR_INVALID_ARG;
    if (f->pixel_format == CORE_PIXEL_JPEG)
    {
        return f->backend == CORE_BACKEND_JPEG && f->stream_kind == CORE_STREAM_JPEG_FRAMES &&
                       !f->bytes_per_line && f->size_image >= 4U &&
                       f->max_payload == f->size_image && f->max_spans == 2U
                   ? SYS_OK
                   : SYS_ERR_INVALID_ARG;
    }
    if (f->pixel_format < CORE_PIXEL_Y8 || f->pixel_format > CORE_PIXEL_YUY2)
        return SYS_ERR_NOT_SUPPORTED;
    if (f->pixel_format == CORE_PIXEL_YUY2 && (f->width & 1U))
        return SYS_ERR_INVALID_ARG;
    bpp = f->pixel_format == CORE_PIXEL_Y8 ? 1U : 2U;
    row = f->width * bpp;
    if (f->height > UINT32_MAX / row)
        return SYS_ERR_INVALID_ARG;
    return f->backend == CORE_BACKEND_RAW && f->stream_kind == CORE_STREAM_RAW_FRAMES &&
                   f->bytes_per_line == row && f->size_image == row * f->height &&
                   f->max_payload == f->size_image && f->max_spans == 1U
               ? SYS_OK
               : SYS_ERR_INVALID_ARG;
}
