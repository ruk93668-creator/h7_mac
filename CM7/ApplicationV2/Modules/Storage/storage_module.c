#include "storage_module.h"
#include <string.h>

#define MAGIC 0x53544F36U
#define FILE_NEW 0U
#define FILE_MOUNTED 1U
#define FILE_WRITING 2U
#define FILE_FINISH 3U
#define FILE_CLOSE 4U
#define FILE_UNMOUNT 5U
#define FILE_DONE 6U

static int owner(void *p)
{
    VStorage *s = p;
    return s && s->ports.consumer->is_owner(s->ports.consumer_port);
}

static SysResult_t enter(VStorage *s)
{
    if (!s || s->magic != MAGIC || !owner(s))
        return SYS_ERR_INVALID_STATE;
    if (s->busy)
        return SYS_ERR_BUSY;
    s->busy = 1U;
    return SYS_OK;
}

static SysResult_t leave(VStorage *s, SysResult_t z)
{
    s->busy = 0U;
    return z;
}

static int waiting(SysResult_t z)
{
    return z == SYS_ERR_BUSY || z == SYS_ERR_NO_RESOURCE || z == SYS_ERR_NOT_FOUND;
}

static int quiet(const VStorage *s)
{
    return (s->facts.flags & VST_IO_QUIET) != 0U;
}

static int volume_clear(const VStorage *s)
{
    return quiet(s) && (s->facts.flags & VST_VOLUME_RELEASED) &&
           !(s->facts.flags & (VST_FILE_OPEN | VST_VOLUME_HELD));
}

static int exited(const VStorage *s)
{
    return quiet(s) && s->frame.state == VSF_FREE && !s->consumer.grant.session &&
           (s->consumer.phase == VSC_IDLE || s->consumer.phase == VSC_IDLE_REQUEST);
}

static int ready_to_configure(const VStorage *s)
{
    return !s->drain && exited(s) && s->consumer.phase == VSC_IDLE && volume_clear(s);
}

static SysResult_t first_result(const VStorage *s)
{
    return s->business_error ? s->business_error : s->cleanup_error;
}

static void remember(VStorage *s, SysResult_t z, SysResult_t *slot)
{
    if (!z)
        return;
    if (!*slot)
        *slot = z;
    if (!s->first_fault)
        s->first_fault = z;
}

static SysResult_t can_begin(void *p, const LCRequest *r)
{
    VStorage *s = p;
    if (r->id.operation == LC_CONFIGURE && !s->bound)
        return SYS_ERR_INVALID_STATE;
    if (r->id.operation == LC_DEACTIVATE || r->id.operation == LC_ERROR_PROCESS)
        return SYS_OK;
    return ready_to_configure(s) ? SYS_OK : SYS_ERR_BUSY;
}

static SysResult_t begin(void *p, const LCRequest *r)
{
    VStorage *s = p;
    SysResult_t z;
    if (r->id.operation == LC_ACTIVATE)
    {
        z = VStorageConsumer_Begin(&s->consumer);
        if (z)
            return z;
        s->file_step = FILE_NEW;
        s->frames = 0U;
        s->business_error = 0;
        s->cleanup_error = 0;
        s->recovery_result = 0;
    }
    if (r->id.operation == LC_ERROR_PROCESS)
    {
        s->recovery_issued = 0U;
        s->recovery_result = 0;
    }
    if (r->id.operation == LC_DEACTIVATE)
        s->drain = 1U;
    s->work = 1U;
    return SYS_OK;
}

static SysResult_t prove(void *p, const LCRequest *r, uint32_t fact)
{
    VStorage *s = p;
    if (r->expected == fact)
        return SYS_OK;
    if (fact == LC_ACTIVE)
        return s->consumer.phase == VSC_GRANTED && quiet(s) &&
                       (s->facts.flags & (VST_VOLUME_HELD | VST_FILE_OPEN)) ==
                           (VST_VOLUME_HELD | VST_FILE_OPEN)
                   ? SYS_OK
                   : SYS_ERR_BUSY;
    if (r->id.operation == LC_DEACTIVATE)
        return exited(s) && volume_clear(s) ? SYS_OK : SYS_ERR_BUSY;
    return ready_to_configure(s) ? SYS_OK : SYS_ERR_BUSY;
}

