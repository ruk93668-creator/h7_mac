//将 JPEG 分段复制到 TCP 发送环，以累计 ACK 统计整帧完成。
#include "eth_mjpeg.h"
#include <stdio.h>
#include <string.h>

enum
{
    MJPEG_CHUNK = 1024,
    MJPEG_STALL_MS = 15000
};

static const char s_response[] = "HTTP/1.1 200 OK\r\n"
    "Content-Type: multipart/x-mixed-replace; boundary=h7frame\r\n"
    "Cache-Control: no-cache, no-store\r\nPragma: no-cache\r\nConnection: close\r\n\r\n";

void eth_mjpeg_start(EthMjpeg_t *stream, const EthMjpegFrame_t *frames, uint16_t count,
    uint16_t period_ms, uint32_t now, uint32_t ack_base)
{
    EthMjpegStats_t stats = stream->stats;
    eth_mjpeg_release_frame(stream);
    memset(stream, 0, sizeof(*stream));
    stream->stats = stats;
    stream->stats.current_fps_milli = 0U;
    stream->frames = frames;
    stream->count = count;
    stream->period_ms = period_ms;
    stream->state = ETH_MJPEG_RESPONSE_HEADER;
    stream->ack_base = ack_base;
    stream->last_progress = now;
    stream->retry_due = now;
    stream->fps_started = now;
}

void eth_mjpeg_set_source(EthMjpeg_t *stream, const EthMjpegSource_t *source)
{
    stream->source = source;
}

void eth_mjpeg_release_frame(EthMjpeg_t *stream)
{
    if (stream->borrowed)
    {
        stream->borrowed = 0U;
        stream->source->release(stream->source->context, stream->lease);
    }
}

void eth_mjpeg_ack(EthMjpeg_t *stream, uint32_t now, uint32_t total_acked)
{
    uint32_t ack = total_acked - stream->ack_base;
    uint32_t elapsed;
    if (ack != stream->last_ack)
    {
        stream->last_ack = ack;
        stream->last_progress = now;
    }
    //累计 ACK 到达帧尾才记为完成，使用模 2^32 的顺序比较。
    if (stream->waiting_ack &&
        (uint32_t)(ack - stream->frame_end) < UINT32_C(0x80000000))
    {
        elapsed = now - stream->frame_started;
        if (elapsed > stream->stats.max_send_latency_ms)
            stream->stats.max_send_latency_ms = elapsed;
        ++stream->stats.frames_completed;
        ++stream->fps_frames;
        stream->waiting_ack = 0U;
    }
    elapsed = now - stream->fps_started;
    if (elapsed >= 1000U)
    {
        stream->stats.current_fps_milli = stream->fps_frames * UINT32_C(1000000) / elapsed;
        stream->fps_started = now;
        stream->fps_frames = 0U;
    }
}

//到达选帧时间后借用完整 JPEG，并准备当前帧的分段头。
static void EthMjpeg_BeginFrame(EthMjpeg_t *stream, uint32_t now)
{
    const EthMjpegFrame_t *frame = &stream->current;
    uint32_t skipped = (now - stream->next_frame) / stream->period_ms;
    int length;
    stream->stats.frame_skipped += skipped;
    stream->next_frame += (skipped + 1U) * stream->period_ms;
    if (stream->source)
    {
        EthMjpegAcquire_t result = stream->source->acquire(
            stream->source->context, &stream->current, &stream->lease);
        if (result != ETH_MJPEG_ACQUIRED)
        {
            ++stream->stats.frame_skipped;
            if (result == ETH_MJPEG_INVALID)
                ++stream->stats.send_errors;
            return;
        }
        stream->borrowed = 1U;
        if (!frame->data || frame->length < 4U)
        {
            eth_mjpeg_release_frame(stream);
            ++stream->stats.send_errors;
            ++stream->stats.frame_skipped;
            return;
        }
    }
    else
        stream->current = stream->frames[stream->sequence % stream->count];
    stream->last_progress = now;
    stream->frame_started = now;
    length = snprintf(stream->header, sizeof(stream->header),
        "--h7frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n"
        "X-Frame-Sequence: %lu\r\nX-Frame-CRC32: %08lX\r\n\r\n",
        (unsigned)frame->length, (unsigned long)(stream->sequence + 1U), (unsigned long)frame->crc32);
    if (length < 0 || (size_t)length >= sizeof(stream->header))
    {
        ++stream->stats.send_errors;
        stream->state = ETH_MJPEG_ERROR;
        return;
    }
    stream->header_length = (uint16_t)length;
    stream->state = ETH_MJPEG_FRAME_HEADER;
}

