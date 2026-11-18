#include "usb_module.h"
#include <string.h>

#define MAGIC 0x56554235U
#define STOP_PROOF (VUSB_MODE_STOPPED | VUSB_READERS_QUIET | VUSB_BRIDGE_IDLE)
#define START_PROOF (VUSB_MODE_STARTED | VUSB_CLASS_READY)
#define SEND_PROOF (START_PROOF | VUSB_HOST_CONFIGURED | VUSB_CAN_SUBMIT)

static int owner(void *p)
{
    VUSB *u = p;
    return u && u->ops->is_owner(u->port);
}

static int waiting(SysResult_t z)
{
    return z == SYS_ERR_BUSY || z == SYS_ERR_NO_RESOURCE || z == SYS_ERR_NOT_FOUND;
}

static int proven(const VUSB *u, uint32_t mask)
{
    return (u->observation.flags & mask) == mask;
}

static int local_drained(const VUSB *u)
{
    return !(u->observation.flags & VUSB_FRAME_OWNED) &&
           proven(u, VUSB_READERS_QUIET | VUSB_BRIDGE_IDLE) && VUSBFrame_IsIdle(&u->frame);
}

static int stopped(const VUSB *u)
{
    return local_drained(u) && proven(u, STOP_PROOF) && !(u->observation.flags & VUSB_MODE_STARTED);
}

static int exit_confirmed(const VUSB *u)
{
    return stopped(u) && !u->consumer.grant.session &&
           (u->consumer.phase == VUC_IDLE || u->consumer.phase == VUC_IDLE_REQUEST);
}

static int retired(const VUSB *u)
{
    return exit_confirmed(u) && !u->drain && u->consumer.phase == VUC_IDLE;
}

static SysResult_t enter(VUSB *u)
{
    if (!u || u->magic != MAGIC || !owner(u))
        return SYS_ERR_INVALID_STATE;
    if (u->busy)
        return SYS_ERR_BUSY;
    u->busy = 1;
    return SYS_OK;
}

static SysResult_t leave(VUSB *u, SysResult_t result)
{
    u->busy = 0;
    return result;
}

static void remember(VUSB *u, SysResult_t z)
{
    if (!z || waiting(z))
        return;
    if (!u->first_fault)
        u->first_fault = z;
    if (!u->active_fault)
        u->active_fault = z;
}

static SysResult_t can_begin(void *p, const LCRequest *r)
{
    VUSB *u = p;
    if (r->id.operation == LC_CONFIGURE && !u->bound)
        return SYS_ERR_INVALID_STATE;
    if (r->id.operation != LC_DEACTIVATE && !retired(u))
        return SYS_ERR_BUSY;
    return SYS_OK;
}

static SysResult_t begin(void *p, const LCRequest *r)
{
    VUSB *u = p;
    SysResult_t z;
    if (r->id.operation == LC_ACTIVATE)
    {
        if (u->mode == VU_MODE_UVC)
        {
            z = VUSBConsumer_Begin(&u->consumer);
            if (z)
                return z;
        }
        u->frame.first_error = 0; /* 前一交接已退休，可开始本次诊断。 */
        u->start_issued = 0;
    }
    if (r->id.operation == LC_DEACTIVATE)
    {
        u->drain = 1;
        u->mode_stop_done = 0;
        u->stop_error = 0;
    }
    u->work = 1;
    return SYS_OK;
}

static SysResult_t prove(void *p, const LCRequest *r, uint32_t fact)
{
    VUSB *u = p;
    if (fact == r->expected)
        return SYS_OK; /* 操作未提交，原稳定事实保持。 */
    if (fact == LC_ACTIVE)
    {
        if (u->mode == VU_MODE_NONE)
            return SYS_OK;
        if (u->mode == VU_MODE_TEST)
            return proven(u, VUSB_MODE_STARTED) ? SYS_OK : SYS_ERR_BUSY;
        return proven(u, START_PROOF) && u->consumer.phase == VUC_GRANTED ? SYS_OK : SYS_ERR_BUSY;
    }
    if (r->id.operation == LC_DEACTIVATE)
        return exit_confirmed(u) ? SYS_OK : SYS_ERR_BUSY;
    return retired(u) ? SYS_OK : SYS_ERR_BUSY;
}

