#include "network_frames.h"
#include "../../Core/app_messages.h"
#include "eth_execution.h"
#include "eth_jpeg_copy.h"
#include "camera_mjpeg_fifo.h"
#include "platform_time.h"
#include "stm32h7xx.h"

#define NETWORK_JPEG_CAPACITY 61440U
#define NETWORK_FRAME_WAIT_MS 2000U

typedef enum
{
    NETWORK_FRAME_IDLE,
    NETWORK_FRAME_WAIT_OFFER,
    NETWORK_FRAME_RETURN_PENDING,
    NETWORK_FRAME_WAIT_RETURN,
    NETWORK_FRAME_CONSUMED_PENDING
} NetworkFrameState_t;

//独立私有副本，链接器放入 AXI SRAM；相机和网口 DMA 均不访问。
static uint8_t s_jpeg[NETWORK_JPEG_CAPACITY]
    __attribute__((section(".network_jpeg"), zero_init, aligned(32)));
static struct
{
    NetworkFrameState_t state;
    //仅保留消息凭据，复制完成后清除原帧地址。
    AppMessage_t identity;
    EthMjpegFrame_t frame;
    uint32_t command;
    uint32_t lease;
    uint32_t requested;
    uint32_t invalid;
    uint32_t returned;
    bool demand;
    bool ready;
    bool pinned;
    bool copied;
    bool cancel_sent;
    bool stopping;
    bool withdrawn;//当前客户端已离开，不能把其迟到 OFFER 发布为新副本
} s_frames;

//按 E7 策略从 SDRAM FIFO 分段读取，每最多八字节执行一次 DSB。
static void NetworkFrames_Barrier(void)
{
    __DSB();
}

static EthMjpegAcquire_t NetworkFrames_Acquire(void *context,
    EthMjpegFrame_t *frame, uint32_t *lease)
{
    (void)context;
    if (!eth_execution_allowed() || frame == NULL || lease == NULL)
        return ETH_MJPEG_INVALID;
    if (s_frames.stopping || s_frames.pinned)
        return ETH_MJPEG_UNAVAILABLE;
    if (!s_frames.ready)
    {
        s_frames.demand = true;
        return ETH_MJPEG_UNAVAILABLE;
    }
    if (s_frames.lease == UINT32_MAX)
        return ETH_MJPEG_INVALID;
    s_frames.pinned = true;
    s_frames.ready = false;
    *frame = s_frames.frame;
    *lease = ++s_frames.lease;
    return ETH_MJPEG_ACQUIRED;
}

static void NetworkFrames_Release(void *context, uint32_t lease)
{
    (void)context;
    if (eth_execution_allowed() && s_frames.pinned && s_frames.lease == lease)
        s_frames.pinned = false;
}

static const EthMjpegSource_t s_source =
{
    NULL, NetworkFrames_Acquire, NetworkFrames_Release
};

const EthMjpegSource_t *NetworkFrames_Source(void)
{
    if (!eth_execution_allowed())
        return NULL;
    if (s_frames.state == NETWORK_FRAME_IDLE && !s_frames.pinned)
        s_frames.stopping = false;
    return &s_source;
}

static bool NetworkFrames_Matches(const AppMessage_t *message)
{
    return message->command_id == s_frames.identity.command_id &&
        message->stream_epoch == s_frames.identity.stream_epoch &&
        message->lease_id == s_frames.identity.lease_id;
}

static void NetworkFrames_Receive(const AppMessage_t *message)
{
    if (message->command_id != s_frames.command)
        return;
    if (s_frames.state == NETWORK_FRAME_WAIT_OFFER && message->type == APP_MESSAGE_FRAME_OFFER)
    {
        s_frames.identity = *message;
        s_frames.identity.source = RUNTIME_TARGET_NETWORK;
        s_frames.identity.result = SYS_OK;
        s_frames.copied = false;
        if (message->result == SYS_OK && message->stream_epoch != 0U && message->lease_id != 0U)
        {
            const CameraFrame_t *frame = &message->frame;
            EthJpegView_t view;
            view.data = frame->data;
            view.data2 = frame->data2;
            view.length = frame->size_bytes;
            view.tail = frame->size_bytes2;
            view.fifo = (const uint8_t *)CAMERA_MJPEG_FIFO_BASE;
            view.fifo_size = CAMERA_MJPEG_FIFO_BYTES;
            if (!s_frames.stopping && !s_frames.withdrawn && frame->format == CAMERA_PIXEL_FORMAT_JPEG &&
                eth_jpeg_copy(&view, s_jpeg, sizeof(s_jpeg),
                    NetworkFrames_Barrier, &s_frames.frame.crc32))
            {
                s_frames.frame.data = s_jpeg;
                s_frames.frame.length = (uint16_t)frame->size_bytes;
                s_frames.copied = true;
            }
            else if (!s_frames.stopping && !s_frames.withdrawn)
                ++s_frames.invalid;
            s_frames.state = NETWORK_FRAME_RETURN_PENDING;
        }
        else
            s_frames.state = NETWORK_FRAME_CONSUMED_PENDING;
        s_frames.identity.frame = (CameraFrame_t){0};
    }
    else if (s_frames.state == NETWORK_FRAME_WAIT_RETURN &&
        message->type == APP_MESSAGE_RETURN_ACK && NetworkFrames_Matches(message) &&
        message->result == SYS_OK)
    {
        ++s_frames.returned;
        s_frames.state = NETWORK_FRAME_CONSUMED_PENDING;
    }
}

