// 推进 Storage 帧交付、写入、归还和收据确认。

#include "storage_frame.h"
#include <string.h>

// 匹配完整的请求或收据身份，拒绝旧交接污染当前帧。
static int receipt_matches(const VStorageFrame *frame, const MRReceipt *receipt, int physical)
{
    const MRRequest *request = &frame->current.request;
    const MRMedia empty = {0};
    return receipt->serial && receipt->result <= SYS_OK && MRGrant_Equal(&receipt->g, &request->g) &&
           receipt->h.session == request->h.session && receipt->h.sequence == request->h.sequence && !receipt->bridge.epoch &&
           !receipt->bridge.command && !receipt->bridge.lease &&
           (physical ? receipt->physical_returned == 1U && MRMedia_Equal(&receipt->m, &frame->current.media)
                     : !receipt->physical_returned && MRMedia_Equal(&receipt->m, &empty));
}

// 接收 Storage 交付通知并核对槽位和媒体身份。
static SysResult_t receive_offer(VStorageFrame *frame, const VStorageFrameOps *ops, void *port_context)
{
    VStorageOffer offer;
    SysResult_t result = ops->offer(port_context, &offer);
    if (result)
        return result;
    if (!MRGrant_Equal(&offer.request.g, &frame->grant) || memcmp(&offer.request.s, &frame->slot, sizeof(frame->slot)))
        return SYS_ERR_NOT_FOUND; /* 迟到槽位/代次，不改当前工作。 */
    if (offer.kind == VSF_DELIVERY_WITHDRAWN && frame->withdrawn && !offer.request.h.session &&
        !offer.request.h.sequence && !offer.request.b.epoch && !offer.request.b.command &&
        !offer.request.b.lease && !offer.media.id && !offer.terminal.serial)
    {
        frame->state = VSF_FREE;
        return SYS_OK;
    }
    if (!offer.request.h.sequence || offer.request.h.sequence <= frame->highest_h ||
        offer.request.h.session != frame->grant.session || offer.request.b.epoch || offer.request.b.command ||
        offer.request.b.lease)
    {
        frame->state = VSF_BLOCKED;
        return SYS_ERR_PROTOCOL;
    }
    frame->current = offer;
    frame->highest_h = offer.request.h.sequence;
    if (offer.kind == VSF_DELIVERY_TERMINAL && receipt_matches(frame, &offer.terminal, 0))
    {
        frame->terminal = offer.terminal;
        frame->state = VSF_CONSUME;
        return offer.terminal.result;
    }
    if (offer.kind != VSF_DELIVERY_FRAME || offer.media.backend != MR_JPEG || !offer.media.session ||
        !offer.media.id || !offer.media.generation)
    {
        frame->state = VSF_BLOCKED;
        return SYS_ERR_PROTOCOL;
    }
    /* 先保留真实M。JPEG结构或大小错误由写入端报告后走正常Return。 */
    frame->state = VSF_READY;
    return SYS_OK;
}

// 推进 Storage 的帧写入和真实归还流程。
SysResult_t VStorageFrame_Process(VStorageFrame *frame, const VStorageFrameOps *ops, void *port_context,
                                  const MRGrant *grant, int accepting, int quiet)
{
    MRReceipt receipt;
    SysResult_t result;
    switch (frame->state)
    {
    case VSF_FREE:
        if (!accepting || !grant->session)
            return SYS_OK;
        result = MRNumber_Advance(&frame->generation);
        if (result)
            return result;
        frame->grant = *grant;
        frame->slot = (MRSlot){grant->session, 1U, frame->generation};
        frame->withdrawn = 0U;
        frame->state = VSF_POST;
        return SYS_OK;
    case VSF_POST:
        if (!accepting)
        {
            frame->state = VSF_FREE; /* 尚未入队。同一S永不复用。 */
            return SYS_OK;
        }
        result = ops->post(port_context, &frame->grant, &frame->slot);
        if (!result)
            frame->state = VSF_WAIT;
        return result;
    case VSF_WAIT:
        if (!accepting && !frame->withdrawn)
        {
            result = ops->withdraw(port_context, &frame->grant, &frame->slot);
            if (!result)
                frame->withdrawn = 1U;
            else if (result != SYS_ERR_BUSY && result != SYS_ERR_NO_RESOURCE)
                return result;
            /* 撤回背压时仍消费已经送达的Offer，以免双向互等。 */
        }
        return receive_offer(frame, ops, port_context);
    case VSF_READY:
        return SYS_OK; /* Storage业务处理完成/拒绝后显式进入RETURN。 */
    case VSF_RETURN:
        if (!quiet)
            return SYS_ERR_BUSY;
        result = ops->return_frame(port_context, &frame->current.request, &frame->current.media);
        if (!result)
        {
            memset(&frame->current.view, 0, sizeof(frame->current.view));
            frame->state = VSF_ACK;
        }
        return result;
    case VSF_ACK:
        result = ops->receipt(port_context, &receipt);
        if (result)
            return result;
        if (!receipt_matches(frame, &receipt, 1))
            return SYS_ERR_NOT_FOUND;
        frame->terminal = receipt;
        frame->state = VSF_CONSUME;
        return receipt.result;
    case VSF_CONSUME:
        result = ops->consume(port_context, &frame->terminal);
        if (!result)
            frame->state = VSF_RETIRE;
        return result;
    case VSF_RETIRE:
        result = ops->retired(port_context, &frame->terminal);
        if (!result)
        {
            memset(&frame->current, 0, sizeof(frame->current));
            memset(&frame->terminal, 0, sizeof(frame->terminal));
            frame->state = VSF_FREE;
        }
        return result;
    default:
        return SYS_ERR_PROTOCOL;
    }
}