const struct V2LifecycleOps VUSB_LifecycleOps = {owner, can_begin, begin, prove};

SysResult_t VUSB_Init(VUSB *u, uint32_t module, uint32_t session, const VUSBOps *o, void *p,
                      const VUSBConsumerOps *consumer, void *cp, const VUSBFrameOps *frame,
                      void *fp)
{
    SysResult_t z;
    if (!u || u->magic || !module || !session || !o || !o->is_owner || !o->start ||
        !o->mode_process || !o->cancel || !o->uvc_process || !o->stop_mode || !o->observe)
        return SYS_ERR_INVALID_ARG;
    if (!o->is_owner(p))
        return SYS_ERR_INVALID_STATE;
    u->ops = o;
    u->port = p;
    z = VUSBConsumer_Init(&u->consumer, consumer, cp);
    if (!z)
        z = VUSBFrame_Init(&u->frame, frame, fp);
    if (!z)
        z = LC_Init(&u->lifecycle, module, session, &VUSB_LifecycleOps, u);
    if (z)
    {
        /* 这些初始化没有提交外部工作，允许修正端口后重试。 */
        memset(u, 0, sizeof(*u));
        return z;
    }
    u->magic = MAGIC;
    return SYS_OK;
}

SysResult_t VUSB_QueryCapabilities(VUSB *u, const CoreMediaFormat *format,
                                   const CoreFrameRate *rate, VideoStreamConfig_t *out)
{
    SysResult_t z = enter(u);
    if (z)
        return z;
    if (!out || V2_Overlap(u, sizeof(*u), out, sizeof(*out)))
        return leave(u, SYS_ERR_INVALID_ARG);
    return leave(u, VUSB_Translate(format, rate, out));
}

SysResult_t VUSB_BindCandidate(VUSB *u, const V2Registry *registry, const V2CompositionCandidate *v,
                               uint32_t mode, const CoreFrameRate *rate)
{
    const V2ModuleDesc *m;
    uint16_t i;
    VideoStreamConfig_t native = {0};
    SysResult_t z = enter(u);
    if (z)
        return z;
    if (!registry || !v || v->candidate_only != 1 || mode > VU_MODE_UVC)
        return leave(u, SYS_ERR_INVALID_ARG);
    m = V2Registry_Get(registry, (V2ModuleId){u->lifecycle.module});
    if (!m || !(v->modules.selected_mask & (1UL << (m - registry->modules))))
        return leave(u, SYS_ERR_NOT_FOUND);
    if (!v->modules.count || v->modules.count > V2_MODULE_MAX)
        return leave(u, SYS_ERR_PROTOCOL);
    for (i = 0; i < v->modules.count; ++i)
        if (v->modules.order[i].value == u->lifecycle.module)
            break;
    if (i == v->modules.count)
        return leave(u, SYS_ERR_PROTOCOL);
    if (mode == VU_MODE_UVC)
    {
        if (!v->media.pipeline_mask || !v->media.entity_mask)
            return leave(u, SYS_ERR_INVALID_ARG);
        z = VUSB_Translate(&v->media.format, rate, &native);
        if (z)
            return leave(u, z);
    }
    if (u->bound)
        return leave(u, u->mode == mode &&
                                !memcmp(&u->format, &v->media.format, sizeof(u->format)) &&
                                !memcmp(&u->native, &native, sizeof(native))
                            ? SYS_OK
                            : SYS_ERR_NOT_SUPPORTED);
    if (u->lifecycle.state != LC_UNCONFIGURED || u->lifecycle.pending || u->drain)
        return leave(u, SYS_ERR_BUSY);
    u->format = v->media.format;
    u->native = native;
    u->mode = mode;
    u->bound = 1;
    return leave(u, SYS_OK);
}

