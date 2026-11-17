#include "camera_module.h"
#include <string.h>
#define MAGIC 0x56434134U
static SysResult_t enter(VCamera *c)
{
    if (!c || c->magic != MAGIC || !c->ops->is_owner(c->port))
        return SYS_ERR_INVALID_STATE;
    if (c->busy)
        return SYS_ERR_BUSY;
    c->busy = 1;
    return 0;
}
static SysResult_t leave(VCamera *c, SysResult_t z)
{
    c->busy = 0;
    return z;
}
static void fault(VCamera *c, SysResult_t z)
{
    if (z && z != SYS_ERR_BUSY && z != SYS_ERR_NO_RESOURCE && !c->first_fault)
        c->first_fault = z;
}
static int local(uint32_t op) { return op == LC_CONFIGURE || op == LC_CLEANUP; }
static int safe(uint32_t f)
{
    return (f & VC_STOP_MASK) == VC_STOP_MASK || (f & VC_COLD_MASK) == VC_COLD_MASK;
}
static const MRSource *source(VCamera *c, uint32_t backend)
{
    return backend == CORE_BACKEND_RAW ? &c->resources.raw_source : &c->resources.jpeg_source;
}
static int lc_owner(void *p)
{
    VCamera *c = p;
    return c->ops->is_owner(c->port);
}
static SysResult_t capabilities(VCamera *c, const CoreMediaFormat *f, VCameraCapabilities *out)
{
    VCameraCapabilities v;
    SysResult_t z;
    memset(&v, 0, sizeof(v));
    z = VCamera_Translate(f, &v.native);
    if (z)
        return z;
    z = MRSource_Query(source(c, f->backend), &v.resource);
    if (z)
        return z;
    if (v.resource.backend != f->backend || f->max_spans > v.resource.spans)
        return SYS_ERR_NOT_SUPPORTED;
    z = c->ops->validate_layout(c->port, f, &v.native, &v.resource);
    if (z)
        return z;
    *out = v;
    return 0;
}
static SysResult_t can_begin(void *p, const LCRequest *r)
{
    VCamera *c = p;
    VCameraCapabilities cap;
    SysResult_t z;
    if (c->work)
        return SYS_ERR_BUSY;
    if (!c->bound)
        return SYS_ERR_INVALID_STATE;
    if (r->id.operation == LC_ACTIVATE && !c->prepared)
        return SYS_ERR_INVALID_STATE;
    if (r->id.operation == LC_CONFIGURE || r->id.operation == LC_ACTIVATE)
    {
        z = capabilities(c, &c->format, &cap);
        if (z)
            return z;
    }
    return c->ops->gate(c->port, r, VC_GATE_BEGIN);
}
static SysResult_t begin(void *p, const LCRequest *r)
{
    VCamera *c = p;
    c->operation = *r;
    c->work = 1;
    c->submitted = 0;
    c->decided = 0;
    c->delivered = 0;
    memset(&c->result, 0, sizeof(c->result));
    c->result.id = r->id;
    return 0;
}
static SysResult_t prove(void *p, const LCRequest *r, uint32_t fact)
{
    VCamera *c = p;
    VCameraObservation f;
    SysResult_t z;
    if (!c->work && fact == r->expected)
        return c->ops->gate(c->port, r, VC_GATE_PROVE);
    if (!LC_IdEqual(&r->id, &c->operation.id))
        return SYS_ERR_PROTOCOL;
    z = c->ops->gate(c->port, r, VC_GATE_PROVE);
    if (z)
        return z;
    if (r->id.operation == LC_CONFIGURE)
        return 0;
    if (r->id.operation == LC_ACTIVATE)
    {
        z = c->ops->facts(c->port, &f);
        if (z)
            return z;
        return ((c->result.observation.flags & f.flags) & (VC_DEVICE_RUNNING | VC_MEDIA_RUNNING)) ==
                       (VC_DEVICE_RUNNING | VC_MEDIA_RUNNING)
                   ? 0
                   : SYS_ERR_BUSY;
    }
    if (r->id.operation == LC_CLEANUP)
    {
        z = c->ops->facts(c->port, &f);
        if (z)
            return z;
        return safe(f.flags) ? 0 : SYS_ERR_BUSY;
    }
    z = c->ops->facts(c->port, &f);
    if (z)
        return z;
    return safe(c->result.observation.flags) && safe(f.flags) ? 0 : SYS_ERR_BUSY;
}
const struct V2LifecycleOps VCamera_LifecycleOps = {lc_owner, can_begin, begin, prove};
SysResult_t VCamera_Init(VCamera *c, uint32_t module, uint32_t session, const VCameraOps *ops,
                         void *port, const VCameraResources *resources)
{
    SysResult_t z;
    if (!c || c->magic || !ops || !resources || !ops->is_owner || !ops->validate_layout ||
        !ops->gate || !ops->request || !ops->read_result || !ops->consume_result || !ops->facts ||
        !ops->release_identity || !ops->begin_round || !ops->process || !ops->returns ||
        !ops->finish_round)
        return SYS_ERR_INVALID_ARG;
    if (!ops->is_owner(port))
        return SYS_ERR_INVALID_STATE;
    memset(c, 0, sizeof(*c));
    c->ops = ops;
    c->port = port;
    c->resources = *resources;
    z = LC_Init(&c->lifecycle, module, session, &VCamera_LifecycleOps, c);
    if (z)
        return z;
    c->magic = MAGIC;
    return 0;
}
SysResult_t VCamera_QueryCapabilities(VCamera *c, const CoreMediaFormat *f,
                                      VCameraCapabilities *out)
{
    SysResult_t z;
    if (!f || !out || !c || V2_Overlap(c, sizeof(*c), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    z = enter(c);
    if (z)
        return z;
    z = capabilities(c, f, out);
    return leave(c, z);
}
SysResult_t VCamera_BindCandidate(VCamera *c, const V2Registry *r, const V2CompositionCandidate *v)
{
    const V2ModuleDesc *m;
    uint32_t i;
    VCameraCapabilities cap;
    SysResult_t z;
    if (!r || !v || v->candidate_only != 1 || !v->media.pipeline_mask || !v->media.entity_mask)
        return SYS_ERR_INVALID_ARG;
    z = enter(c);
    if (z)
        return z;
    m = V2Registry_Get(r, (V2ModuleId){c->lifecycle.module});
    if (!m || !(v->modules.selected_mask & (1U << (m - r->modules))))
        return leave(c, SYS_ERR_NOT_FOUND);
    if (!v->modules.count || v->modules.count > V2_MODULE_MAX)
        return leave(c, SYS_ERR_PROTOCOL);
    for (i = 0; i < v->modules.count; i++)
        if (v->modules.order[i].value == c->lifecycle.module)
            break;
    if (i == v->modules.count)
        return leave(c, SYS_ERR_PROTOCOL);
    if (c->bound)
        return leave(c, !memcmp(&c->format, &v->media.format, sizeof(c->format))
                            ? SYS_OK
                            : SYS_ERR_NOT_SUPPORTED);
    if (c->lifecycle.state != LC_UNCONFIGURED || c->lifecycle.pending || c->work)
        return leave(c, SYS_ERR_BUSY);
    z = capabilities(c, &v->media.format, &cap);
    if (z)
        return leave(c, z);
    c->format = v->media.format;
    c->native = cap.native;
    c->bound = 1;
    return leave(c, 0);
}
SysResult_t VCamera_Request(VCamera *c, const LCRequest *r, uint32_t now)
{
    SysResult_t z = enter(c);
    if (z)
        return z;
    if (c->round != 1)
        return leave(c, SYS_ERR_INVALID_STATE);
    z = LC_Request(&c->lifecycle, r, now);
    return leave(c, z);
}
SysResult_t VCamera_BeginRound(VCamera *c)
{
    SysResult_t z = enter(c);
    if (z)
        return z;
    if (c->round)
        return leave(c, SYS_ERR_INVALID_STATE);
    c->ops->begin_round(c->port);
    c->round = 1;
    return leave(c, 0);
}
SysResult_t VCamera_Process(VCamera *c, uint32_t now)
{
    SysResult_t z = enter(c);
    if (z)
        return z;
    if (c->round != 1)
        return leave(c, SYS_ERR_INVALID_STATE);
    LC_Process(&c->lifecycle, now);
    if (c->work && !local(c->operation.id.operation) && !c->submitted && !c->decided)
    {
        {
            uint32_t state;
            SysResult_t observed;
            LC_Status(&c->lifecycle, &state, &observed);
            if (observed == SYS_ERR_TIMEOUT)
                z = SYS_ERR_TIMEOUT;
            else
                z = c->ops->request(c->port, &c->operation, &c->native);
        }
        if (!z)
            c->submitted = 1;
        else if (z != SYS_ERR_BUSY && z != SYS_ERR_NO_RESOURCE)
        {
            c->result.result = z;
            c->decided = 1;
            fault(c, z);
        }
    }
    c->ops->process(c->port);
    c->round = 2;
    return leave(c, 0);
}
SysResult_t VCamera_Returns(VCamera *c)
{
    SysResult_t z = enter(c);
    if (z)
        return z;
    if (c->round != 2)
        return leave(c, SYS_ERR_INVALID_STATE);
    c->ops->returns(c->port);
    c->round = 3;
    return leave(c, 0);
}
static void collect(VCamera *c, uint32_t now)
{
    SysResult_t z;
    VCameraResult v;
    LCCompletion done;
    uint32_t fact;
    if (!c->work)
        return;
    if (local(c->operation.id.operation) && !c->decided)
        c->decided = 1;
    if (c->submitted && !c->decided)
    {
        z = c->ops->read_result(c->port, &c->operation.id, &v);
        if (!z)
        {
            if (!LC_IdEqual(&v.id, &c->operation.id) || v.result > 0)
            {
                c->port_error = SYS_ERR_PROTOCOL;
                fault(c, c->port_error);
                return;
            }
            c->result = v;
            c->decided = 1;
            fault(c, v.result);
        }
        else if (z != SYS_ERR_NOT_FOUND && z != SYS_ERR_BUSY && z != SYS_ERR_NO_RESOURCE)
        {
            c->port_error = z;
            fault(c, z);
            return;
        }
    }
    if (c->decided && !c->delivered)
    {
        fact = c->result.result ? LC_ERROR : LC_Target(c->operation.id.operation);
        z = LC_Complete(&c->lifecycle, &c->operation.id, c->result.result, fact);
        if (!z)
            c->delivered = 1;
        else
        {
            c->port_error = z;
            fault(c, z);
        }
    }
    LC_Process(&c->lifecycle, now);
    if (LC_Read(&c->lifecycle, &c->operation.id, &done))
        return;
    fault(c, done.result);
    if (c->submitted)
    {
        z = c->ops->consume_result(c->port, &c->result);
        if (z)
        {
            c->port_error = z;
            fault(c, z);
            return;
        }
    }
    if (done.actual_result == SYS_OK)
    {
        if (done.fact == LC_INACTIVE && c->operation.id.operation == LC_CONFIGURE)
            c->prepared = 1;
        if (done.fact == LC_UNCONFIGURED)
            c->prepared = 0;
    }
    c->work = 0;
}
SysResult_t VCamera_FinishRound(VCamera *c, uint32_t now)
{
    SysResult_t z = enter(c);
    if (z)
        return z;
    if (c->round != 3)
        return leave(c, SYS_ERR_INVALID_STATE);
    c->ops->finish_round(c->port);
    collect(c, now);
    {
        VCameraObservation o;
        if (c->ops->facts(c->port, &o) == 0)
        {
            fault(c, o.media_result);
            if (o.stop_result && !c->first_stop_failure.stop_result)
            {
                c->first_stop_failure = o;
                fault(c, o.stop_result);
            }
        }
    }
    c->round = 0;
    return leave(c, 0);
}
SysResult_t VCamera_ReadCompletion(VCamera *c, const V2ExecId *id, LCCompletion *out)
{
    SysResult_t z;
    if (!c || !out || V2_Overlap(c, sizeof(*c), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    z = enter(c);
    if (z)
        return z;
    z = LC_Read(&c->lifecycle, id, out);
    return leave(c, z);
}
SysResult_t VCamera_ConfirmCompletion(VCamera *c, const LCCompletion *v)
{
    SysResult_t z = enter(c);
    if (z)
        return z;
    if (c->work)
        return leave(c, SYS_ERR_BUSY);
    z = LC_Confirm(&c->lifecycle, v);
    return leave(c, z);
}
SysResult_t VCamera_QueryStatus(VCamera *c, VCameraStatus *out)
{
    VCameraStatus v;
    SysResult_t z;
    if (!c || !out || V2_Overlap(c, sizeof(*c), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    z = enter(c);
    if (z)
        return z;
    z = LC_Status(&c->lifecycle, &v.lifecycle, &v.operation_error);
    if (z)
        return leave(c, z);
    z = c->ops->facts(c->port, &v.current);
    if (z)
        return leave(c, z);
    v.first_stop_failure = c->first_stop_failure;
    v.bound = c->bound;
    v.prepared = c->prepared;
    v.first_fault = c->first_fault;
    v.port_error = c->port_error;
    *out = v;
    return leave(c, 0);
}
SysResult_t VCamera_QueryExitEvidence(VCamera *c, const MRRelease *r, VCameraExitEvidence *out)
{
    VCameraExitEvidence v;
    MRContext *resource;
    SysResult_t z;
    if (!c || !r || !out || V2_Overlap(c, sizeof(*c), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    z = enter(c);
    if (z)
        return z;
    if (!c->bound)
        return leave(c, SYS_ERR_INVALID_STATE);
    memset(&v, 0, sizeof(v));
    v.release = *r;
    v.backend = c->format.backend;
    v.identity_result = c->ops->release_identity(c->port, r);
    resource = c->format.backend == CORE_BACKEND_RAW ? c->resources.raw_resource
                                                     : c->resources.jpeg_resource;
    v.resource_result = v.identity_result
                            ? v.identity_result
                            : (!resource ? SYS_ERR_NOT_SUPPORTED : MR_VerifyExit(resource, r));
    *out = v;
    z = v.resource_result;
    return leave(c, z);
}