const struct V2LifecycleOps VStorage_LifecycleOps = {owner, can_begin, begin, prove};

SysResult_t VStorage_Init(VStorage *s, uint32_t module, uint32_t session, const VStorageCaps *caps,
                          const VStoragePorts *ports)
{
    const VStorageBackendOps *b;
    const VStorageFrameOps *f;
    SysResult_t z;
    if (!s || s->magic || !module || !session || !caps || !caps->max_payload || !ports)
        return SYS_ERR_INVALID_ARG;
    b = ports->backend;
    f = ports->frame;
    if (!b || !b->mount || !b->open || !b->write || !b->finish || !b->close || !b->unmount ||
        !b->recover || !b->observe || !f || !f->post || !f->withdraw || !f->offer ||
        !f->return_frame || !f->receipt || !f->consume || !f->retired)
        return SYS_ERR_INVALID_ARG;
    z = VStorageConsumer_Init(&s->consumer, ports->consumer, ports->consumer_port);
    if (z)
        return z;
    s->ports = *ports;
    s->caps = *caps;
    z = LC_Init(&s->lifecycle, module, session, &VStorage_LifecycleOps, s);
    if (z)
    {
        memset(s, 0, sizeof(*s)); /* 初始化还未获取外部资源。 */
        return z;
    }
    s->magic = MAGIC;
    return SYS_OK;
}

SysResult_t VStorage_BindCandidate(VStorage *s, const V2Registry *registry,
                                   const V2CompositionCandidate *v, const VStorageConfig *cfg)
{
    const V2ModuleDesc *d;
    uint32_t interval;
    uint16_t i;
    SysResult_t z = enter(s);
    if (z)
        return z;
    if (!registry || !v || v->candidate_only != 1U || !v->media.pipeline_mask ||
        !v->media.entity_mask || !v->modules.count || v->modules.count > V2_MODULE_MAX)
        return leave(s, SYS_ERR_INVALID_ARG);
    d = V2Registry_Get(registry, (V2ModuleId){s->lifecycle.module});
    if (!d || !(v->modules.selected_mask & (1UL << (d - registry->modules))))
        return leave(s, SYS_ERR_NOT_FOUND);
    for (i = 0; i < v->modules.count; ++i)
        if (v->modules.order[i].value == s->lifecycle.module)
            break;
    if (i == v->modules.count)
        return leave(s, SYS_ERR_PROTOCOL);
    if (!cfg || !cfg->sink_entity || !v->media.graph || !v->media.sink_count ||
        v->media.sink_count > GRAPH_SINK_MAX)
        return leave(s, SYS_ERR_INVALID_ARG);
    for (i = 0; i < v->media.sink_count; ++i)
        if (v->media.sinks[i].sink.value == cfg->sink_entity)
            break;
    if (i == v->media.sink_count)
        return leave(s, SYS_ERR_NOT_FOUND);
    if (cfg->mode == VST_AVI && (cfg->sampling.num != v->media.sinks[i].rate.num ||
                                 cfg->sampling.den != v->media.sinks[i].rate.den))
        return leave(s, SYS_ERR_INVALID_ARG);
    z = VStorage_ValidateFormat(&v->media.format, cfg, &s->caps, &interval);
    if (z)
        return leave(s, z);
    if (s->bound)
        return leave(s, !memcmp(cfg, &s->config, sizeof(*cfg)) &&
                                !memcmp(&v->media.format, &s->format, sizeof(s->format))
                            ? SYS_OK
                            : SYS_ERR_NOT_SUPPORTED);
    if (s->lifecycle.state != LC_UNCONFIGURED || s->lifecycle.pending || s->drain)
        return leave(s, SYS_ERR_BUSY);
    s->config = *cfg;
    s->format = v->media.format;
    s->interval_ms = interval;
    s->bound = 1U;
    return leave(s, SYS_OK);
}