SysResult_t VUSB_Request(VUSB *u, const LCRequest *request, uint32_t now)
{
    SysResult_t z = enter(u);
    return z ? z : leave(u, LC_Request(&u->lifecycle, request, now));
}

static void complete(VUSB *u, SysResult_t z, uint32_t fact)
{
    remember(u, z);
    if (!LC_Complete(&u->lifecycle, &u->lifecycle.request.id, z, fact))
        u->work = 0;
}

/* 失败后的排空独立于原操作回复生命周期。 */
static void stop_fault(VUSB *u, SysResult_t z)
{
    if (!z || waiting(z))
        return;
    remember(u, z);
    if (!u->stop_error)
        u->stop_error = z;
    if (u->work && u->lifecycle.request.id.operation == LC_DEACTIVATE)
        complete(u, z, LC_ERROR);
}

static void activate(VUSB *u)
{
    SysResult_t z;
    if (u->mode == VU_MODE_UVC && u->consumer.phase != VUC_GRANTED)
    {
        if (u->consumer.phase == VUC_IDLE && u->consumer.result)
            complete(u, u->consumer.result, LC_INACTIVE);
        return;
    }
    if (u->mode != VU_MODE_NONE && !u->start_issued)
    {
        z = u->ops->start(u->port, u->mode, &u->native);
        if (waiting(z))
            return;
        u->start_issued = 1;
        if (z)
        {
            u->drain = 1;
            u->mode_stop_done = 0;
            u->stop_error = z;
            complete(u, z, LC_ERROR);
        }
        /* 下一轮观察启动事实，禁止用提交返回值宣告启动成功。 */
        return;
    }
    if (u->mode != VU_MODE_NONE && u->observation.result)
    {
        u->drain = 1;
        u->mode_stop_done = 0;
        u->stop_error = u->observation.result;
        complete(u, u->stop_error, LC_ERROR);
        return;
    }
    if (!prove(u, &u->lifecycle.request, LC_ACTIVE))
    {
        complete(u, SYS_OK, LC_ACTIVE);
        if (!u->lifecycle.first_error)
            u->active_fault = SYS_OK;
    }
}

static SysResult_t observe(VUSB *u)
{
    VUSBObservation latest = {0};
    SysResult_t z = u->ops->observe(u->port, &latest);
    /* 观测失败，本轮不能沿用旧的停止或DMA安全证明。 */
    u->observation = latest;
    if (z)
    {
        u->observation.flags = 0;
        u->observation.result = z;
    }
    return z;
}

static void finish_stop(VUSB *u)
{
    SysResult_t z;
    if (!local_drained(u))
        return;
    if (!u->mode_stop_done)
    {
        /* 从未提交过硬件启动，已有冷态证据时无需调用Stop。 */
        z = u->mode == VU_MODE_NONE || (!u->start_issued && stopped(u))
                ? SYS_OK
                : u->ops->stop_mode(u->port);
        if (z)
        {
            stop_fault(u, z);
            return;
        }
        u->mode_stop_done = 1;
        return; /* 等下一轮重新采样硬件事实。 */
    }
    if (!stopped(u))
        return;
    if (u->consumer.phase == VUC_GRANTED)
    {
        stop_fault(u, VUSBConsumer_End(&u->consumer, u->stop_error));
        return;
    }
    if (u->consumer.phase == VUC_BLOCKED)
        stop_fault(u, u->consumer.result);
    /* USB可以先完成退出；全局STOP_IF_IDLE仍保留在Tx中，等待原回复。 */
    if (!exit_confirmed(u))
        return;
    u->drain = 0;
    u->start_issued = 0;
    if (u->work && u->lifecycle.request.id.operation == LC_DEACTIVATE)
        complete(u, SYS_OK, LC_INACTIVE);
}

