#include "usb_frame.h"
#include <string.h>
#define MAGIC 0x55465235U
static SysResult_t enter(VUSBFrame *f)
{
    if (!f || f->magic != MAGIC || !f->ops->is_owner(f->port))
        return SYS_ERR_INVALID_STATE;
    if (f->busy)
        return SYS_ERR_BUSY;
    f->busy = 1;
    return 0;
}
static SysResult_t leave(VUSBFrame *f, SysResult_t z)
{
    f->busy = 0;
    return z;
}
static void fault(VUSBFrame *f, SysResult_t z)
{
    if (z != SYS_ERR_BUSY && z != SYS_ERR_NO_RESOURCE && z != SYS_ERR_NOT_FOUND && z &&
        !f->first_error)
        f->first_error = z;
}
int VUSBFrame_IsIdle(const VUSBFrame *f)
{
    return f && f->magic == MAGIC && f->ops->is_owner(f->port) && f->phase == VUF_IDLE;
}
SysResult_t VUSBFrame_Init(VUSBFrame *f, const VUSBFrameOps *o, void *p)
{
    if (!f || f->magic || !o || !o->is_owner || !o->prepare || !o->prepared || !o->commit ||
        !o->cancel || !o->cancelled || !o->offer || !o->submit_uvc || !o->readers_safe ||
        !o->return_frame || !o->return_ack || !o->consumed || !o->retired)
        return SYS_ERR_INVALID_ARG;
    if (!o->is_owner(p))
        return SYS_ERR_INVALID_STATE;
    memset(f, 0, sizeof(*f));
    f->ops = o;
    f->port = p;
    f->magic = MAGIC;
    return 0;
}
static int request_equal(const MRRequest *a, const MRRequest *b)
{
    return !memcmp(a, b, sizeof(*a));
}
static int terminal_matches(VUSBFrame *f, const MRReceipt *t, int physical)
{
    return MRGrant_Equal(&f->request.g, &t->g) && !memcmp(&f->request.h, &t->h, sizeof(t->h)) &&
           t->serial && t->result <= 0 &&
           (physical ? (t->physical_returned == 1 && MRMedia_Equal(&f->data.media, &t->m) &&
                        !memcmp(&f->data.bridge, &t->bridge, sizeof(t->bridge)))
                     : (!t->physical_returned && !t->m.id && !t->bridge.lease));
}
static int view_valid(const MRView *v, const CoreMediaFormat *fmt)
{
    uint32_t total;
    if (!v->data || !v->bytes || (v->bytes2 && !v->data2) || v->bytes > UINT32_MAX - v->bytes2)
        return 0;
    total = v->bytes + v->bytes2;
    return total <= fmt->max_payload &&
           (fmt->backend == CORE_BACKEND_JPEG ? total >= 4
                                              : !v->bytes2 && total == fmt->size_image);
}
SysResult_t VUSBFrame_Process(VUSBFrame *f, const MRGrant *grant, const CoreMediaFormat *fmt,
                              int accepting)
{
    SysResult_t z = enter(f);
    VUSBDelivery d;
    MRRequest h;
    MRReceipt t;
    if (z)
        return z;
    if (!grant || !fmt)
        return leave(f, SYS_ERR_INVALID_ARG);
    if (f->phase == VUF_BLOCKED)
        return leave(f, f->first_error ? f->first_error : SYS_ERR_PROTOCOL);
    switch (f->phase)
    {
    case VUF_IDLE:
        if (!accepting || !grant->session)
            return leave(f, 0);
        z = f->ops->prepare(f->port, grant);
        if (!z)
        {
            f->preparing = *grant;
            f->phase = VUF_PREPARING;
        }
        break;
    case VUF_PREPARING:
        z = f->ops->prepared(f->port, &h);
        if (!z)
        {
            if (!MRGrant_Equal(&h.g, &f->preparing))
                return leave(f, SYS_ERR_NOT_FOUND);
            if (!h.h.sequence || h.h.session != h.g.session || h.h.sequence <= f->highest_h ||
                !h.b.epoch || !h.b.command || h.b.lease)
            {
                f->phase = VUF_BLOCKED;
                z = SYS_ERR_PROTOCOL;
                break;
            }
            f->request = h;
            f->highest_h = h.h.sequence;
            f->phase = VUF_PREPARED;
        }
        break;
    case VUF_PREPARED:
        if (!accepting)
        {
            z = f->ops->cancel(f->port, &f->request);
            if (!z)
                f->phase = VUF_CANCELLING;
        }
        else
        {
            z = f->ops->commit(f->port, &f->request);
            if (!z)
                f->phase = VUF_COMMITTED;
        }
        break;
    case VUF_CANCELLING:
        z = f->ops->cancelled(f->port, &f->request);
        if (!z)
            f->phase = VUF_IDLE;
        break;
    case VUF_COMMITTED:
        z = f->ops->offer(f->port, &d);
        if (!z)
        {
            if (!request_equal(&d.request, &f->request))
                return leave(f, SYS_ERR_NOT_FOUND);
            if (d.kind == VUF_TERMINAL && terminal_matches(f, &d.terminal, 0))
            {
                f->terminal = d.terminal;
                fault(f, d.terminal.result);
                f->phase = VUF_CONSUMING;
                break;
            }
            if (d.kind != VUF_FRAME || d.data.media.backend != fmt->backend ||
                !d.data.media.session || !d.data.media.id || !d.data.media.generation ||
                !d.data.bridge.lease || d.data.bridge.epoch != f->request.b.epoch ||
                d.data.bridge.command != f->request.b.command || !view_valid(&d.data.view, fmt))
            {
                f->phase = VUF_BLOCKED;
                z = SYS_ERR_PROTOCOL;
                break;
            }
            f->data = d.data;
            f->phase = VUF_OFFERED;
        }
        break;
    case VUF_OFFERED:
        if (!accepting)
        {
            f->phase = VUF_RETURNING;
            return leave(f, 0);
        }
        z = f->ops->submit_uvc(f->port, &f->request, &f->data);
        if (!z)
            f->phase = VUF_IN_UVC;
        else if (z != SYS_ERR_BUSY && z != SYS_ERR_NO_RESOURCE)
            f->phase = VUF_RETURNING;
        break;
    case VUF_IN_UVC:
        z = f->ops->readers_safe(f->port, &f->request, &f->data);
        if (!z)
            f->phase = VUF_RETURNING;
        break;
    case VUF_RETURNING:
        /* 第二次核验覆盖未提交UVC及多个异步CPU/DMA读取者。 */
        z = f->ops->readers_safe(f->port, &f->request, &f->data);
        if (z)
            break;
        z = f->ops->return_frame(f->port, &f->request, &f->data);
        if (!z)
            f->phase = VUF_WAIT_ACK;
        break;
    case VUF_WAIT_ACK:
        z = f->ops->return_ack(f->port, &t);
        if (!z)
        {
            if (!terminal_matches(f, &t, 1))
                return leave(f, SYS_ERR_NOT_FOUND);
            f->terminal = t;
            fault(f, t.result);
            f->phase = VUF_CONSUMING;
        }
        break;
    case VUF_CONSUMING:
        z = f->ops->consumed(f->port, &f->terminal);
        if (!z)
            f->phase = VUF_RETIRING;
        break;
    case VUF_RETIRING:
        z = f->ops->retired(f->port, &f->terminal);
        if (!z)
        {
            /* 只退休本地元数据；不是Media Release，也不清相邻消费者。 */
            f->phase = VUF_IDLE;
            memset(&f->data, 0, sizeof(f->data));
        }
        break;
    default:
        z = SYS_ERR_INVALID_STATE;
        break;
    }
    fault(f, z);
    return leave(f, z);
}