SysResult_t VStorage_QueryCapabilities(VStorage *s, const CoreMediaFormat *f,
                                       const VStorageConfig *cfg, uint32_t *ms)
{
    SysResult_t z = enter(s);
    if (z)
        return z;
    if (!ms || V2_Overlap(s, sizeof(*s), ms, sizeof(*ms)))
        return leave(s, SYS_ERR_INVALID_ARG);
    return leave(s, VStorage_ValidateFormat(f, cfg, &s->caps, ms));
}

SysResult_t VStorage_Request(VStorage *s, const LCRequest *r, uint32_t now)
{
    SysResult_t z = enter(s);
    return z ? z : leave(s, LC_Request(&s->lifecycle, r, now));
}

static void complete(VStorage *s, SysResult_t z, uint32_t fact)
{
    if (!LC_Complete(&s->lifecycle, &s->lifecycle.request.id, z, fact))
        s->work = 0U;
}

static void poll_facts(VStorage *s)
{
    VStorageFacts fresh = {0};
    SysResult_t z = s->ports.backend->observe(s->ports.backend_port, &fresh);
    s->facts.flags = z ? 0U : fresh.flags; /* 查询失败后禁止复用旧静止证据。 */
    if (z && !waiting(z))
        remember(s, z, &s->cleanup_error);
}

static void start(VStorage *s)
{
    const VStorageBackendOps *b = s->ports.backend;
    void *p = s->ports.backend_port;
    SysResult_t z = SYS_OK;

    if (s->consumer.phase == VSC_IDLE && s->consumer.result)
    {
        remember(s, s->consumer.result, &s->business_error);
        complete(s, s->consumer.result, LC_INACTIVE);
        return;
    }
    if (s->consumer.phase == VSC_BLOCKED)
    {
        remember(s, s->consumer.result, &s->business_error);
        complete(s, s->consumer.result, LC_ERROR);
        return;
    }
    if (s->consumer.phase != VSC_GRANTED)
        return;
    if (s->lifecycle.first_error == SYS_ERR_TIMEOUT)
        z = SYS_ERR_TIMEOUT;
    else if (s->file_step == FILE_NEW)
    {
        z = b->mount(p, &s->config);
        if (!z)
            s->file_step = FILE_MOUNTED;
    }
    else if (s->file_step == FILE_MOUNTED)
    {
        z = b->open(p, &s->config, &s->format);
        if (!z)
            s->file_step = FILE_WRITING;
    }
    else if (!prove(s, &s->lifecycle.request, LC_ACTIVE))
        complete(s, SYS_OK, LC_ACTIVE);

    if (z)
    {
        remember(s, z, &s->business_error);
        s->drain = 1U;
        complete(s, z, LC_ERROR);
    }
}

static void write_frame(VStorage *s, uint32_t now)
{
    const VStorageOffer *o = &s->frame.current;
    SysResult_t z;

    /* 已准入的录像帧可排空。发生文件错误后仍归还，但不再写。 */
    if (!s->business_error && (s->facts.flags & VST_FILE_OPEN) &&
        (!s->drain || s->config.mode == VST_AVI))
    {
        z = s->ports.backend->write(s->ports.backend_port, &s->config, &s->format, &o->view,
                                    o->timestamp);
        if (z)
        {
            remember(s, z, &s->business_error);
            s->drain = 1U;
        }
        else
        {
            s->frames++;
            s->sample_tick = now;
            if (s->config.mode == VST_SNAPSHOT)
                s->drain = 1U;
        }
        s->facts.flags &= ~VST_IO_QUIET; /* 写入后的归还需要重新观测。 */
    }
    s->frame.state = VSF_RETURN;
}

