#ifndef ETH_HTTP_H
#define ETH_HTTP_H

#include "eth_mjpeg.h"
#include "eth_transport.h"

#define ETH_HTTP_REQUEST_MAX 512U

typedef struct
{
    uint32_t sessions;
    uint32_t requests;
    uint32_t home;
    uint32_t health;
    uint32_t streams;
    uint32_t favicon;
    uint32_t bad_request;
    uint32_t method_rejected;
    uint32_t not_found;
    uint32_t headers_too_large;
    uint32_t request_timeout;
    uint32_t peer_fin;
    uint32_t closed;
    uint32_t connection_errors;
    //响应提交时发送环空间不足的次数
    uint32_t response_busy;
    uint32_t response_errors;
    uint32_t extra_rx;
    EthMjpegState_t stream_state;
    //MJPEG发送和整帧确认计数
    EthMjpegStats_t stream;
} EthHttpStats_t;

//使用静态JPEG帧表初始化HTTP服务，周期单位ms
err_t eth_http_init(
    EthNetwork_t *network,
    const EthMjpegFrame_t *frames,
    uint16_t frame_count,
    uint16_t frame_period_ms
);

//推进请求解析和响应，在TCP轮询后调用
void eth_http_poll(void);

//绑定动态JPEG源，发送期间按租约取得和归还帧
err_t eth_http_init_source(
    EthNetwork_t *network,
    const EthMjpegSource_t *source,
    uint16_t frame_period_ms
);

//初始化后设置，供后续MJPEG连接选择提交节奏
#if defined(ETH_MJPEG_FAST_SEND_TEST)
void eth_http_set_stream_fast_send(int enabled);
#endif

//读取HTTP请求和MJPEG发送计数快照
void eth_http_get_statistics(EthHttpStats_t *stats);

#endif