void eth_mjpeg_poll(EthMjpeg_t *stream, EthTcpHandle_t handle, uint32_t now)
{
    const void *data;
    uint16_t length;
    uint16_t amount;
    err_t result;
    if ((uint32_t)(now - stream->last_progress) >= MJPEG_STALL_MS &&
        (stream->waiting_ack || stream->state != ETH_MJPEG_WAIT_NEXT_FRAME))
    {
        ++stream->stats.send_errors;
        stream->state = ETH_MJPEG_ERROR;
        return;
    }
    switch (stream->state)
    {
    case ETH_MJPEG_RESPONSE_HEADER:
        data = s_response;
        length = sizeof(s_response) - 1U;
        break;
    case ETH_MJPEG_FRAME_HEADER:
        data = stream->header;
        length = stream->header_length;
        break;
    case ETH_MJPEG_FRAME_DATA:
        data = stream->current.data;
        length = stream->current.length;
        break;
    case ETH_MJPEG_FRAME_TRAILER:
        data = "\r\n";
        length = 2U;
        break;
    case ETH_MJPEG_WAIT_NEXT_FRAME:
        if (!stream->waiting_ack && (uint32_t)(now - stream->next_frame) < UINT32_C(0x80000000))
            EthMjpeg_BeginFrame(stream, now);
        return;
    default:
        return;
    }
    amount = (uint16_t)(length - stream->offset);
    if ((uint32_t)(now - stream->retry_due) >= UINT32_C(0x80000000))
        return;
    if (amount > MJPEG_CHUNK)
        amount = MJPEG_CHUNK;
    result = eth_tcp_send(handle, (const uint8_t *)data + stream->offset, amount);
    //发送环不足时保留当前位置和租约，等待下一次尝试。
    if (result == ERR_MEM)
    {
        ++stream->stats.send_busy;
        stream->retry_due = now + 10U;
        return;
    }
    if (result != ERR_OK)
    {
        ++stream->stats.send_errors;
        stream->state = ETH_MJPEG_ERROR;
        return;
    }
    stream->last_progress = now;
    stream->retry_due = now;
    stream->stats.bytes_submitted += amount;
    stream->submitted += amount;
    stream->offset += amount;
    if (stream->offset != length)
        return;
    stream->offset = 0U;
    if (stream->state == ETH_MJPEG_RESPONSE_HEADER)
    {
        stream->next_frame = now;
        stream->state = ETH_MJPEG_WAIT_NEXT_FRAME;
    }
    else if (stream->state == ETH_MJPEG_FRAME_TRAILER)
    {
        ++stream->sequence;
        ++stream->stats.frames_submitted;
        stream->frame_end = stream->submitted;
        stream->waiting_ack = 1U;
        stream->state = ETH_MJPEG_WAIT_NEXT_FRAME;
    }
    else if (stream->state == ETH_MJPEG_FRAME_HEADER)
        stream->state = ETH_MJPEG_FRAME_DATA;
    else
    {
        //JPEG 数据已全部复制，尾分隔符及后续 ACK 不再引用帧缓冲。
        eth_mjpeg_release_frame(stream);
        stream->state = ETH_MJPEG_FRAME_TRAILER;
    }
}