static void finish_files(VStorage *s)
{
    const VStorageBackendOps *b = s->ports.backend;
    void *p = s->ports.backend_port;
    SysResult_t z = SYS_OK;
    if (s->file_step < FILE_FINISH)
        s->file_step = FILE_FINISH;
    switch (s->file_step)
    {
    case FILE_FINISH:
        if ((s->facts.flags & VST_FILE_OPEN) && !s->business_error)
        {
            if (s->frames)
                z = b->finish(p);
            else if (s->config.mode == VST_AVI)
                z = SYS_ERR_NOT_FOUND; /* 空AVI不能宣称完整成片。 */
        }
        s->file_step = FILE_CLOSE;
        break;
    case FILE_CLOSE:
        if (s->facts.flags & VST_FILE_OPEN)
            z = b->close(p);
        s->file_step = FILE_UNMOUNT;
        break;
    case FILE_UNMOUNT:
        if (s->facts.flags & VST_VOLUME_HELD)
            z = (s->facts.flags & VST_FILE_OPEN) ? SYS_ERR_INVALID_STATE : b->unmount(p);
        s->file_step = FILE_DONE;
        break;
    default:
        break;
    }
    remember(s, z, &s->cleanup_error);
}

static uint32_t release_flags(const VStorage *s)
{
    uint32_t flags =
        APP_LIFECYCLE_INPUT_CLOSED | APP_LIFECYCLE_WORK_QUIESCENT | APP_LIFECYCLE_FRAMES_DRAINED;
    if (s->facts.flags & VST_FILE_FINISHED)
        flags |= APP_LIFECYCLE_FILE_FINISHED;
    if (s->facts.flags & VST_FILE_CLOSED)
        flags |= APP_LIFECYCLE_FILE_CLOSED;
    if (s->facts.flags & VST_VOLUME_RELEASED)
        flags |= APP_LIFECYCLE_VOLUME_RELEASED;
    return flags;
}

static void stop(VStorage *s)
{
    SysResult_t z;
    if (s->frame.state != VSF_FREE || !quiet(s))
        return;
    if (s->file_step != FILE_DONE)
    {
        finish_files(s);
        return;
    }
    if (s->consumer.phase == VSC_GRANTED)
    {
        z = VStorageConsumer_End(&s->consumer, s->business_error, s->cleanup_error,
                                 release_flags(s));
        remember(s, z, &s->cleanup_error);
        return;
    }
    if (exited(s))
    {
        s->drain = 0U;
        if (s->work && s->lifecycle.request.id.operation == LC_DEACTIVATE)
        {
            z = first_result(s);
            if (!z && (!volume_clear(s) ||
                       (s->frames && (s->facts.flags & (VST_FILE_FINISHED | VST_FILE_CLOSED)) !=
                                         (VST_FILE_FINISHED | VST_FILE_CLOSED))))
            {
                z = SYS_ERR_INVALID_STATE;
                remember(s, z, &s->cleanup_error);
            }
            complete(s, z, z ? LC_ERROR : LC_INACTIVE);
        }
    }
}

static void recover(VStorage *s)
{
    if (!s->recovery_issued && !volume_clear(s))
    {
        /* 显式恢复可使原来故障的IDMA停止，然后继续归还原Frame。
         * 完成的同步写入不会在此轮重新执行。 */
        s->recovery_issued = 1U;
        if (s->facts.flags & VST_FS_FAULT)
            s->recovery_result = s->ports.backend->recover(s->ports.backend_port);
        else
            s->recovery_result = SYS_ERR_INVALID_STATE;
        if (s->recovery_result)
            complete(s, s->recovery_result, LC_ERROR);
        return;
    }
    if (ready_to_configure(s))
    {
        s->bound = 0U;
        complete(s, SYS_OK, LC_UNCONFIGURED);
    }
}

