// 推进 USB 帧交接、异步传输、归还与 ACK 消费。

#include "usb_frame.h"
#include <string.h>
#define MAGIC 0x55465235U
// 校验所有者调用并设置重入保护，调用方仍负责跨任务串行化。
static SysResult_t enter(VUSBFrame *frame)
{
    if (!frame || frame->magic != MAGIC || !frame->ops->is_owner(frame->port))
        return SYS_ERR_INVALID_STATE;
    if (frame->busy)
        return SYS_ERR_BUSY;
    frame->busy = 1;
    return 0;
}
// 退出当前调用的重入保护。
static SysResult_t leave(VUSBFrame *frame, SysResult_t result)
{
    frame->busy = 0;
    return result;
}
// 将本轮观察到的故障记录到模块上下文。
static void fault(VUSBFrame *frame, SysResult_t result)
{
    if (result != SYS_ERR_BUSY && result != SYS_ERR_NO_RESOURCE && result != SYS_ERR_NOT_FOUND && result &&
        !frame->first_error)
        frame->first_error = result;
}
// 检查 USB 交接是否仍有活动传输、Return 或 ACK。
int VUSBFrame_IsIdle(const VUSBFrame *frame)
{
    return frame && frame->magic == MAGIC && frame->ops->is_owner(frame->port) && frame->phase == VUF_IDLE;
}
// 绑定 USB 帧交接所有者及数据面端口。
SysResult_t VUSBFrame_Init(VUSBFrame *frame, const VUSBFrameOps *frame_ops, void *port_context)
{
    if (!frame || frame->magic || !frame_ops || !frame_ops->is_owner || !frame_ops->prepare || !frame_ops->prepared || !frame_ops->commit ||
        !frame_ops->cancel || !frame_ops->cancelled || !frame_ops->offer || !frame_ops->submit_uvc || !frame_ops->readers_safe ||
        !frame_ops->return_frame || !frame_ops->return_ack || !frame_ops->consumed || !frame_ops->retired)
        return SYS_ERR_INVALID_ARG;
    if (!frame_ops->is_owner(port_context))
        return SYS_ERR_INVALID_STATE;
    memset(frame, 0, sizeof(*frame));
    frame->ops = frame_ops;
    frame->port = port_context;
    frame->magic = MAGIC;
    return 0;
}
// 匹配完整的请求或收据身份，拒绝旧交接污染当前帧。
static int request_equal(const MRRequest *left_request, const MRRequest *right_request)
{
    return !memcmp(left_request, right_request, sizeof(*left_request));
}
// 匹配完整的请求或收据身份，拒绝旧交接污染当前帧。
static int terminal_matches(VUSBFrame *frame, const MRReceipt *receipt, int physical)
{
    return MRGrant_Equal(&frame->request.g, &receipt->g) && !memcmp(&frame->request.h, &receipt->h, sizeof(receipt->h)) &&
           receipt->serial && receipt->result <= 0 &&
           (physical
                ? (receipt->physical_returned == 1 && MRMedia_Equal(&frame->data.media, &receipt->m) &&
                   !memcmp(&frame->data.bridge, &receipt->bridge, sizeof(receipt->bridge)))
                : (!receipt->physical_returned && !receipt->m.backend && !receipt->m.session && !receipt->m.id &&
                   !receipt->m.generation && !receipt->bridge.epoch && !receipt->bridge.lease && !receipt->bridge.command));
}
// 检查交付帧的身份和数据跨度。
static int view_valid(const MRView *view, const CoreMediaFormat *fmt)
{
    uint32_t total;
    if (!view->data || !view->bytes || (view->bytes2 && !view->data2) || view->bytes > UINT32_MAX - view->bytes2)
        return 0;
    total = view->bytes + view->bytes2;
    return total <= fmt->max_payload &&
           (fmt->backend == CORE_BACKEND_JPEG ? total >= 4
                                              : !view->bytes2 && total == fmt->size_image);
}
// 推进传输与归还；匹配 ACK 被消费前保留交接身份。
SysResult_t VUSBFrame_Process(VUSBFrame *frame, const MRGrant *grant, const CoreMediaFormat *fmt,
                              int accepting)
{
    SysResult_t result = enter(frame);
    VUSBDelivery delivery;
    MRRequest request;
    MRReceipt receipt;
    if (result)
        return result;
    if (!grant || !fmt)
        return leave(frame, SYS_ERR_INVALID_ARG);
    if (frame->phase == VUF_BLOCKED)
        return leave(frame, frame->first_error ? frame->first_error : SYS_ERR_PROTOCOL);
    switch (frame->phase)
    {
    case VUF_IDLE:
        if (!accepting || !grant->session)
            return leave(frame, 0);
        result = frame->ops->prepare(frame->port, grant);
        if (!result)
        {
            frame->preparing = *grant;
            frame->phase = VUF_PREPARING;
        }
        break;
    case VUF_PREPARING:
        result = frame->ops->prepared(frame->port, &request);
        if (!result)
        {
            if (!MRGrant_Equal(&request.g, &frame->preparing) ||
                (request.h.sequence && request.h.sequence <= frame->highest_h))
                return leave(frame, SYS_ERR_NOT_FOUND);
            if (!request.h.sequence || request.h.session != request.g.session || !request.b.epoch || !request.b.command ||
                request.b.lease)
            {
                frame->phase = VUF_BLOCKED;
                result = SYS_ERR_PROTOCOL;
                break;
            }
            frame->request = request;
            frame->highest_h = request.h.sequence;
            frame->phase = VUF_PREPARED;
        }
        break;
    case VUF_PREPARED:
        if (!accepting)
        {
            result = frame->ops->cancel(frame->port, &frame->request);
            if (!result)
                frame->phase = VUF_CANCELLING;
        }
        else
        {
            result = frame->ops->commit(frame->port, &frame->request);
            if (!result)
                frame->phase = VUF_COMMITTED;
        }
        break;
    case VUF_CANCELLING:
        result = frame->ops->cancelled(frame->port, &frame->request);
        if (!result)
            frame->phase = VUF_IDLE;
        break;
    case VUF_COMMITTED:
        result = frame->ops->offer(frame->port, &delivery);
        if (!result)
        {
            if (!request_equal(&delivery.request, &frame->request))
                return leave(frame, SYS_ERR_NOT_FOUND);
            if (delivery.kind == VUF_TERMINAL && terminal_matches(frame, &delivery.terminal, 0))
            {
                frame->terminal = delivery.terminal;
                fault(frame, delivery.terminal.result);
                result = delivery.terminal.result;
                frame->phase = VUF_CONSUMING;
                break;
            }
            if (delivery.kind != VUF_FRAME || delivery.data.media.backend != fmt->backend ||
                !delivery.data.media.session || !delivery.data.media.id || !delivery.data.media.generation ||
                !delivery.data.bridge.lease || delivery.data.bridge.epoch != frame->request.b.epoch ||
                delivery.data.bridge.command != frame->request.b.command || !view_valid(&delivery.data.view, fmt))
            {
                frame->phase = VUF_BLOCKED;
                result = SYS_ERR_PROTOCOL;
                break;
            }
            frame->data = delivery.data;
            frame->phase = VUF_OFFERED;
        }
        break;
    case VUF_OFFERED:
        if (!accepting)
        {
            frame->phase = VUF_RETURNING;
            return leave(frame, 0);
        }
        result = frame->ops->submit_uvc(frame->port, &frame->request, &frame->data);
        if (!result)
            frame->phase = VUF_IN_UVC;
        else if (result != SYS_ERR_BUSY && result != SYS_ERR_NO_RESOURCE)
            frame->phase = VUF_RETURNING;
        break;
    case VUF_IN_UVC:
        result = frame->ops->readers_safe(frame->port, &frame->request, &frame->data);
        if (!result)
            frame->phase = VUF_RETURNING;
        break;
    case VUF_RETURNING:
        /* 第二次核验覆盖未提交UVC及多个异步CPU/DMA读取者。 */
        result = frame->ops->readers_safe(frame->port, &frame->request, &frame->data);
        if (result)
            break;
        result = frame->ops->return_frame(frame->port, &frame->request, &frame->data);
        if (!result)
            frame->phase = VUF_WAIT_ACK;
        break;
    case VUF_WAIT_ACK:
        result = frame->ops->return_ack(frame->port, &receipt);
        if (!result)
        {
            if (!terminal_matches(frame, &receipt, 1))
                return leave(frame, SYS_ERR_NOT_FOUND);
            frame->terminal = receipt;
            fault(frame, receipt.result);
            result = receipt.result;
            frame->phase = VUF_CONSUMING;
        }
        break;
    case VUF_CONSUMING:
        result = frame->ops->consumed(frame->port, &frame->terminal);
        if (!result)
            frame->phase = VUF_RETIRING;
        break;
    case VUF_RETIRING:
        result = frame->ops->retired(frame->port, &frame->terminal);
        if (!result)
        {
            /* 只退休本地元数据；不是Media Release，也不清相邻消费者。 */
            frame->phase = VUF_IDLE;
            memset(&frame->data, 0, sizeof(frame->data));
        }
        break;
    default:
        result = SYS_ERR_INVALID_STATE;
        break;
    }
    fault(frame, result);
    return leave(frame, result);
}
