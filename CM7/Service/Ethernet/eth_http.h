//HTTP 服务的初始化、轮询和状态查询在同一 lwIP Raw API 上下文串行执行。
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

//借用已初始化的 network 和静态帧表，表及数据须在整个服务期间有效。
//周期单位 ms。ERR_OK 表示接受配置，监听由 Transport 轮询建立。
err_t eth_http_init(
    EthNetwork_t *network,
    const EthMjpegFrame_t *frames,
    uint16_t frame_count,
    uint16_t frame_period_ms
);

//在 Network 和 Transport 轮询后推进请求解析、响应提交和清理。
void eth_http_poll(void);

//复制 source 回调表并借用 context。周期与返回值含义同静态初始化。
err_t eth_http_init_source(
    EthNetwork_t *network,
    const EthMjpegSource_t *source,
    uint16_t frame_period_ms
);

//初始化后设置，供后续MJPEG连接选择提交节奏
#if defined(ETH_MJPEG_FAST_SEND_TEST)
void eth_http_set_stream_fast_send(int enabled);
#endif

//在服务上下文读取请求和流的计数快照，空输出指针无操作。
void eth_http_get_statistics(EthHttpStats_t *stats);

//关闭新帧准入并归还当前 JPEG 私有租约，Transport 负责连接排空。
void eth_http_begin_stop(void);
//Transport 全部退休后解除旧服务配置；否则返回 ERR_INPROGRESS。
err_t eth_http_finish_stop(void);

//唯一 Raw API 拥有者读取：只有实际打开的 MJPEG 连接才返回 1。
//用于撤销已断开客户端的帧需求，不借出连接或帧指针。
int eth_http_stream_active(void);

#endif
