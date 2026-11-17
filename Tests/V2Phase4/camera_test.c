#include "../../CM7/ApplicationV2/Modules/Camera/camera_endpoint.h"
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            g_failure = __LINE__;                                                                  \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
uint32_t g_failure, g_trace_count, g_sizes[12], g_offsets[8], g_trace[1024][8];
static uint32_t actor, now, events;
static void trace(uint32_t op, const V2ExecId *id, SysResult_t z, uint32_t facts)
{
    uint32_t *t;
    if (g_trace_count >= 1024)
        return;
    t = g_trace[g_trace_count++];
    t[0] = op;
    t[1] = actor;
    t[2] = id ? id->request : 0;
    t[3] = id ? id->operation : 0;
    t[4] = id ? id->session : 0;
    t[5] = now;
    t[6] = (uint32_t)z;
    t[7] = facts;
}
typedef struct
{
    uint32_t backend, owned[2], reader[2], granted[2], generation, release_count, obtain_count,
        drained;
    uint8_t bytes[32];
} Backend;
static Backend raw, jpeg;
static MRContext rr, jr;
static MRGrant usb = {1, 1, 1, 11}, sd = {1, 2, 1, 12};
static MRRelease ur = {{1, 1, 1, 11}, 21}, sr = {{1, 2, 1, 12}, 22};
static bool owner(void *p)
{
    (void)p;
    return actor == VC_ACTOR_CAMERA;
}
static SysResult_t caps(void *p, MRSourceCaps *v)
{
    Backend *b = p;
    v->backend = b->backend;
    v->owners = MR_CAP_USB | MR_CAP_SD;
    v->concurrent = b->backend == MR_RAW ? 1 : 7;
    v->spans = b->backend == MR_RAW ? 1 : 2;
    return 0;
}
static SysResult_t role(void *p, const MRGrant *g, uint32_t *v)
{
    (void)p;
    if (g->consumer < 1 || g->consumer > 2)
        return SYS_ERR_NOT_SUPPORTED;
    *v = g->consumer == 1 ? MR_CAP_USB : MR_CAP_SD;
    return 0;
}
static SysResult_t grant_valid(void *p, const MRGrant *g)
{
    Backend *b = p;
    return g->session == 1 && g->generation == 1 &&
                   (MRGrant_Equal(g, &usb) || MRGrant_Equal(g, &sd)) && b->granted[g->consumer - 1]
               ? 0
               : SYS_ERR_PROTOCOL;
}
static SysResult_t obtain(void *p, const MRGrant *g, const MRHandoff *h, MRMedia *m, MRView *v)
{
    Backend *b = p;
    (void)h;
    if (b->owned[g->consumer - 1])
        return SYS_ERR_BUSY;
    b->owned[g->consumer - 1] = 1;
    b->obtain_count++;
    m->backend = b->backend;
    m->session = 3;
    m->id = 9;
    m->generation = b->generation;
    v->data = b->bytes;
    v->bytes = 16;
    v->data2 = b->backend == MR_JPEG ? b->bytes + 16 : NULL;
    v->bytes2 = b->backend == MR_JPEG ? 8 : 0;
    return 0;
}
static SysResult_t validate(void *p, const MRGrant *g, const MRMedia *m, const MRView *v)
{
    Backend *b = p;
    (void)v;
    return m->backend == b->backend && m->session == 3 && m->id == 9 &&
                   m->generation == b->generation && b->owned[g->consumer - 1]
               ? 0
               : SYS_ERR_PROTOCOL;
}
static SysResult_t readers(void *p, const MRGrant *g, const MRMedia *m)
{
    Backend *b = p;
    (void)m;
    return b->reader[g->consumer - 1] ? SYS_ERR_BUSY : 0;
}
static SysResult_t release(void *p, const MRGrant *g, const MRMedia *m)
{
    Backend *b = p;
    (void)m;
    b->owned[g->consumer - 1] = 0;
    b->release_count++;
    return 0;
}
static SysResult_t released(void *p, const MRGrant *g, const MRMedia *m)
{
    Backend *b = p;
    (void)m;
    return b->owned[g->consumer - 1] ? SYS_ERR_BUSY : 0;
}
static SysResult_t exit_safe(void *p, const MRGrant *g)
{
    Backend *b = p;
    return b->owned[g->consumer - 1] || b->reader[g->consumer - 1] ? SYS_ERR_BUSY : 0;
}
static const MRSourceOps source_ops = {owner,    caps,    role,    grant_valid, obtain,
                                       validate, readers, release, released,    exit_safe};
