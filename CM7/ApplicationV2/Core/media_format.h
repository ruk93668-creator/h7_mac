/* Neutral immutable media values. Layout validation proves no hardware support.
 * Rates describe sink targets; they do not configure or measure camera capture.
 * JPEG_FRAMES is the MJPEG frame representation, not a fifth sensor pixel code.
 */
#ifndef V2_MEDIA_FORMAT_H
#define V2_MEDIA_FORMAT_H
#include "v2_types.h"
enum
{
    CORE_MEDIA_VIDEO = 1,
    CORE_MEDIA_METADATA = 2
};
enum
{
    CORE_PIXEL_Y8 = 1,
    CORE_PIXEL_RGB565 = 2,
    CORE_PIXEL_YUY2 = 3,
    CORE_PIXEL_JPEG = 4
};
enum
{
    CORE_BACKEND_RAW = 1,
    CORE_BACKEND_JPEG = 2
};
enum
{
    CORE_STREAM_RAW_FRAMES = 1,
    CORE_STREAM_JPEG_FRAMES = 2
};
typedef struct CoreMediaFormat
{
    uint32_t media_type; /* 单一媒体码，不是mask */
    uint32_t pixel_format;
    uint32_t stream_kind;
    uint32_t width, height;
    uint32_t bytes_per_line;
    uint32_t size_image;
    uint32_t max_payload;
    uint32_t plane_count;
    uint32_t max_spans;
    uint32_t backend;
    uint32_t dma_alignment;   /* DMA目标要求 */
    uint32_t frame_alignment; /* 完整Frame地址要求，JPEG可为1 */
    uint32_t flags;           /* R2要求0，拒绝未知位 */
} CoreMediaFormat;
typedef struct
{
    uint32_t num, den;
} CoreFrameRate;
SysResult_t CoreMedia_Validate(const CoreMediaFormat *format);
SysResult_t CoreRate_Validate(const CoreFrameRate *rate);
#endif