void NetworkFrames_WithdrawDemand(void)
{
    if (!eth_execution_allowed())
        return;
    s_frames.demand = false;
    s_frames.ready = false;
    if (s_frames.state != NETWORK_FRAME_IDLE)
        s_frames.withdrawn = true;
}

void NetworkFrames_BeginStop(void)
{
    if (!eth_execution_allowed())
        return;
    s_frames.stopping = true;
    s_frames.demand = false;
    s_frames.ready = false;
}

bool NetworkFrames_Retired(void)
{
    return eth_execution_allowed() && s_frames.state == NETWORK_FRAME_IDLE &&
        !s_frames.pinned;
}

void NetworkFrames_Poll(bool accepting)
{
    AppMessage_t message;
    uint32_t count, now = Platform_TimeGetMs();
    if (!eth_execution_allowed())
        return;
    if (!accepting)
        NetworkFrames_BeginStop();
    for (count = 0U; count < 4U &&
        App_MessagesReceive(APP_CHANNEL_CAMERA_TO_NETWORK, &message); ++count)
        NetworkFrames_Receive(&message);

    if (s_frames.state == NETWORK_FRAME_IDLE && accepting && s_frames.demand &&
        !s_frames.pinned && !s_frames.ready && s_frames.command != UINT32_MAX)
    {
        message = (AppMessage_t){.type = APP_MESSAGE_FRAME_REQUEST,
            .source = RUNTIME_TARGET_NETWORK, .command_id = s_frames.command + 1U};
        if (App_MessagesSend(APP_CHANNEL_NETWORK_TO_CAMERA, &message) == SYS_OK)
        {
            ++s_frames.command;
            s_frames.identity = message;
            s_frames.requested = now;
            s_frames.cancel_sent = false;
            s_frames.withdrawn = false;
            s_frames.demand = false;
            s_frames.state = NETWORK_FRAME_WAIT_OFFER;
        }
    }
    else if (s_frames.state == NETWORK_FRAME_WAIT_OFFER &&
        (s_frames.stopping || s_frames.withdrawn || now - s_frames.requested >= NETWORK_FRAME_WAIT_MS) &&
        !s_frames.cancel_sent)
    {
        message = s_frames.identity;
        message.type = APP_MESSAGE_FRAME_CANCEL_REQUEST;
        if (App_MessagesSend(APP_CHANNEL_NETWORK_TO_CAMERA, &message) == SYS_OK)
            s_frames.cancel_sent = true;
    }
    else if (s_frames.state == NETWORK_FRAME_RETURN_PENDING ||
        s_frames.state == NETWORK_FRAME_CONSUMED_PENDING)
    {
        message = s_frames.identity;
        message.type = s_frames.state == NETWORK_FRAME_RETURN_PENDING ?
            APP_MESSAGE_FRAME_RETURN : APP_MESSAGE_RETURN_ACK_CONSUMED;
        if (App_MessagesSend(APP_CHANNEL_NETWORK_TO_CAMERA, &message) == SYS_OK)
        {
            if (s_frames.state == NETWORK_FRAME_RETURN_PENDING)
                s_frames.state = NETWORK_FRAME_WAIT_RETURN;
            else
            {
                s_frames.state = NETWORK_FRAME_IDLE;
                s_frames.ready = s_frames.copied && !s_frames.stopping && !s_frames.withdrawn;
                s_frames.copied = false;
                s_frames.identity = (AppMessage_t){.command_id = 0U};
            }
        }
    }
}