typedef struct
{
    VCameraObservation obs;
    LCRequest request;
    VCameraResult result;
    uint32_t pending, complete, intent, round_attempted, begin_count, media_count, returns_count,
        finish_count;
    uint32_t sensor_count, capture_count, stop_count, consume_count, request_count, notify_count;
    uint32_t fail_sensor, fail_capture, fail_stop, protect, skip_auto, resource_block,
        consumer_active[2], releasing[2];
    uint32_t native_full, reply_full, request_full, ack_full, notify_fail, result_consume_busy,
        wrong_result, gate_busy;
    uint32_t layout_limit, layout_align, delay, accepted_at;
    SysResult_t native_result, gate_error;
} Control;
static Control hw;
static VCamera camera;
static VCameraEndpoint endpoint;
static VCameraClient client;
static EMEndpoint public_endpoint;
static V2CompositionCandidate candidate;
static V2Registry registry;
static V2ModuleDesc descriptor;
static VCameraResources resources;
static int module_owner(void *p)
{
    (void)p;
    return actor == VC_ACTOR_CAMERA;
}
static SysResult_t layout(void *p, const CoreMediaFormat *f, const CameraConfig_t *n,
                          const MRSourceCaps *cap)
{
    Control *h = p;
    (void)n;
    (void)cap;
    return f->max_payload <= h->layout_limit && f->dma_alignment >= h->layout_align
               ? 0
               : SYS_ERR_NOT_SUPPORTED;
}
static int global_safe(void)
{
    uint32_t i;
    const MRRelease *rels[2] = {&ur, &sr};
    if (hw.consumer_active[0] || hw.consumer_active[1] || raw.owned[0] || raw.owned[1] ||
        jpeg.owned[0] || jpeg.owned[1] || raw.reader[0] || raw.reader[1] || jpeg.reader[0] ||
        jpeg.reader[1] || hw.resource_block)
        return 0;
    for (i = 0; i < 2; i++)
        if (hw.releasing[i])
        {
            if (raw.granted[i] && MR_VerifyExit(&rr, rels[i]) != 0)
                return 0;
            if (jpeg.granted[i] && MR_VerifyExit(&jr, rels[i]) != 0)
                return 0;
        }
    return 1;
}
static SysResult_t gate(void *p, const LCRequest *r, uint32_t stage)
{
    Control *h = p;
    trace(5, &r->id, 0, h->obs.flags);
    if (h->gate_error)
        return h->gate_error;
    if (h->gate_busy)
        return SYS_ERR_BUSY;
    if (stage == VC_GATE_BEGIN &&
        (r->id.operation == LC_ACTIVATE || r->id.operation == LC_CLEANUP ||
         r->id.operation == LC_ERROR_PROCESS) &&
        !global_safe())
        return SYS_ERR_BUSY;
    if (stage == VC_GATE_PROVE &&
        (r->id.operation == LC_DEACTIVATE || r->id.operation == LC_CLEANUP ||
         r->id.operation == LC_ERROR_PROCESS) &&
        !global_safe())
        return SYS_ERR_BUSY;
    return 0;
}
static void stop_attempt(uint32_t kind)
{
    if (hw.round_attempted)
        return;
    hw.round_attempted = 1;
    hw.stop_count++;
    hw.obs.stop_sequence++;
    hw.obs.stop_kind = kind;
    hw.obs.stop_result = hw.fail_stop ? SYS_ERR_IO : 0;
    trace(kind == 2 ? 31 : 30, &hw.request.id, hw.obs.stop_result, hw.obs.flags);
    if (!hw.fail_stop)
    {
        hw.obs.flags = VC_STOP_MASK;
        hw.intent = 0;
    }
}
static SysResult_t control_request(void *p, const LCRequest *r, const CameraConfig_t *n)
{
    Control *h = p;
    (void)n;
    if (h->native_full)
        return SYS_ERR_NO_RESOURCE;
    h->request = *r;
    h->result.id = r->id;
    h->result.result = 0;
    h->pending = 1;
    h->complete = 0;
    h->request_count++;
    h->accepted_at = now;
    trace(10, &r->id, 0, h->obs.flags);
    if (r->id.operation == LC_ACTIVATE)
    {
        h->sensor_count++;
        h->obs.flags = VC_DEVICE_RUNNING;
        if (!h->fail_sensor)
        {
            h->capture_count++;
            if (!h->fail_capture)
                h->obs.flags |= VC_MEDIA_RUNNING;
        }
        if (h->fail_sensor || h->fail_capture)
        {
            h->result.result = SYS_ERR_IO;
            h->complete = 1;
            h->intent = 1;
            stop_attempt(2);
            h->result.observation = h->obs;
        }
    }
    else if (r->id.operation == LC_DEACTIVATE)
        h->intent = 1;
    return 0;
}
static SysResult_t read_result(void *p, const V2ExecId *id, VCameraResult *v)
{
    Control *h = p;
    if (!h->pending || !h->complete || !LC_IdEqual(id, &h->result.id))
        return SYS_ERR_NOT_FOUND;
    *v = h->result;
    if (h->wrong_result)
        v->id.request++;
    return 0;
}
static SysResult_t consume_result(void *p, const VCameraResult *v)
{
    Control *h = p;
    if (h->result_consume_busy)
        return SYS_ERR_NO_RESOURCE;
    if (!h->pending || memcmp(v, &h->result, sizeof(*v)))
        return SYS_ERR_PROTOCOL;
    h->pending = 0;
    h->consume_count++;
    return 0;
}
static SysResult_t facts(void *p, VCameraObservation *o)
{
    Control *h = p;
    *o = h->obs;
    return 0;
}
static SysResult_t release_identity(void *p, const MRRelease *r)
{
    Control *h = p;
    return (r->request != r->grant.acquire) && ((!memcmp(r, &ur, sizeof(*r)) && h->releasing[0]) ||
                                                (!memcmp(r, &sr, sizeof(*r)) && h->releasing[1]))
               ? 0
               : SYS_ERR_PROTOCOL;
}
static void begin_round(void *p)
{
    Control *h = p;
    h->begin_count++;
    h->round_attempted = 0;
    trace(1, NULL, 0, h->obs.flags);
}
static void media_process(void *p)
{
    Control *h = p;
    h->media_count++;
    trace(20, NULL, 0, h->obs.flags);
    if (h->protect)
    {
        h->obs.media_result = SYS_ERR_IO;
        stop_attempt(2);
        h->protect = 0;
    }
}
static void return_process(void *p)
{
    Control *h = p;
    h->returns_count++;
    trace(21, NULL, 0, h->obs.flags);
}
static void finish_round(void *p)
{
    Control *h = p;
    h->finish_count++;
    trace(22, NULL, 0, h->obs.flags);
    if (h->intent && global_safe())
        stop_attempt(1);
    if (h->pending && !h->complete && !h->skip_auto && now - h->accepted_at >= h->delay)
    {
        if (h->request.id.operation == LC_ACTIVATE &&
            (h->obs.flags & (VC_DEVICE_RUNNING | VC_MEDIA_RUNNING)) ==
                (VC_DEVICE_RUNNING | VC_MEDIA_RUNNING))
            h->complete = 1;
        if (h->request.id.operation == LC_DEACTIVATE &&
            ((h->obs.flags & VC_STOP_MASK) == VC_STOP_MASK || h->obs.stop_result))
            h->complete = 1;
        if (h->request.id.operation == LC_ERROR_PROCESS && global_safe())
            h->complete = 1;
        if (h->complete)
        {
            h->result.result = h->request.id.operation == LC_DEACTIVATE ? h->obs.stop_result : 0;
            h->result.observation = h->obs;
        }
    }
}
static const VCameraOps control_ops = {
    module_owner, layout,           gate,        control_request, read_result,    consume_result,
    facts,        release_identity, begin_round, media_process,   return_process, finish_round};
/* Three single-entry reliable queues; notification may fail or coalesce.
 * Serial actor fixture only, not FreeRTOS interleaving proof. */
static LCRequest command;
static VCameraReply reply;
static LCCompletion ack;
static uint32_t command_valid, reply_valid, ack_valid;
static int is_camera(void *p)
{
    (void)p;
    return actor == 1;
}
static int is_supervisor(void *p)
{
    (void)p;
    return actor == 2;
}
static SysResult_t send_request(void *p, const LCRequest *v)
{
    (void)p;
    if (actor != 2)
        return SYS_ERR_INVALID_STATE;
    if (command_valid || hw.request_full)
        return SYS_ERR_NO_RESOURCE;
    command = *v;
    command_valid = 1;
    events |= 1;
    trace(40, &v->id, 0, 0);
    return 0;
}
static SysResult_t receive_request(void *p, LCRequest *v)
{
    (void)p;
    if (actor != 1)
        return SYS_ERR_INVALID_STATE;
    if (!command_valid)
        return SYS_ERR_NOT_FOUND;
    *v = command;
    command_valid = 0;
    return 0;
}
static SysResult_t send_reply(void *p, const VCameraReply *v)
{
    (void)p;
    if (actor != 1)
        return SYS_ERR_INVALID_STATE;
    if (reply_valid || hw.reply_full)
        return SYS_ERR_NO_RESOURCE;
    reply = *v;
    reply_valid = 1;
    events |= 2;
    trace(41, &v->completion.id, 0, 0);
    return 0;
}
static SysResult_t receive_reply(void *p, VCameraReply *v)
{
    (void)p;
    if (actor != 2)
        return SYS_ERR_INVALID_STATE;
    if (!reply_valid)
        return SYS_ERR_NOT_FOUND;
    *v = reply;
    reply_valid = 0;
    return 0;
}
static SysResult_t send_ack(void *p, const LCCompletion *v)
{
    (void)p;
    if (actor != 2)
        return SYS_ERR_INVALID_STATE;
    if (ack_valid || hw.ack_full)
        return SYS_ERR_NO_RESOURCE;
    ack = *v;
    ack_valid = 1;
    events |= 1;
    trace(42, &v->id, 0, 0);
    return 0;
}
static SysResult_t receive_ack(void *p, LCCompletion *v)
{
    (void)p;
    if (actor != 1)
        return SYS_ERR_INVALID_STATE;
    if (!ack_valid)
        return SYS_ERR_NOT_FOUND;
    *v = ack;
    ack_valid = 0;
    return 0;
}
static SysResult_t notify(void *p, uint32_t target)
{
    (void)p;
    (void)target;
    hw.notify_count++;
    return hw.notify_fail ? SYS_ERR_IO : 0;
}
static const VCameraTransportOps transport_ops = {is_camera,       is_supervisor, send_request,
                                                  receive_request, send_reply,    receive_reply,
                                                  send_ack,        receive_ack,   notify};
