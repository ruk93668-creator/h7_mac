#ifndef ETH_MJPEG_H
#define ETH_MJPEG_H

#include "eth_tcp.h"

typedef struct
{
    //有效的完整JPEG数据
    const uint8_t *data;
    //JPEG有效字节数
    uint16_t length;
    //该帧有效数据的CRC32
    uint32_t crc32;
} EthMjpegFrame_t;

typedef enum
{
    ETH_MJPEG_ACQUIRED,
    ETH_MJPEG_UNAVAILABLE,
    ETH_MJPEG_INVALID
} EthMjpegAcquire_t;

typedef struct
{
    void *context;
    EthMjpegAcquire_t (*acquire)(void *context, EthMjpegFrame_t *frame, uint32_t *lease);
    void (*release)(void *context, uint32_t lease);
} EthMjpegSource_t;

typedef enum
{
    ETH_MJPEG_IDLE,
    ETH_MJPEG_RESPONSE_HEADER,
    ETH_MJPEG_FRAME_HEADER,
    ETH_MJPEG_FRAME_DATA,
    ETH_MJPEG_FRAME_TRAILER,
    ETH_MJPEG_WAIT_NEXT_FRAME,
    ETH_MJPEG_STOPPING,
    ETH_MJPEG_CLOSED,
    ETH_MJPEG_ERROR
} EthMjpegState_t;

typedef struct
{
    //完整提交到TCP发送环的帧数
    uint32_t frames_submitted;
    //收到整帧累计TCP确认的帧数
    uint32_t frames_completed;
    uint32_t bytes_submitted;
    uint32_t send_busy;
    uint32_t send_errors;
    //因调度滞后或无可用帧跳过的次数
    uint32_t frame_skipped;
    //已确认帧率乘以1000
    uint32_t current_fps_milli;
    //从选帧到整帧确认的最大耗时，单位ms
    uint32_t max_send_latency_ms;
} EthMjpegStats_t;

typedef struct
{
    EthMjpegState_t state;
    EthMjpegStats_t stats;
    const EthMjpegFrame_t *frames;
    const EthMjpegSource_t *source;
    EthMjpegFrame_t current;
    uint32_t lease;
    uint32_t ack_base;
    uint32_t submitted;
    uint32_t frame_end;
    uint32_t frame_started;
    uint32_t next_frame;
    uint32_t fps_started;
    uint32_t fps_frames;
    uint32_t last_progress;
    uint32_t last_ack;
    uint32_t sequence;
    uint32_t retry_due;
    uint16_t count;
    uint16_t period_ms;
    uint16_t offset;
    uint16_t header_length;
    //已提交整帧，等待帧尾字节确认
    uint8_t waiting_ack;
    //当前仍持有私有帧租约
    uint8_t borrowed;
    char header[128];
} EthMjpeg_t;

//初次调用前清零 stream，由 HTTP 所在的同一上下文串行调用。
//period_ms 必须非零；轮询选帧前须提供常驻的有效帧数组和非零 count，或绑定动态帧源。
//ack_base 取当前连接已确认字节数，作为本轮视频响应的累计确认基准。
void eth_mjpeg_start(
    EthMjpeg_t *stream,
    const EthMjpegFrame_t *frames,
    uint16_t count,
    uint16_t period_ms,
    uint32_t now,
    uint32_t ack_base
);

//更新累计确认字节，确认整帧后增加完成计数
void eth_mjpeg_ack(EthMjpeg_t *stream, uint32_t now, uint32_t total_acked);

//按配置周期推进分段提交，队列不足时保留当前偏移
void eth_mjpeg_poll(EthMjpeg_t *stream, EthTcpHandle_t handle, uint32_t now);

//绑定动态帧源，源和上下文须在连接存续期间有效
void eth_mjpeg_set_source(EthMjpeg_t *stream, const EthMjpegSource_t *source);

//归还当前私有帧租约，允许提供器复用缓冲
void eth_mjpeg_release_frame(EthMjpeg_t *stream);

#endif