SysResult_t VUSB_RunRound(VUSB *u, uint32_t now)
{
    SysResult_t z = enter(u);
    uint32_t op;
    if (z)
        return z;
    (void)LC_Process(&u->lifecycle, now);
    z = VUSBConsumer_Process(&u->consumer);
    remember(u, z);
    if (u->drain)
        stop_fault(u, z);
    if (u->mode != VU_MODE_NONE)
    {
        u->ops->mode_process(u->port);
        if (u->drain && u->mode == VU_MODE_UVC && !u->mode_stop_done)
            stop_fault(u, u->ops->cancel(u->port));
        u->ops->uvc_process(u->port);
    }
    z = observe(u);
    remember(u, z);
    if (u->drain)
        stop_fault(u, z);
    if (u->mode == VU_MODE_UVC)
    {
        z = VUSBFrame_Process(&u->frame, &u->consumer.grant, &u->format,
                              !u->drain && u->lifecycle.state == LC_ACTIVE &&
                                  u->consumer.phase == VUC_GRANTED && proven(u, SEND_PROOF));
        remember(u, z);
        remember(u, u->frame.first_error);
        if (u->drain)
        {
            stop_fault(u, z);
            u->ops->uvc_process(u->port);
            stop_fault(u, observe(u));
            stop_fault(u, u->observation.result);
        }
    }
    if (u->drain)
        finish_stop(u);
    op = u->lifecycle.request.id.operation;
    if (u->work && op == LC_ACTIVATE)
        activate(u);
    if (u->work && (op == LC_CONFIGURE || op == LC_CLEANUP || op == LC_ERROR_PROCESS))
    {
        if (retired(u))
        {
            if (op != LC_CONFIGURE)
                u->bound = 0;
            complete(u, SYS_OK, LC_Target(op));
        }
    }
    /* 下一轮发布决定，避免在本轮重复尝试 can_begin/begin。 */
    return leave(u, SYS_OK);
}

SysResult_t VUSB_QueryStatus(VUSB *u, VUSBStatus *out)
{
    SysResult_t z = enter(u);
    if (z)
        return z;
    if (!out || V2_Overlap(u, sizeof(*u), out, sizeof(*out)))
        return leave(u, SYS_ERR_INVALID_ARG);
    out->lifecycle = u->lifecycle.state;
    out->drain = u->drain;
    out->frame_phase = u->frame.phase;
    out->consumer_phase = u->consumer.phase;
    out->grant = u->consumer.grant;
    out->native = u->observation;
    out->operation_error = u->lifecycle.first_error;
    out->first_fault = u->first_fault;
    out->active_fault = u->active_fault;
    out->stop_error = u->stop_error;
    out->camera_stop_result = u->consumer.camera_stop_result;
    return leave(u, SYS_OK);
}

SysResult_t VUSB_QueryExitEvidence(VUSB *u, const MRGrant *g)
{
    SysResult_t z = enter(u);
    if (z)
        return z;
    if (!g || !g->session || !MRGrant_Equal(g, &u->consumer.grant))
        return leave(u, SYS_ERR_PROTOCOL);
    /* 只是USB本地证据。最终Camera核验仍使用原 Grant + 独立 RELEASE。 */
    return leave(u, u->drain && u->mode_stop_done && stopped(u) ? SYS_OK : SYS_ERR_BUSY);
}

SysResult_t VUSB_ReadCompletion(VUSB *u, const V2ExecId *id, LCCompletion *out)
{
    SysResult_t z = enter(u);
    if (z)
        return z;
    if (!out || V2_Overlap(u, sizeof(*u), out, sizeof(*out)))
        return leave(u, SYS_ERR_INVALID_ARG);
    return leave(u, LC_Read(&u->lifecycle, id, out));
}

SysResult_t VUSB_ConfirmCompletion(VUSB *u, const LCCompletion *v)
{
    SysResult_t z = enter(u);
    return z ? z : leave(u, LC_Confirm(&u->lifecycle, v));
}
