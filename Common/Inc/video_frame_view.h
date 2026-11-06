#ifndef VIDEO_FRAME_VIEW_H
#define VIDEO_FRAME_VIEW_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

//一帧最多跨SDRAM FIFO末尾一次，不复制图像来拼接
#define VIDEO_FRAME_MAX_SPANS 2U

//一段只读图像内存
typedef struct
{
    const uint8_t *data;
    uint32_t length;
} VideoFrameSpan_t;

//通用分段帧视图，不依赖Camera、Media或USB
typedef struct
{
    VideoFrameSpan_t spans[VIDEO_FRAME_MAX_SPANS];
    uint32_t size_bytes;
    uint8_t span_count;
} VideoFrameView_t;

//检查段数、地址溢出和总长度，不读取图像内容
static inline bool VideoFrameView_Valid(const VideoFrameView_t *view)
{
    uint32_t i, total = 0U;

    if (view == NULL || view->span_count == 0U ||
        view->span_count > VIDEO_FRAME_MAX_SPANS || view->size_bytes == 0U)
        return false;
    for (i = 0U; i < view->span_count; i++)
    {
        const VideoFrameSpan_t *span = &view->spans[i];

        //先校验长度和地址，再进行求和及末地址计算
        if (span->data == NULL || span->length == 0U ||
            (uintptr_t)span->data > UINTPTR_MAX - (span->length - 1U) ||
            total > UINT32_MAX - span->length)
            return false;
        total += span->length;
    }
    //两个物理段不能相互覆盖，FIFO跨尾的两段应分别位于末尾和起点
    if (view->span_count == 2U)
    {
        uintptr_t first = (uintptr_t)view->spans[0].data;
        uintptr_t second = (uintptr_t)view->spans[1].data;
        if (first <= second + view->spans[1].length - 1U &&
            second <= first + view->spans[0].length - 1U)
            return false;
    }
    return total == view->size_bytes;
}

//读取已校验视图中的一个字节，调用者保证offset在有效范围内
static inline uint8_t VideoFrameView_Byte(const VideoFrameView_t *view, uint32_t offset)
{
    return offset < view->spans[0].length ? view->spans[0].data[offset] :
        view->spans[1].data[offset - view->spans[0].length];
}

#endif
