#include "storage_frame.h"
#include <string.h>

static int receipt_matches(const VStorageFrame *s, const MRReceipt *t, int physical)
{
    const MRRequest *r = &s->current.request;
    const MRMedia empty = {0};
    return t->serial && t->result <= SYS_OK && MRGrant_Equal(&t->g, &r->g) &&
           t->h.session == r->h.session && t->h.sequence == r->h.sequence && !t->bridge.epoch &&
           !t->bridge.command && !t->bridge.lease &&
           (physical ? t->physical_returned == 1U && MRMedia_Equal(&t->m, &s->current.media)
                     : !t->physical_returned && MRMedia_Equal(&t->m, &empty));
}

static SysResult_t receive_offer(VStorageFrame *s, const VStorageFrameOps *ops, void *p)
{
    VStorageOffer o;
    SysResult_t z = ops->offer(p, &o);
    if (z)
        return z;
    if (!MRGrant_Equal(&o.request.g, &s->grant) || memcmp(&o.request.s, &s->slot, sizeof(s->slot)))
        return SYS_ERR_NOT_FOUND; /* 迟到槽位/代次，不改当前工作。 */
    if (o.kind == VSF_DELIVERY_WITHDRAWN && s->withdrawn && !o.request.h.session &&
        !o.request.h.sequence)
    {
        s->state = VSF_FREE;
        return SYS_OK;
    }
    if (!o.request.h.sequence || o.request.h.sequence <= s->highest_h ||
        o.request.h.session != s->grant.session || o.request.b.epoch || o.request.b.command ||
        o.request.b.lease)
    {
        s->state = VSF_BLOCKED;
        return SYS_ERR_PROTOCOL;
    }
    s->current = o;
    s->highest_h = o.request.h.sequence;
    if (o.kind == VSF_DELIVERY_TERMINAL && receipt_matches(s, &o.terminal, 0))
    {
        s->terminal = o.terminal;
        s->state = VSF_CONSUME;
        return o.terminal.result;
    }
    if (o.kind != VSF_DELIVERY_FRAME || o.media.backend != MR_JPEG || !o.media.session ||
        !o.media.id || !o.media.generation)
    {
        s->state = VSF_BLOCKED;
        return SYS_ERR_PROTOCOL;
    }
    /* 先保留真实M。JPEG结构或大小错误由写入端报告后走正常Return。 */
    s->state = VSF_READY;
    return SYS_OK;
}

SysResult_t VStorageFrame_Process(VStorageFrame *s, const VStorageFrameOps *ops, void *p,
                                  const MRGrant *g, int accepting, int quiet)
{
    MRReceipt t;
    SysResult_t z;
    switch (s->state)
    {
    case VSF_FREE:
        if (!accepting || !g->session)
            return SYS_OK;
        z = MRNumber_Advance(&s->generation);
        if (z)
            return z;
        s->grant = *g;
        s->slot = (MRSlot){g->session, 1U, s->generation};
        s->withdrawn = 0U;
        s->state = VSF_POST;
        return SYS_OK;
    case VSF_POST:
        if (!accepting)
        {
            s->state = VSF_FREE; /* 尚未入队。同一S永不复用。 */
            return SYS_OK;
        }
        z = ops->post(p, &s->grant, &s->slot);
        if (!z)
            s->state = VSF_WAIT;
        return z;
    case VSF_WAIT:
        if (!accepting && !s->withdrawn)
        {
            z = ops->withdraw(p, &s->grant, &s->slot);
            if (!z)
                s->withdrawn = 1U;
            else if (z != SYS_ERR_BUSY && z != SYS_ERR_NO_RESOURCE)
                return z;
            /* 撤回背压时仍消费已经送达的Offer，以免双向互等。 */
        }
        return receive_offer(s, ops, p);
    case VSF_READY:
        return SYS_OK; /* Storage业务处理完成/拒绝后显式进入RETURN。 */
    case VSF_RETURN:
        if (!quiet)
            return SYS_ERR_BUSY;
        z = ops->return_frame(p, &s->current.request, &s->current.media);
        if (!z)
        {
            memset(&s->current.view, 0, sizeof(s->current.view));
            s->state = VSF_ACK;
        }
        return z;
    case VSF_ACK:
        z = ops->receipt(p, &t);
        if (z)
            return z;
        if (!receipt_matches(s, &t, 1))
            return SYS_ERR_NOT_FOUND;
        s->terminal = t;
        s->state = VSF_CONSUME;
        return t.result;
    case VSF_CONSUME:
        z = ops->consume(p, &s->terminal);
        if (!z)
            s->state = VSF_RETIRE;
        return z;
    case VSF_RETIRE:
        z = ops->retired(p, &s->terminal);
        if (!z)
        {
            memset(&s->current, 0, sizeof(s->current));
            memset(&s->terminal, 0, sizeof(s->terminal));
            s->state = VSF_FREE;
        }
        return z;
    default:
        return SYS_ERR_PROTOCOL;
    }
}