static CoreMediaFormat raw_format = {1, 3, 1, 160, 120, 320, 38400, 38400, 1, 1, 1, 32, 32, 0};
static CoreMediaFormat jpeg_format = {1, 4, 2, 320, 240, 0, 50000, 50000, 1, 2, 2, 4, 1, 0};
static LCRequest request(uint32_t seq, uint32_t op, uint32_t expected)
{
    LCRequest r;
    memset(&r, 0, sizeof(r));
    r.id.session = 7;
    r.id.module = 81;
    r.id.request = seq;
    r.id.operation = op;
    r.expected = expected;
    r.timeout = 100;
    return r;
}
static void rounds(uint32_t n)
{
    while (n--)
    {
        actor = 1;
        VCameraEndpoint_RunRound(&endpoint, now++);
    }
}
static SysResult_t setup(uint32_t backend)
{
    VCameraTransport t = {&transport_ops, NULL};
    memset(&camera, 0, sizeof(camera));
    memset(&endpoint, 0, sizeof(endpoint));
    memset(&client, 0, sizeof(client));
    memset(&hw, 0, sizeof(hw));
    memset(&raw, 0, sizeof(raw));
    memset(&jpeg, 0, sizeof(jpeg));
    memset(&rr, 0, sizeof(rr));
    memset(&jr, 0, sizeof(jr));
    now = 0;
    command_valid = reply_valid = ack_valid = events = 0;
    raw.backend = MR_RAW;
    jpeg.backend = MR_JPEG;
    raw.generation = jpeg.generation = 1;
    hw.layout_limit = 100000;
    hw.layout_align = backend == MR_RAW ? 32 : 4;
    hw.obs.flags = VC_COLD_MASK;
    resources.raw_source.ops = resources.jpeg_source.ops = &source_ops;
    resources.raw_source.context = &raw;
    resources.jpeg_source.context = &jpeg;
    resources.raw_resource = &rr;
    resources.jpeg_resource = &jr;
    actor = 1;
    if (MR_Init(&rr, &resources.raw_source) || MR_Init(&jr, &resources.jpeg_source))
        return SYS_ERR_IO;
    if (VCamera_Init(&camera, 81, 7, &control_ops, &hw, &resources))
        return SYS_ERR_IO;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.id.value = 81;
    descriptor.major = 1;
    descriptor.lifecycle_ops = &VCamera_LifecycleOps;
    registry.modules = &descriptor;
    registry.count = 1;
    registry.reserved = 0;
    memset(&candidate, 0, sizeof(candidate));
    candidate.candidate_only = 1;
    candidate.modules.selected_mask = 1;
    candidate.modules.count = 1;
    candidate.modules.order[0].value = 81;
    candidate.media.pipeline_mask = 1;
    candidate.media.entity_mask = 1;
    candidate.media.format = backend == MR_RAW ? raw_format : jpeg_format;
    if (VCamera_BindCandidate(&camera, &registry, &candidate) ||
        VCameraEndpoint_Init(&endpoint, &camera, &t))
        return SYS_ERR_IO;
    actor = 2;
    if (VCameraClient_Init(&client, 81, 7, &t))
        return SYS_ERR_IO;
    return VCameraClient_Endpoint(&client, &public_endpoint);
}
static int transact(uint32_t seq, uint32_t op, uint32_t expected, LCCompletion *out)
{
    LCRequest r = request(seq, op, expected);
    uint32_t i;
    actor = 2;
    if (public_endpoint.submit(public_endpoint.port, &r))
        return 0;
    for (i = 0; i < 20; i++)
    {
        rounds(1);
        actor = 2;
        if (public_endpoint.read(public_endpoint.port, &r.id, out) == 0)
        {
            if (public_endpoint.consume(public_endpoint.port, out))
                return 0;
            rounds(1);
            actor = 2;
            return 1;
        }
    }
    return 0;
}
static int active(void)
{
    LCCompletion v;
    return transact(1, LC_CONFIGURE, LC_UNCONFIGURED, &v) && !v.result &&
           transact(2, LC_ACTIVATE, LC_INACTIVE, &v) && !v.result;
}
static SysResult_t terminal_send(void *p, const MRReceipt *t)
{
    (void)p;
    (void)t;
    return 0;
}
static int loan(Backend *b, MRContext *c, MRGrant *g, MRRequest *r, MRMedia *m)
{
    MRBridge bridge = {1, 0, 7};
    MRSlot slot = {1, 2, 1};
    MRView view;
    b->granted[g->consumer - 1] = 1;
    hw.consumer_active[g->consumer - 1] = 1;
    return !MR_AssociateGrant(c, g) && !MR_Stage(c, g, &bridge, &slot, r) && !MR_Commit(c, r) &&
           !MR_Admit(c, r) && !MR_Obtain(c, r, m, &view);
}
static int drain(MRContext *c, const MRRequest *r, const MRMedia *m, const MRRelease *rel)
{
    MRReceipt t;
    if (MR_Return(c, r, m) || MR_ReadReceipt(c, r, &t) ||
        MR_FlushReceipt(c, &t, terminal_send, NULL) || MR_ConfirmConsumed(c, &t) ||
        MR_Retire(c, &t))
        return 0;
    hw.releasing[rel->grant.consumer - 1] = 1;
    if (MR_Close(c, rel))
        return 0;
    return 1;
}
static SysResult_t graph_format(const GraphEntityDesc *e, const GraphFormat *f,
                                const GraphCheckContext *c, GraphIssue *i)
{
    CameraConfig_t n;
    (void)e;
    (void)c;
    (void)i;
    return VCamera_Translate(f, &n);
}
static SysResult_t graph_sink(const GraphEntityDesc *e, const GraphFormat *f,
                              const GraphFrameRate *r, const GraphCheckContext *c, GraphIssue *i)
{
    (void)e;
    (void)f;
    (void)c;
    (void)i;
    return r->num == 10 && r->den == 1 ? 0 : SYS_ERR_NOT_SUPPORTED;
}
static SysResult_t resource_preflight(const GraphValidatedView *v, const V2Resolution *r,
                                      V2Issue *i)
{
    CameraConfig_t n;
    (void)r;
    (void)i;
    return VCamera_Translate(&v->format, &n);
}
static V2CompositionScratch composition_scratch;
static GraphEntityDesc entities[4];
static GraphPadDesc pads[6];
static GraphLinkDesc link[3];
static GraphPipelineDesc pipeline;
static GraphCaps graph_caps;
static GraphDesc graph;
static GraphSelection selection;
static GraphFrameRate graph_rate = {10, 1};
static const GraphCheckContext graph_context = {1, 1};
static const GraphEntityOps graph_ops = {graph_format, graph_sink};
static int composed(void)
{
    V2EntityBinding binding[4] = {{{1}, 0, {81}}, {{2}, 0, {81}}, {{3}, 0, {81}}, {{4}, 0, {81}}};
    V2ModuleId root = {81};
    V2Desired desired;
    V2Issue issue;
    GraphIssue ge;
    uint32_t i, j, first = 0;
    SysResult_t result;
    memset(&composition_scratch, 0, sizeof(composition_scratch));
    memset(entities, 0, sizeof(entities));
    memset(pads, 0, sizeof(pads));
    memset(&pipeline, 0, sizeof(pipeline));
    memset(&graph_caps, 0, sizeof(graph_caps));
    memset(&selection, 0, sizeof(selection));
    memset(link, 0, sizeof(link));
    memset(&graph, 0, sizeof(graph));
    for (i = 0; i < 4; i++)
    {
        GraphEntityDesc *e = &entities[i];
        e->id.value = i + 1;
        e->kind = i == 0   ? GRAPH_ENTITY_HW
                  : i == 1 ? GRAPH_ENTITY_CAPTURE
                  : i == 2 ? GRAPH_ENTITY_PROVIDER
                           : GRAPH_ENTITY_CONSUMER;
        e->name = "controlled";
        e->pad_first = first;
        e->pad_count = (i == 0 || i == 3) ? 1 : 2;
        e->flags = GRAPH_E_IMPLEMENTED | (i == 3 ? GRAPH_E_TERMINAL : 0);
        e->ops = &graph_ops;
        for (j = 0; j < e->pad_count; j++)
        {
            GraphPadDesc *p = &pads[first + j];
            p->id.value = j;
            p->parent = e->id;
            p->direction = i == 0   ? GRAPH_PAD_SOURCE
                           : i == 3 ? GRAPH_PAD_SINK
                           : j == 0 ? GRAPH_PAD_SINK
                                    : GRAPH_PAD_SOURCE;
            p->protocol = i == 0 || (i == 1 && j == 0)               ? GRAPH_PROTO_SENSOR_BYTES
                          : (i == 1 && j == 1) || (i == 2 && j == 0) ? GRAPH_PROTO_DMA_CHUNKS
                                                                     : GRAPH_PROTO_FRAME_VIEW;
            p->caps.value = 1;
            p->max_links = 1;
            p->flags = p->direction == GRAPH_PAD_SINK ? 2 : 0;
        }
        first += e->pad_count;
    }
    for (i = 0; i < 3; i++)
    {
        link[i].id.value = i + 1;
        link[i].flags = 1;
        link[i].source.entity.value = i + 1;
        link[i].source.pad.value = i ? 1 : 0;
        link[i].sink.entity.value = i + 2;
    }
    pipeline.id.value = 1;
    pipeline.name = "controlled";
    pipeline.source.value = 1;
    pipeline.backend = CORE_BACKEND_RAW;
    pipeline.sinks[0].value = 4;
    pipeline.sink_count = 1;
    pipeline.link_count = 3;
    for (i = 0; i < 3; i++)
        pipeline.links[i].value = i + 1;
    pipeline.default_format = &raw_format;
    pipeline.default_sink_rates = &graph_rate;
    graph_caps.media_mask = 1;
    graph_caps.pixel_mask = 4;
    graph_caps.backend_mask = 1;
    graph_caps.width_min = graph_caps.width_max = 160;
    graph_caps.height_min = graph_caps.height_max = 120;
    graph_caps.width_step = graph_caps.height_step = 1;
    graph_caps.max_payload = 38400;
    graph_caps.max_spans = 1;
    graph_caps.max_fps_num = 10;
    graph_caps.max_fps_den = 1;
    graph_caps.dma_alignment = graph_caps.frame_alignment = 32;
    graph.schema_version = 1;
    graph.entities = entities;
    graph.pads = pads;
    graph.links = link;
    graph.pipelines = &pipeline;
    graph.caps = &graph_caps;
    graph.entity_count = 4;
    graph.pad_count = 6;
    graph.link_count = 3;
    graph.pipeline_count = graph.caps_count = 1;
    graph.check_context = &graph_context;
    selection.pipeline.value = 1;
    selection.format = raw_format;
    selection.sink_policy_count = 1;
    selection.sink_policies[0].sink.value = 4;
    selection.sink_policies[0].rate = graph_rate;
    desired.roots = &root;
    desired.root_count = 1;
    desired.selections = &selection;
    desired.selection_count = 1;
    result = V2Composition_Validate(&registry, &graph, binding, 4, &desired, resource_preflight,
                                    &composition_scratch, &candidate, &issue, &ge);
    trace(90, NULL, result, ge.code);
    if (result)
    {
        uint32_t *t = g_trace[g_trace_count - 1];
        t[2] = ge.field;
        t[3] = ge.expected;
        t[4] = ge.actual;
    }
    return result == 0;
}
static EMContext supervisor;
static EMPlan execution_plan;
static int supervisor_owner(void *p)
{
    (void)p;
    return actor == 2;
}
int Camera_Run(uint32_t id)
{
    LCRequest r;
    LCCompletion done, copy;
    CameraConfig_t n;
    VCameraStatus st;
    VCameraCapabilities cap;
    VCameraExitEvidence exit;
    MRRequest h1, h2;
    MRMedia m1, m2;
    uint32_t i, count;
    SysResult_t z;
    g_failure = g_trace_count = 0;
    CHECK(setup((id >= 35 && id <= 43) || id == 76 ? MR_JPEG : MR_RAW) == 0);
    switch (id)
    {
    case 0:
        g_sizes[0] = sizeof(VCamera);
        g_sizes[1] = sizeof(VCameraEndpoint);
        g_sizes[2] = sizeof(VCameraClient);
        g_sizes[3] = sizeof(VCameraOps);
        g_sizes[4] = sizeof(VCameraTransportOps);
        g_sizes[5] = sizeof(VCameraStatus);
        g_sizes[6] = sizeof(VCameraResult);
        g_sizes[7] = sizeof(VCameraReply);
        g_sizes[8] = sizeof(VCameraCapabilities);
        g_sizes[9] = sizeof(VCameraExitEvidence);
        g_sizes[10] = sizeof(CameraConfig_t);
        g_sizes[11] = sizeof(LCCompletion);
        g_offsets[0] = offsetof(VCamera, lifecycle);
        g_offsets[1] = offsetof(VCamera, format);
        g_offsets[2] = offsetof(VCamera, native);
        g_offsets[3] = offsetof(VCameraResult, observation);
        g_offsets[4] = offsetof(VCameraReply, delivery_error);
        g_offsets[5] = offsetof(VCameraClient, request);
        g_offsets[6] = offsetof(VCameraEndpoint, ack);
        g_offsets[7] = sizeof(CoreMediaFormat);
        CHECK(sizeof(CameraConfig_t) == 6 && offsetof(CameraConfig_t, format) == 4);
        break;
    case 1:
        CHECK(VCamera_Translate(&raw_format, &n) == 0 && n.format == CAMERA_PIXEL_FORMAT_YUV422);
        break;
    case 2:
    {
        CoreMediaFormat f = raw_format;
        f.pixel_format = CORE_PIXEL_Y8;
        f.bytes_per_line = 160;
        f.size_image = f.max_payload = 19200;
        CHECK(VCamera_Translate(&f, &n) == 0 && n.format == CAMERA_PIXEL_FORMAT_Y8);
    }
    break;
    case 3:
    {
        CoreMediaFormat f = raw_format;
        f.pixel_format = CORE_PIXEL_RGB565;
        CHECK(VCamera_Translate(&f, &n) == 0 && n.format == CAMERA_PIXEL_FORMAT_RGB565);
    }
    break;
    case 4:
        CHECK(VCamera_Translate(&jpeg_format, &n) == 0 && n.format == CAMERA_PIXEL_FORMAT_JPEG);
        break;
    case 5:
    {
        CoreMediaFormat f = raw_format;
        f.width = 320;
        f.height = 240;
        f.bytes_per_line = 640;
        f.size_image = f.max_payload = 153600;
        CHECK(VCamera_Translate(&f, &n) == SYS_ERR_NOT_SUPPORTED);
    }
    break;
    case 6:
    {
        CoreMediaFormat f = raw_format;
        f.bytes_per_line++;
        CHECK(VCamera_Translate(&f, &n) == SYS_ERR_INVALID_ARG);
    }
    break;
    case 7:
    {
        CoreMediaFormat f = jpeg_format;
        f.width = 800;
        CHECK(VCamera_Translate(&f, &n) == SYS_ERR_NOT_SUPPORTED);
    }
    break;
    case 8:
        actor = 1;
        CHECK(VCamera_QueryCapabilities(&camera, &raw_format, &cap) == 0 &&
              cap.resource.backend == MR_RAW);
        CHECK(!hw.sensor_count && !hw.capture_count && !hw.stop_count);
        break;
    case 9:
        actor = 1;
        hw.layout_limit = 20000;
        CHECK(VCamera_QueryCapabilities(&camera, &raw_format, &cap) == SYS_ERR_NOT_SUPPORTED);
        break;
    case 10:
        actor = 1;
        candidate.media.format = jpeg_format;
        CHECK(VCamera_BindCandidate(&camera, &registry, &candidate) == SYS_ERR_NOT_SUPPORTED);
        break;
    case 11:
        CHECK(transact(1, LC_CONFIGURE, LC_UNCONFIGURED, &done) && done.fact == LC_INACTIVE &&
              !done.result);
        CHECK(!hw.sensor_count && !hw.capture_count && !hw.stop_count && camera.prepared);
        break;
    case 12:
        CHECK(active());
        CHECK(hw.sensor_count == 1 && hw.capture_count == 1 && camera.lifecycle.state == LC_ACTIVE);
        break;
    case 13:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        CHECK(public_endpoint.submit(public_endpoint.port, &r) == 0);
        CHECK(camera.lifecycle.state == LC_UNCONFIGURED && command_valid);
        rounds(1);
        CHECK(camera.lifecycle.state == LC_INACTIVE && reply_valid);
        CHECK(camera.lifecycle.pending);
        break;
    case 14:
        hw.request_full = 1;
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        CHECK(public_endpoint.submit(public_endpoint.port, &r) == SYS_ERR_NO_RESOURCE &&
              !client.active);
        hw.request_full = 0;
        CHECK(public_endpoint.submit(public_endpoint.port, &r) == 0);
        CHECK(public_endpoint.submit(public_endpoint.port, &r) == 0);
        CHECK(hw.notify_count == 1);
        break;
    case 15:
        hw.reply_full = 1;
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(5);
        CHECK(camera.lifecycle.state == LC_INACTIVE && !reply_valid && camera.lifecycle.pending);
        hw.reply_full = 0;
        rounds(1);
        CHECK(reply_valid);
        break;
    case 16:
        hw.notify_fail = 1;
        CHECK(active());
        CHECK(hw.sensor_count == 1 && hw.capture_count == 1);
        break;
    case 17:
        hw.ack_full = 1;
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(2);
        actor = 2;
        CHECK(public_endpoint.read(public_endpoint.port, &r.id, &done) == 0);
        CHECK(public_endpoint.consume(public_endpoint.port, &done) == SYS_ERR_NO_RESOURCE &&
              client.active);
        hw.ack_full = 0;
        CHECK(public_endpoint.consume(public_endpoint.port, &done) == 0);
        CHECK(camera.lifecycle.pending);
        rounds(1);
        CHECK(!camera.lifecycle.pending);
        break;
    case 18:
        CHECK(active());
        r = request(2, LC_ACTIVATE, LC_INACTIVE);
        CHECK(public_endpoint.submit(public_endpoint.port, &r) == 0);
        rounds(3);
        CHECK(hw.sensor_count == 1 && hw.capture_count == 1);
        break;
    case 19:
        CHECK(active());
        hw.fail_sensor = 1; /* next operation is STOP, not a hidden sensor reset */
        CHECK(transact(3, LC_DEACTIVATE, LC_ACTIVE, &done));
        CHECK(hw.stop_count == 1 && done.fact == LC_INACTIVE);
        break;
    case 20:
        CHECK(transact(1, LC_CONFIGURE, LC_UNCONFIGURED, &done));
        hw.fail_sensor = 1;
        CHECK(transact(2, LC_ACTIVATE, LC_INACTIVE, &done));
        CHECK(done.result == SYS_ERR_IO && done.fact == LC_ERROR && hw.sensor_count == 1 &&
              hw.capture_count == 0);
        CHECK(hw.stop_count == 1);
        break;
    case 21:
        CHECK(transact(1, LC_CONFIGURE, LC_UNCONFIGURED, &done));
        hw.fail_capture = 1;
        CHECK(transact(2, LC_ACTIVATE, LC_INACTIVE, &done));
        CHECK(done.result == SYS_ERR_IO && done.fact == LC_ERROR && hw.capture_count == 1 &&
              hw.stop_count == 1);
        break;
    case 22:
        CHECK(transact(1, LC_CONFIGURE, LC_UNCONFIGURED, &done));
        hw.fail_capture = hw.fail_stop = 1;
        r = request(2, LC_ACTIVATE, LC_INACTIVE);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(1);
        CHECK(hw.stop_count == 1);
        actor = 1;
        CHECK(VCamera_QueryStatus(&camera, &st) == 0 &&
              st.first_stop_failure.stop_result == SYS_ERR_IO);
        rounds(1);
        CHECK(hw.stop_count == 2);
        break;
    case 23:
        CHECK(active());
        CHECK(transact(3, LC_DEACTIVATE, LC_ACTIVE, &done));
        CHECK(done.fact == LC_INACTIVE && !done.result && hw.stop_count == 1);
        break;
    case 24:
        CHECK(active());
        hw.consumer_active[0] = 1;
        r = request(3, LC_DEACTIVATE, LC_ACTIVE);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(5);
        CHECK(hw.stop_count == 0 && camera.lifecycle.state == 19);
        hw.consumer_active[0] = 0;
        rounds(1);
        CHECK(hw.stop_count == 1 && camera.lifecycle.state == LC_INACTIVE);
        break;
    case 25:
        CHECK(active());
        raw.owned[1] = 1;
        r = request(3, LC_DEACTIVATE, LC_ACTIVE);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(4);
        CHECK(!hw.stop_count);
        raw.owned[1] = 0;
        rounds(1);
        CHECK(hw.stop_count == 1);
        break;
    case 26:
        CHECK(active());
        hw.fail_stop = 1;
        CHECK(transact(3, LC_DEACTIVATE, LC_ACTIVE, &done));
        CHECK(done.result == SYS_ERR_IO && done.fact == LC_ERROR);
        copy = done;
        hw.fail_stop = 0;
        rounds(3);
        actor = 1;
        CHECK(VCamera_ReadCompletion(&camera, &done.id, &done) == 0 &&
              !memcmp(&done, &copy, sizeof(done)));
        CHECK(camera.lifecycle.state == LC_ERROR);
        break;
    case 27:
        CHECK(active());
        hw.fail_stop = 1;
        CHECK(transact(3, LC_DEACTIVATE, LC_ACTIVE, &done));
        hw.fail_stop = 0;
        rounds(2);
        CHECK(transact(4, LC_ERROR_PROCESS, LC_ERROR, &done) && done.fact == LC_UNCONFIGURED &&
              !done.result);
        CHECK(camera.first_fault == SYS_ERR_IO);
        break;
    case 28:
        CHECK(active());
        hw.skip_auto = 1;
        r = request(3, LC_DEACTIVATE, LC_ACTIVE);
        r.timeout = 2;
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(5);
        CHECK(camera.lifecycle.pending && !camera.lifecycle.terminal && hw.stop_count == 1);
        hw.skip_auto = 0;
        rounds(1);
        actor = 2;
        CHECK(public_endpoint.read(public_endpoint.port, &r.id, &done) == 0 &&
              done.result == SYS_ERR_TIMEOUT && done.actual_result == 0);
        break;
    case 29:
        CHECK(active());
        hw.protect = 1;
        r = request(3, LC_DEACTIVATE, LC_ACTIVE);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(1);
        CHECK(hw.stop_count == 1 && hw.obs.stop_kind == 2 && hw.returns_count == hw.media_count);
        break;
    case 30:
        CHECK(transact(1, LC_CONFIGURE, LC_UNCONFIGURED, &done));
        CHECK(transact(2, LC_CLEANUP, LC_INACTIVE, &done));
        CHECK(!hw.sensor_count && !hw.capture_count && !hw.stop_count &&
              done.fact == LC_UNCONFIGURED);
        break;
    case 31:
        CHECK(active());
        CHECK(transact(3, LC_DEACTIVATE, LC_ACTIVE, &done));
        CHECK(transact(4, LC_CLEANUP, LC_INACTIVE, &done));
        CHECK(transact(5, LC_CONFIGURE, LC_UNCONFIGURED, &done));
        CHECK(transact(6, LC_ACTIVATE, LC_INACTIVE, &done));
        CHECK(hw.sensor_count == 2 && hw.capture_count == 2);
        break;
    case 32:
        CHECK(active());
        actor = 1;
        CHECK(VCamera_Process(&camera, now) == SYS_ERR_INVALID_STATE);
        CHECK(VCamera_BeginRound(&camera) == 0);
        CHECK(VCamera_FinishRound(&camera, now) == SYS_ERR_INVALID_STATE);
        CHECK(VCamera_Process(&camera, now) == 0 && VCamera_Returns(&camera) == 0 &&
              VCamera_FinishRound(&camera, now) == 0);
        break;
    case 33:
        actor = 2;
        CHECK(VCamera_QueryStatus(&camera, &st) == SYS_ERR_INVALID_STATE);
        actor = 1;
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        CHECK(public_endpoint.submit(public_endpoint.port, &r) == SYS_ERR_INVALID_STATE);
        break;
    case 34:
        CHECK(transact(1, LC_CONFIGURE, LC_UNCONFIGURED, &done));
        hw.reply_full = 1;
        r = request(2, LC_ACTIVATE, LC_INACTIVE);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(10);
        CHECK(hw.sensor_count == 1 && hw.capture_count == 1 && camera.lifecycle.state == LC_ACTIVE);
        hw.reply_full = 0;
        rounds(1);
        CHECK(reply_valid);
        break;
    case 35:
        CHECK(active());
        actor = 1;
        CHECK(loan(&jpeg, &jr, &usb, &h1, &m1));
        CHECK(loan(&jpeg, &jr, &sd, &h2, &m2));
        CHECK(drain(&jr, &h1, &m1, &ur));
        hw.consumer_active[0] = 0;
        CHECK(VCamera_QueryExitEvidence(&camera, &ur, &exit) == 0);
        CHECK(jpeg.owned[1] && camera.lifecycle.state == LC_ACTIVE && !hw.stop_count);
        break;
    case 36:
        CHECK(active());
        actor = 1;
        CHECK(loan(&jpeg, &jr, &usb, &h1, &m1));
        CHECK(loan(&jpeg, &jr, &sd, &h2, &m2));
        CHECK(drain(&jr, &h2, &m2, &sr));
        hw.consumer_active[1] = 0;
        CHECK(VCamera_QueryExitEvidence(&camera, &sr, &exit) == 0);
        CHECK(jpeg.owned[0] && !hw.stop_count);
        break;
    case 37:
        CHECK(active());
        actor = 1;
        CHECK(loan(&jpeg, &jr, &usb, &h1, &m1));
        hw.releasing[0] = 1;
        CHECK(MR_Close(&jr, &ur) == 0);
        CHECK(VCamera_QueryExitEvidence(&camera, &ur, &exit) == SYS_ERR_BUSY);
        CHECK(jpeg.owned[0]);
        break;
    case 38:
        CHECK(active());
        actor = 1;
        CHECK(loan(&jpeg, &jr, &usb, &h1, &m1));
        hw.releasing[0] = 1;
        CHECK(MR_Close(&jr, &ur) == 0);
        CHECK(MR_Return(&jr, &h1, &m1) == 0);
        CHECK(VCamera_QueryExitEvidence(&camera, &ur, &exit) == SYS_ERR_BUSY && !jpeg.owned[0]);
        break;
    case 39:
        CHECK(active());
        actor = 1;
        CHECK(loan(&jpeg, &jr, &usb, &h1, &m1));
        hw.releasing[0] = 1;
        CHECK(MR_Close(&jr, &ur) == 0);
        {
            MRGrant old = usb;
            old.generation--;
            MRRequest newer;
            MRBridge b = {1, 0, 5};
            MRSlot slot = {0, 0, 0};
            CHECK(MR_Stage(&jr, &old, &b, &slot, &newer) == SYS_ERR_INVALID_ARG);
        }
        break;
    case 40:
        CHECK(active());
        actor = 1;
        CHECK(loan(&jpeg, &jr, &usb, &h1, &m1));
        CHECK(drain(&jr, &h1, &m1, &ur));
        {
            MRRelease bad = ur;
            bad.request = usb.acquire;
            CHECK(VCamera_QueryExitEvidence(&camera, &bad, &exit) == SYS_ERR_PROTOCOL);
        }
        break;
    case 41:
        CHECK(active());
        actor = 1;
        CHECK(loan(&jpeg, &jr, &usb, &h1, &m1));
        jpeg.reader[0] = 1;
        CHECK(MR_Return(&jr, &h1, &m1) == SYS_ERR_BUSY && jpeg.owned[0]);
        CHECK(!hw.stop_count);
        break;
    case 42:
        CHECK(active());
        actor = 1;
        CHECK(loan(&jpeg, &jr, &usb, &h1, &m1));
        {
            MRBridge b = {2, 8, 7};
            CHECK(MR_BindBridge(&jr, &h1, &b) == SYS_ERR_INVALID_ARG);
            b.epoch = 1;
            CHECK(MR_BindBridge(&jr, &h1, &b) == 0);
            b.lease++;
            CHECK(MR_BindBridge(&jr, &h1, &b) == SYS_ERR_PROTOCOL);
        }
        CHECK(drain(&jr, &h1, &m1, &ur));
        CHECK(MR_Return(&jr, &h1, &m1) == SYS_ERR_INVALID_ARG);
        break;
    case 43:
        CHECK(active());
        actor = 1;
        CHECK(loan(&jpeg, &jr, &usb, &h1, &m1));
        CHECK(loan(&jpeg, &jr, &sd, &h2, &m2));
        actor = 2;
        r = request(3, LC_DEACTIVATE, LC_ACTIVE);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(4);
        CHECK(!hw.stop_count);
        actor = 1;
        CHECK(drain(&jr, &h1, &m1, &ur) && drain(&jr, &h2, &m2, &sr));
        hw.consumer_active[0] = hw.consumer_active[1] = 0;
        rounds(2);
        CHECK(hw.stop_count == 1 && camera.lifecycle.state == LC_INACTIVE);
        break;
    case 44:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        public_endpoint.submit(public_endpoint.port, &r);
        memset(&reply, 0, sizeof(reply));
        reply.completion.id = r.id;
        reply.completion.id.session--;
        reply_valid = 1;
        CHECK(public_endpoint.read(public_endpoint.port, &r.id, &done) == SYS_ERR_NOT_FOUND &&
              !client.has_reply);
        rounds(2);
        actor = 2;
        CHECK(public_endpoint.read(public_endpoint.port, &r.id, &done) == 0);
        break;
    case 45:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(2);
        actor = 2;
        public_endpoint.read(public_endpoint.port, &r.id, &done);
        copy = done;
        copy.id.request++;
        CHECK(public_endpoint.consume(public_endpoint.port, &copy) == SYS_ERR_PROTOCOL &&
              camera.lifecycle.pending);
        break;
    case 46:
        CHECK(transact(1, LC_CONFIGURE, LC_UNCONFIGURED, &done));
        hw.wrong_result = 1;
        r = request(2, LC_ACTIVATE, LC_INACTIVE);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(5);
        CHECK(!camera.lifecycle.terminal && camera.port_error == SYS_ERR_PROTOCOL &&
              hw.sensor_count == 1);
        hw.wrong_result = 0;
        rounds(2);
        CHECK(camera.lifecycle.state == LC_ACTIVE);
        break;
    case 47:
        CHECK(transact(1, LC_CONFIGURE, LC_UNCONFIGURED, &done));
        hw.result_consume_busy = 1;
        r = request(2, LC_ACTIVATE, LC_INACTIVE);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(3);
        actor = 2;
        CHECK(public_endpoint.read(public_endpoint.port, &r.id, &done) == 0 &&
              public_endpoint.consume(public_endpoint.port, &done) == 0);
        rounds(2);
        CHECK(endpoint.active && camera.lifecycle.pending && hw.sensor_count == 1);
        hw.result_consume_busy = 0;
        rounds(2);
        CHECK(!endpoint.active && !camera.lifecycle.pending);
        break;
    case 48:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        r.id.session = 8;
        CHECK(public_endpoint.submit(public_endpoint.port, &r) == SYS_ERR_PROTOCOL &&
              !command_valid);
        break;
    case 49:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        public_endpoint.submit(public_endpoint.port, &r);
        r.timeout++;
        CHECK(public_endpoint.submit(public_endpoint.port, &r) == SYS_ERR_PROTOCOL);
        break;
    case 50:
        r = request(1, LC_ACTIVATE, LC_UNCONFIGURED);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(1);
        actor = 2;
        CHECK(public_endpoint.read(public_endpoint.port, &r.id, &done) == SYS_ERR_INVALID_STATE &&
              !hw.sensor_count);
        break;
    case 51:
        CHECK(transact(1, LC_CONFIGURE, LC_UNCONFIGURED, &done));
        hw.resource_block = 1;
        r = request(2, LC_ACTIVATE, LC_INACTIVE);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(4);
        CHECK(!hw.sensor_count && !camera.lifecycle.terminal);
        hw.resource_block = 0;
        rounds(2);
        CHECK(hw.sensor_count == 1);
        break;
    case 52:
        CHECK(transact(1, LC_CONFIGURE, LC_UNCONFIGURED, &done));
        hw.gate_error = SYS_ERR_IO;
        r = request(2, LC_ACTIVATE, LC_INACTIVE);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(3);
        CHECK(!hw.sensor_count);
        break;
    case 53:
        CHECK(active());
        count = g_trace_count;
        r = request(3, LC_DEACTIVATE, LC_ACTIVE);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(1);
        {
            uint32_t process = 0, returns = 0, finish = 0;
            for (i = count; i < g_trace_count; i++)
            {
                if (g_trace[i][0] == 20)
                    process = i;
                if (g_trace[i][0] == 21)
                    returns = i;
                if (g_trace[i][0] == 22)
                    finish = i;
            }
            CHECK(process < returns && returns < finish);
        }
        break;
    case 54:
        CHECK(active());
        hw.fail_stop = 1;
        CHECK(transact(3, LC_DEACTIVATE, LC_ACTIVE, &done));
        hw.fail_stop = 0;
        rounds(3);
        actor = 1;
        CHECK(VCamera_QueryStatus(&camera, &st) == 0 && st.current.stop_result == 0 &&
              st.first_stop_failure.stop_result == SYS_ERR_IO);
        break;
    case 55:
        actor = 1;
        memset(&st, 0xAB, sizeof(st));
        CHECK(VCamera_QueryStatus(&camera, &st) == 0 && st.current.flags == VC_COLD_MASK &&
              st.lifecycle == LC_UNCONFIGURED);
        CHECK(!hw.sensor_count);
        break;
    case 56:
        for (i = 0; i < 20; i++)
            rounds(1);
        CHECK(!hw.sensor_count && !hw.capture_count && !hw.stop_count);
        break;
    case 57:
        actor = 1;
        {
            CoreMediaFormat f = raw_format;
            f.dma_alignment = 1;
            CHECK(VCamera_QueryCapabilities(&camera, &f, &cap) == SYS_ERR_NOT_SUPPORTED);
        }
        break;
    case 58:
        actor = 1;
        {
            CameraConfig_t before;
            memset(&n, 0xAB, sizeof(n));
            before = n;
            {
                CoreMediaFormat f = raw_format;
                f.flags = 1;
                CHECK(VCamera_Translate(&f, &n) == SYS_ERR_INVALID_ARG &&
                      !memcmp(&n, &before, sizeof(n)));
            }
        }
        break;
    case 59:
        CHECK(active());
        actor = 1;
        r = request(2, LC_ACTIVATE, LC_INACTIVE);
        CHECK(VCamera_ReadCompletion(&camera, &r.id, &done) == 0);
        r.id.request--;
        CHECK(VCamera_ReadCompletion(&camera, &r.id, &done) == SYS_ERR_NOT_FOUND);
        break;
    case 60:
        CHECK(transact(1, LC_CONFIGURE, LC_UNCONFIGURED, &done));
        hw.native_full = 1;
        r = request(2, LC_ACTIVATE, LC_INACTIVE);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(4);
        CHECK(hw.request_count == 0 && hw.sensor_count == 0);
        hw.native_full = 0;
        rounds(2);
        CHECK(hw.request_count == 1 && hw.sensor_count == 1);
        break;
    case 61:
        CHECK(transact(1, LC_CONFIGURE, LC_UNCONFIGURED, &done));
        hw.native_full = 1;
        r = request(2, LC_ACTIVATE, LC_INACTIVE);
        r.timeout = 2;
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(5);
        CHECK(hw.sensor_count == 0);
        hw.native_full = 0;
        rounds(3);
        CHECK(hw.sensor_count == 0 && camera.lifecycle.state == LC_ERROR);
        actor = 2;
        CHECK(public_endpoint.read(public_endpoint.port, &r.id, &done) == 0 &&
              done.result == SYS_ERR_TIMEOUT);
        break;
    case 62:
        CHECK(active());
        hw.protect = 1;
        rounds(1);
        actor = 1;
        CHECK(VCamera_QueryStatus(&camera, &st) == 0 && st.current.media_result == SYS_ERR_IO &&
              st.first_fault == SYS_ERR_IO);
        CHECK((st.current.flags & VC_MEDIA_RUNNING) == 0);
        break;
    case 63:
        actor = 1;
        candidate.media.format = raw_format;
        CHECK(VCamera_BindCandidate(&camera, &registry, &candidate) == 0);
        CHECK(!hw.sensor_count);
        break;
    case 64:
        CHECK(active());
        actor = 1;
        candidate.media.format = jpeg_format;
        CHECK(VCamera_BindCandidate(&camera, &registry, &candidate) == SYS_ERR_NOT_SUPPORTED);
        CHECK(hw.sensor_count == 1);
        break;
    case 65:
        CHECK(active());
        CHECK(transact(3, LC_DEACTIVATE, LC_ACTIVE, &done));
        hw.resource_block = 1;
        r = request(4, LC_CLEANUP, LC_INACTIVE);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(4);
        CHECK(camera.prepared && camera.lifecycle.pending);
        hw.resource_block = 0;
        rounds(2);
        CHECK(!camera.prepared && camera.lifecycle.state == LC_UNCONFIGURED);
        break;
    case 66:
        CHECK(transact(1, LC_CONFIGURE, LC_UNCONFIGURED, &done));
        hw.fail_capture = 1;
        CHECK(transact(2, LC_ACTIVATE, LC_INACTIVE, &done));
        count = hw.request_count;
        hw.resource_block = 1;
        r = request(3, LC_ERROR_PROCESS, LC_ERROR);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(4);
        CHECK(hw.request_count == count);
        break;
    case 67:
        r = request(0xFFFFFFFFU, LC_CONFIGURE, LC_UNCONFIGURED);
        CHECK(public_endpoint.submit(public_endpoint.port, &r) == 0);
        r.id.request = 1;
        CHECK(public_endpoint.submit(public_endpoint.port, &r) == SYS_ERR_PROTOCOL);
        break;
    case 68:
        CHECK(active());
        actor = 1;
        memset(&exit, 0, sizeof(exit));
        CHECK(VCamera_QueryExitEvidence(&camera, &ur, &exit) == SYS_ERR_PROTOCOL &&
              exit.identity_result == SYS_ERR_PROTOCOL);
        break;
    case 69:
        actor = 1;
        CHECK(VCamera_Init(&camera, 81, 7, &control_ops, &hw, &resources) == SYS_ERR_INVALID_ARG);
        CHECK(VCameraEndpoint_Init(&endpoint, &camera, &(VCameraTransport){&transport_ops, NULL}) ==
              SYS_ERR_INVALID_ARG);
        break;
    case 70:
        actor = 1;
        CHECK(composed());
        CHECK(VCamera_BindCandidate(&camera, &registry, &candidate) == 0 && !hw.sensor_count);
        break;
    case 71:
        actor = 1;
        CHECK(composed());
        CHECK(VCamera_BindCandidate(&camera, &registry, &candidate) == 0);
        actor = 2;
        memset(&supervisor, 0, sizeof(supervisor));
        {
            uint32_t states[16] = {LC_UNCONFIGURED};
            CHECK(EM_BuildPlan(&registry, &candidate, states, 0, EM_START, 0, &execution_plan) ==
                  0);
            CHECK(EM_Init(&supervisor, 7, &public_endpoint, 1, supervisor_owner, NULL) == 0);
            CHECK(EM_Load(&supervisor, &execution_plan, 100) == 0);
            for (i = 0; i < 30; i++)
            {
                actor = 2;
                EM_Process(&supervisor, now);
                rounds(1);
            }
            CHECK(supervisor.state == EM_DONE && !supervisor.first_error && hw.sensor_count == 1 &&
                  hw.capture_count == 1);
            states[0] = LC_ACTIVE;
            actor = 2;
            CHECK(EM_BuildPlan(&registry, &candidate, states, 1, EM_STOP, 1, &execution_plan) ==
                      0 &&
                  EM_Load(&supervisor, &execution_plan, 100) == 0);
            for (i = 0; i < 30; i++)
            {
                actor = 2;
                EM_Process(&supervisor, now);
                rounds(1);
            }
            CHECK(supervisor.state == EM_DONE && !supervisor.first_error &&
                  camera.lifecycle.state == LC_UNCONFIGURED && hw.stop_count == 1);
        }
        break;
    case 72:
        CHECK(active());
        raw.owned[0] = 1;
        r = request(3, LC_DEACTIVATE, LC_ACTIVE);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(1);
        hw.result.result = 0;
        hw.result.observation.flags = VC_STOP_MASK;
        hw.complete = 1;
        rounds(3);
        CHECK(camera.lifecycle.state == 19 && camera.lifecycle.pending && raw.owned[0] &&
              !hw.stop_count);
        break;
    case 73:
        CHECK(transact(1, LC_CONFIGURE, LC_UNCONFIGURED, &done));
        hw.skip_auto = 1;
        r = request(2, LC_ACTIVATE, LC_INACTIVE);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(1);
        hw.result.result = 0;
        hw.result.observation.flags = VC_DEVICE_RUNNING | VC_MEDIA_RUNNING;
        hw.complete = 1;
        hw.obs.flags = VC_STOP_MASK;
        rounds(3);
        CHECK(camera.lifecycle.state == 18 && camera.lifecycle.pending);
        break;
    case 74:
        CHECK(transact(1, LC_CONFIGURE, LC_UNCONFIGURED, &done));
        hw.delay = 10;
        r = request(2, LC_ACTIVATE, LC_INACTIVE);
        public_endpoint.submit(public_endpoint.port, &r);
        rounds(4);
        CHECK(hw.sensor_count == 1 && hw.capture_count == 1 && hw.request_count == 1);
        break;
    case 75:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        public_endpoint.submit(public_endpoint.port, &r);
        memset(&reply, 0, sizeof(reply));
        reply.completion.id = r.id;
        reply.completion.id.session--;
        reply.completion.fact = LC_INACTIVE;
        reply_valid = 1;
        CHECK(public_endpoint.read(public_endpoint.port, &r.id, &done) == SYS_ERR_NOT_FOUND &&
              !client.has_reply);
        break;
    case 76:
        CHECK(active());
        actor = 1;
        CHECK(loan(&jpeg, &jr, &usb, &h1, &m1));
        hw.releasing[0] = 1;
        CHECK(MR_Close(&jr, &ur) == 0 && MR_Return(&jr, &h1, &m1) == 0);
        hw.consumer_active[0] = 0;
        actor = 2;
        r = request(3, LC_DEACTIVATE, LC_ACTIVE);
        CHECK(public_endpoint.submit(public_endpoint.port, &r) == 0);
        rounds(4);
        CHECK(!hw.stop_count && camera.lifecycle.pending);
        actor = 1;
        {
            MRReceipt t;
            CHECK(MR_ReadReceipt(&jr, &h1, &t) == 0 &&
                  MR_FlushReceipt(&jr, &t, terminal_send, NULL) == 0);
            rounds(2);
            CHECK(!hw.stop_count);
            actor = 1;
            CHECK(MR_ConfirmConsumed(&jr, &t) == 0 && MR_Retire(&jr, &t) == 0);
        }
        rounds(2);
        CHECK(hw.stop_count == 1 && camera.lifecycle.state == LC_INACTIVE);
        break;
    default:
        return 2;
    }
    z = 0;
    (void)z;
    return 0;
}