SysResult_t VStorage_RunRound(VStorage *s, uint32_t now)
{
    SysResult_t z = enter(s);
    uint32_t op, before;
    int accepting;
    if (z)
        return z;
    poll_facts(s);
    (void)LC_Process(&s->lifecycle, now);
    op = s->lifecycle.request.id.operation;
    z = VStorageConsumer_Process(&s->consumer);
    if (s->consumer.phase == VSC_BLOCKED)
        remember(s, z, &s->cleanup_error);
    accepting = !s->drain && quiet(s) && !(s->facts.flags & VST_FS_FAULT) &&
                s->lifecycle.state == LC_ACTIVE && s->consumer.phase == VSC_GRANTED &&
                (s->facts.flags & VST_FILE_OPEN) &&
                (!s->frames || now - s->sample_tick >= s->interval_ms);
    before = s->frame.state;
    z = VStorageFrame_Process(&s->frame, s->ports.frame, s->ports.frame_port, &s->consumer.grant,
                              accepting, quiet(s));
    if (s->frame.state == VSF_BLOCKED ||
        (z && (!waiting(z) || (before == VSF_ACK && s->frame.state == VSF_CONSUME))))
    {
        remember(s, z, &s->business_error);
        s->drain = 1U;
    }
    if (s->file_step == FILE_WRITING && (s->facts.flags & VST_FS_FAULT))
    {
        remember(s, SYS_ERR_IO, &s->business_error);
        s->drain = 1U;
    }
    if (s->frame.state == VSF_READY && (quiet(s) || s->business_error))
        write_frame(s, now);
    if (s->drain)
        stop(s);
    if (s->work && op == LC_DEACTIVATE && first_result(s))
        complete(s, first_result(s), LC_ERROR);
    if (s->work && op == LC_ACTIVATE)
        start(s);
    if (s->work && op == LC_ERROR_PROCESS)
        recover(s);
    if (s->work && (op == LC_CONFIGURE || op == LC_CLEANUP) && ready_to_configure(s))
    {
        if (op == LC_CLEANUP)
            s->bound = 0U;
        complete(s, SYS_OK, LC_Target(op));
    }
    return leave(s, SYS_OK);
}

SysResult_t VStorage_QueryStatus(VStorage *s, VStorageStatus *o)
{
    SysResult_t z = enter(s);
    if (z)
        return z;
    if (!o || V2_Overlap(s, sizeof(*s), o, sizeof(*o)))
        return leave(s, SYS_ERR_INVALID_ARG);
    o->lifecycle = s->lifecycle.state;
    o->drain = s->drain;
    o->frame_state = s->frame.state;
    o->frames = s->frames;
    o->qualification = s->consumer.phase;
    o->grant = s->consumer.grant;
    o->files = s->facts;
    o->first_fault = s->first_fault;
    o->business_error = s->business_error;
    o->cleanup_error = s->cleanup_error;
    o->recovery_result = s->recovery_result;
    o->camera_stop_result = s->consumer.camera_stop_result;
    return leave(s, SYS_OK);
}

SysResult_t VStorage_QueryExitEvidence(VStorage *s, const MRGrant *g, uint32_t *flags)
{
    SysResult_t z = enter(s);
    if (z)
        return z;
    if (!g || !g->session || !flags || V2_Overlap(s, sizeof(*s), flags, sizeof(*flags)) ||
        V2_Overlap(g, sizeof(*g), flags, sizeof(*flags)) || !MRGrant_Equal(g, &s->consumer.grant))
        return leave(s, SYS_ERR_PROTOCOL);
    if (!s->drain || s->file_step != FILE_DONE || s->frame.state != VSF_FREE || !quiet(s))
        return leave(s, SYS_ERR_BUSY);
    *flags = release_flags(s);
    return leave(s, SYS_OK);
}

SysResult_t VStorage_ReadCompletion(VStorage *s, const V2ExecId *id, LCCompletion *out)
{
    SysResult_t z = enter(s);
    if (z)
        return z;
    if (!out || V2_Overlap(s, sizeof(*s), out, sizeof(*out)))
        return leave(s, SYS_ERR_INVALID_ARG);
    return leave(s, LC_Read(&s->lifecycle, id, out));
}

SysResult_t VStorage_ConfirmCompletion(VStorage *s, const LCCompletion *c)
{
    SysResult_t z = enter(s);
    return z ? z : leave(s, LC_Confirm(&s->lifecycle, c));
}
