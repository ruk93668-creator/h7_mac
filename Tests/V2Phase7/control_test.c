#include "../../CM7/ApplicationV2/Core/Runtime/control_execution_link.h"
#include "../../CM7/ApplicationV2/Core/Runtime/control_transport.h"
#include "ipc_shared.h"
#include <string.h>

uint32_t g_failure;
uint32_t g_sizes[12];
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            g_failure = __LINE__;                                                                  \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define GUARD(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            g_failure = __LINE__;                                                                  \
            return SYS_ERR_PROTOCOL;                                                               \
        }                                                                                          \
    } while (0)

typedef struct
{
    uint32_t id, pending;
    LCRequest request;
    LCCompletion result;
} TestModule;

static CSService service;
static CSExecutionEndpoint execution;
static CSExecClient cs_client;
static CSExecServer cs_server;
static uint32_t use_queue;
static CSAdapter adapter;
static EMContext manager;
static CSIngress inbound;
static CSEgress outbound;
static CSSnapshot current;
static TestModule capture, storage;
static CSReply reply, temporary;
static CSRequest request;
static uint32_t actor, now, submits, reads, consumes, module_ops, acknowledge, policy_calls;
static uint32_t deny_connection, deny_plan, deny_rollback, deny_command, busy_submit, busy_consume;
static uint32_t wrong_result, wait_module, fail_start, fail_rollback, ingress_ready, full_out, sent;
static uint32_t revision_on_submit, rw_denied, recurse, recurse_result;
static uint32_t operations[32][2];
static const CSPeer ipc = {CS_IPC, 11, 1};
static const CSPeer usb = {CS_USB, 11, 1};
static const CSPeer ble = {CS_BLE, 22, 1};
static const CSPeer wifi = {CS_WIFI, 33, 1};
static const CSPeer ethernet = {CS_ETHERNET, 44, 1};

static const CoreMediaFormat jpeg_format = {1, 4, 2, 320, 240, 0, 50000, 50000, 1, 2, 2, 4, 1, 0};
static const V2Provided video = {{1}, 1, 0};
static const V2Requirement camera_required = {{1}, 1, 0, 1, 0, {3}, 0};
static const V2ModuleDesc modules[] = {
    {.id = {3}, .major = 1, .provided = &video, .provided_count = 1},
    {.id = {4}, .major = 1, .required = &camera_required, .required_count = 1}};
static const V2Registry registry = {modules, 2, 0};
static CSProfile profiles[2];
static CSCatalog catalog;

static int control_owner(void *p)
{
    (void)p;
    return actor == 1;
}

static int supervisor_owner(void *p)
{
    (void)p;
    return actor == 2;
}

static SysResult_t authorize(void *p, const CSPeer *peer, uint32_t op, uint32_t profile,
                             uint32_t module, uint32_t transition)
{
    (void)p;
    GUARD(control_owner(NULL) && peer->principal);
    policy_calls++;
    if (recurse)
    {
        recurse = 0;
        recurse_result = CS_Process(&service, now);
    }
    if (!op)
        return deny_connection ? SYS_ERR_NOT_SUPPORTED : SYS_OK;
    if (deny_command || (rw_denied && peer->transport == CS_BLE && op != CS_VALIDATE))
        return SYS_ERR_NOT_SUPPORTED;
    if (module == deny_plan ||
        (deny_rollback && module && (transition == LC_DEACTIVATE || transition == LC_CLEANUP)))
        return module ? SYS_ERR_NOT_SUPPORTED : SYS_OK;
    (void)profile;
    return SYS_OK;
}

static SysResult_t snapshot(void *p, CSSnapshot *o)
{
    (void)p;
    *o = current;
    return SYS_OK;
}

#define LINK_QUEUE(T, N, SENDER, RECEIVER)                                                         \
    static T N##_wire;                                                                             \
    static unsigned N##_full, N##_pending;                                                         \
    static SysResult_t send_##N(void *p, const T *v)                                               \
    {                                                                                              \
        (void)p;                                                                                   \
        GUARD(SENDER(NULL));                                                                       \
        if (N##_full || N##_pending)                                                               \
            return SYS_ERR_NO_RESOURCE;                                                            \
        N##_wire = *v;                                                                             \
        N##_pending = 1;                                                                           \
        return SYS_OK;                                                                             \
    }                                                                                              \
    static SysResult_t receive_##N(void *p, T *v)                                                  \
    {                                                                                              \
        (void)p;                                                                                   \
        GUARD(RECEIVER(NULL));                                                                     \
        if (!N##_pending)                                                                          \
            return SYS_ERR_NOT_FOUND;                                                              \
        *v = N##_wire;                                                                             \
        N##_pending = 0;                                                                           \
        return SYS_OK;                                                                             \
    }

LINK_QUEUE(CSExecution, work, control_owner, supervisor_owner)
LINK_QUEUE(CSExecResult, result, supervisor_owner, control_owner)
LINK_QUEUE(CSExecResult, ack, control_owner, supervisor_owner)
static const CSExecLinkOps link_ops = {control_owner, supervisor_owner, send_work, receive_work,
                                       send_result,   receive_result,   send_ack,  receive_ack};

static SysResult_t submit_work(void *p, const CSExecution *work)
{
    SysResult_t z;
    (void)p;
    GUARD(control_owner(NULL));
    if (busy_submit)
        return SYS_ERR_NO_RESOURCE;
    if (use_queue)
    {
        if (revision_on_submit)
            current.revision++;
        z = CSExecClient_Submit(&cs_client, work);
        if (!z)
            submits++;
        return z;
    }
    if (revision_on_submit)
        current.revision++;
    actor = 2; /* 夹具切换到接收端上下文。生产跨任务走复制队列。 */
    z = CSExec_Submit(&execution, work);
    actor = 1;
    if (!z)
        submits++;
    return z;
}

static SysResult_t read_work(void *p, const CSIdentity *id, CSExecResult *r)
{
    SysResult_t z;
    (void)p;
    GUARD(control_owner(NULL));
    if (use_queue)
        return CSExecClient_Read(&cs_client, id, r);
    actor = 2;
    z = CSExec_Read(&execution, id, r);
    actor = 1;
    if (!z)
    {
        reads++;
        if (wrong_result)
        {
            wrong_result--;
            r->id.ticket++;
        }
    }
    return z;
}

static SysResult_t consume_work(void *p, const CSExecResult *r)
{
    SysResult_t z;
    (void)p;
    GUARD(control_owner(NULL));
    consumes++;
    if (busy_consume)
        return SYS_ERR_NO_RESOURCE;
    if (use_queue)
        return CSExecClient_Consume(&cs_client, r);
    actor = 2;
    z = CSExec_Consume(&execution, r);
    actor = 1;
    return z;
}

static const CSPortOps ports = {control_owner, authorize, snapshot,
                                submit_work,   read_work, consume_work};

static SysResult_t module_submit(void *p, const LCRequest *r)
{
    TestModule *m = p;
    SysResult_t z = SYS_OK;
    uint32_t fact = LC_Target(r->id.operation);
    GUARD(supervisor_owner(NULL) && m->id == r->id.module && !m->pending);
    GUARD(module_ops < 32 && current.states[m->id - 3U] == r->expected);
    operations[module_ops][0] = m->id;
    operations[module_ops++][1] = r->id.operation;
    if (fail_start && m->id == 4 && r->id.operation == LC_ACTIVATE)
    {
        z = SYS_ERR_IO;
        fact = r->expected;
    }
    if (fail_rollback && m->id == 4 && r->id.operation == LC_CLEANUP)
    {
        z = SYS_ERR_PROTOCOL;
        fact = LC_ERROR;
    }
    m->request = *r;
    m->result = (LCCompletion){r->id, z, z, fact};
    m->pending = 1;
    return SYS_OK;
}

static SysResult_t module_read(void *p, const V2ExecId *id, LCCompletion *r)
{
    TestModule *m = p;
    GUARD(supervisor_owner(NULL) && m->pending && LC_IdEqual(id, &m->request.id));
    if (wait_module)
        return SYS_ERR_BUSY;
    *r = m->result;
    return SYS_OK;
}

static SysResult_t module_consume(void *p, const LCCompletion *r)
{
    TestModule *m = p;
    GUARD(supervisor_owner(NULL) && m->pending && !memcmp(r, &m->result, sizeof(*r)));
    m->pending = 0;
    current.states[m->id - 3U] = r->fact;
    acknowledge++;
    return SYS_OK;
}

static SysResult_t transport_receive(void *p, CSIngress *in)
{
    (void)p;
    if (!ingress_ready)
        return SYS_ERR_NOT_FOUND;
    *in = inbound;
    ingress_ready = 0;
    return SYS_OK;
}

static SysResult_t transport_send(void *p, const CSEgress *out)
{
    (void)p;
    if (full_out)
        return SYS_ERR_NO_RESOURCE;
    outbound = *out;
    sent++;
    return SYS_OK;
}
static const CSAdapterOps adapter_ops = {control_owner, transport_receive, transport_send};

static int pump(uint32_t rounds)
{
    while (rounds--)
    {
        SysResult_t z;
        now++;
        actor = 1;
        z = CS_Process(&service, now);
        if (z && z != SYS_ERR_BUSY && z != SYS_ERR_NO_RESOURCE && z != SYS_ERR_PROTOCOL &&
            z != SYS_ERR_NOT_FOUND)
            return 0;
        actor = 2;
        z = use_queue ? CSExecServer_Process(&cs_server, now) : CSExec_Process(&execution, now);
        if (z && z != SYS_ERR_BUSY && z != SYS_ERR_NO_RESOURCE && z != SYS_ERR_NOT_FOUND)
            return 0;
    }
    actor = 1;
    return !g_failure;
}

static CSRequest make_request(uint32_t session, uint32_t seq, uint32_t operation)
{
    return (CSRequest){CS_VERSION, 81, session, seq, operation, 1, 12, 0, 0, 100};
}

static SysResult_t graph_format(const GraphEntityDesc *e, const GraphFormat *f,
                                const GraphCheckContext *c, GraphIssue *i)
{
    (void)e;
    (void)c;
    (void)i;
    return CoreMedia_Validate(f);
}
static SysResult_t graph_sink(const GraphEntityDesc *e, const GraphFormat *f,
                              const GraphFrameRate *r, const GraphCheckContext *c, GraphIssue *i)
{
    (void)e;
    (void)f;
    (void)c;
    (void)i;
    return CoreRate_Validate(r);
}
static SysResult_t preflight(const GraphValidatedView *v, const V2Resolution *r, V2Issue *i)
{
    (void)r;
    (void)i;
    return v->format.backend == CORE_BACKEND_JPEG ? SYS_OK : SYS_ERR_NOT_SUPPORTED;
}
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
static void setup_graph(void)
{
    static V2EntityBinding binding[4] = {
        {{1}, 0, {3}}, {{2}, 0, {3}}, {{3}, 0, {3}}, {{4}, 0, {4}}};
    static V2ModuleId root = {4};
    V2Desired desired;
    uint32_t i, j, first = 0;
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
    pipeline.backend = CORE_BACKEND_JPEG;
    pipeline.sinks[0].value = 4;
    pipeline.sink_count = 1;
    pipeline.link_count = 3;
    for (i = 0; i < 3; i++)
        pipeline.links[i].value = i + 1;
    pipeline.default_format = &jpeg_format;
    pipeline.default_sink_rates = &graph_rate;
    graph_caps.media_mask = 1;
    graph_caps.pixel_mask = 8;
    graph_caps.backend_mask = 2;
    graph_caps.width_min = graph_caps.width_max = 320;
    graph_caps.height_min = graph_caps.height_max = 240;
    graph_caps.width_step = graph_caps.height_step = 1;
    graph_caps.max_payload = 50000;
    graph_caps.max_spans = 2;
    graph_caps.max_fps_num = 10;
    graph_caps.max_fps_den = 1;
    graph_caps.dma_alignment = 4;
    graph_caps.frame_alignment = 1;
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
    selection.format = jpeg_format;
    selection.sink_policy_count = 1;
    selection.sink_policies[0].sink.value = 4;
    selection.sink_policies[0].rate = graph_rate;
    desired.roots = &root;
    desired.root_count = 1;
    desired.selections = &selection;
    desired.selection_count = 1;
    profiles[0] = (CSProfile){1, desired};
    profiles[1] = (CSProfile){2, desired};
    catalog = (CSCatalog){&registry, &graph, binding, profiles, preflight, 4, 2};
}

int Control_Run(uint32_t id)
{
    static EMEndpoint endpoints[2];
    uint32_t session, second, i;
    CSIdentity old;
    CSPeer stranger;
    use_queue = id == 23 || id == 24;
    g_sizes[0] = sizeof(CSService);
    g_sizes[1] = sizeof(CSExecutionEndpoint);
    g_sizes[2] = sizeof(CSAdapter);
    g_sizes[3] = sizeof(CSRequest);
    g_sizes[4] = sizeof(CSReply);
    g_sizes[5] = sizeof(CSIngress);
    g_sizes[6] = sizeof(CSEgress);
    g_sizes[7] = offsetof(CSService, scratch);
    g_sizes[8] = sizeof(IpcShared_t);
    g_sizes[9] = sizeof(CSExecution);
    g_sizes[10] = sizeof(CSExecClient);
    g_sizes[11] = sizeof(CSExecServer);
    setup_graph();
    current.revision = 12;
    current.prepared_profile = 1;
    current.states[0] = current.states[1] = LC_UNCONFIGURED;
    capture.id = 3;
    storage.id = 4;
    endpoints[0] = (EMEndpoint){3, &capture, module_submit, module_read, module_consume};
    endpoints[1] = (EMEndpoint){4, &storage, module_submit, module_read, module_consume};
    actor = 2;
    CHECK(EM_Init(&manager, 7, endpoints, 2, supervisor_owner, NULL) == SYS_OK);
    CHECK(CSExec_Init(&execution, 81, &manager, supervisor_owner, snapshot, NULL) == SYS_OK);
    CHECK(CS_Init(&service, 81, &catalog, &ports, NULL) == SYS_ERR_INVALID_STATE);
    CHECK(CSExecServer_Init(&cs_server, &execution, &link_ops, NULL) == SYS_OK);
    actor = 1;
    CHECK(CS_Init(&service, 81, &catalog, &ports, NULL) == SYS_OK);
    CHECK(CSExecClient_Init(&cs_client, &link_ops, NULL) == SYS_OK);
    if (id == 1)
    {
        deny_connection = 1;
        CHECK(CS_Open(&service, &ipc, &session) == SYS_ERR_NOT_SUPPORTED);
        deny_connection = 0;
    }
    CHECK(CS_Open(&service, &ipc, &session) == SYS_OK && session == 1);
    CHECK(CS_Open(&service, &ipc, &second) == SYS_OK && second == session);
    request = make_request(session, 1, CS_START);
    if (id == 2)
    {
        CHECK(CS_Submit(&service, &usb, &request, now, &reply) == SYS_ERR_PROTOCOL);
        request.boot--;
        CHECK(CS_Submit(&service, &ipc, &request, now, &reply) == SYS_ERR_PROTOCOL);
        request.boot++;
        request.version++;
        CHECK(CS_Submit(&service, &ipc, &request, now, &reply) == SYS_ERR_INVALID_ARG);
        return 0;
    }
    if (id == 3)
    {
        const CSPeer *const peers[] = {&ipc, &usb, &ble, &wifi, &ethernet};
        rw_denied = 1;
        for (i = 0; i < 5; ++i)
        {
            CHECK(CS_Open(&service, peers[i], &second) == SYS_OK);
            request = make_request(second, 1, CS_START);
            if (peers[i]->transport == CS_BLE)
                CHECK(CS_Submit(&service, peers[i], &request, now, &reply) ==
                      SYS_ERR_NOT_SUPPORTED);
            request.operation = CS_VALIDATE;
            CHECK(CS_Submit(&service, peers[i], &request, now, &reply) == SYS_OK);
            CHECK(pump(1));
            CHECK(CS_Query(&service, peers[i], &reply.id, &temporary) == SYS_OK);
            CHECK(temporary.phase == CS_FINAL && temporary.effect == CS_EFFECT_CANDIDATE);
            CHECK(CS_Confirm(&service, peers[i], &temporary) == SYS_OK);
        }
        CHECK(!submits && !module_ops);
        return 0;
    }
    if (id == 4)
    {
        CHECK(CSAdapter_Init(&adapter, &service, CS_USB, &adapter_ops, NULL) == SYS_OK);
        inbound.route = (CSRoute){ipc, 2};
        inbound.type = CSA_OPEN;
        ingress_ready = 1;
        CHECK(CSAdapter_Process(&adapter, now) == SYS_ERR_PROTOCOL && !sent);
        inbound.route.peer = usb;
        ingress_ready = full_out = 1;
        CHECK(CSAdapter_Process(&adapter, now) == SYS_ERR_NO_RESOURCE && adapter.pending);
        full_out = 0;
        CHECK(CSAdapter_Process(&adapter, now) == SYS_OK && outbound.session == 2);
        inbound.route.exchange = 4;
        inbound.type = CSA_SUBMIT;
        inbound.body.request = make_request(outbound.session, 1, CS_VALIDATE);
        full_out = ingress_ready = 1;
        CHECK(CSAdapter_Process(&adapter, now) == SYS_ERR_NO_RESOURCE);
        CHECK(pump(1) && service.next_ticket == 1);
        full_out = 0;
        CHECK(CSAdapter_Process(&adapter, now) == SYS_OK && outbound.route.exchange == 4);
        CHECK(outbound.reply.phase == CS_ACCEPTED && service.next_ticket == 1);
        inbound.type = CSA_QUERY;
        inbound.body.id = outbound.reply.id;
        ingress_ready = 1;
        CHECK(CSAdapter_Process(&adapter, now) == SYS_OK && outbound.reply.phase == CS_FINAL);
        CHECK(CS_Close(&service, &usb, 81, outbound.reply.id.session) == SYS_ERR_BUSY);
        inbound.type = CSA_CONFIRM;
        inbound.body.reply = outbound.reply;
        ingress_ready = 1;
        CHECK(CSAdapter_Process(&adapter, now) == SYS_OK && outbound.result == SYS_OK);
        CHECK(CS_Close(&service, &usb, 81, 2) == SYS_OK);
        return 0;
    }
    if (id == 5)
    {
        request.operation = CS_VALIDATE;
        for (i = 0; i < CS_RECORD_MAX; ++i)
        {
            request.sequence = i + 1;
            CHECK(CS_Submit(&service, &ipc, &request, now, &reply) == SYS_OK && pump(1));
            if (!i)
                old = reply.id;
        }
        request.sequence++;
        CHECK(CS_Submit(&service, &ipc, &request, now, &reply) == SYS_ERR_NO_RESOURCE);
        CHECK(CS_Query(&service, &ipc, &old, &temporary) == SYS_OK);
        CHECK(CS_Confirm(&service, &ipc, &temporary) == SYS_OK);
        CHECK(CS_Submit(&service, &ipc, &request, now, &reply) == SYS_OK && pump(1));
        CHECK(CS_Query(&service, &ipc, &old, &temporary) == SYS_ERR_NOT_FOUND);
        request.sequence = old.sequence;
        CHECK(CS_Submit(&service, &ipc, &request, now, &reply) == SYS_ERR_NOT_FOUND);
        return 0;
    }
    if (id == 6)
    {
        stranger = ipc;
        for (i = 1; i < CS_SESSION_MAX; ++i)
        {
            stranger.client++;
            CHECK(CS_Open(&service, &stranger, &second) == SYS_OK);
        }
        CHECK(CS_Open(&service, &wifi, &second) == SYS_ERR_NO_RESOURCE);
        CHECK(CS_Close(&service, &ipc, 81, session) == SYS_OK);
        CHECK(CS_Open(&service, &wifi, &second) == SYS_OK && second > CS_SESSION_MAX);
        return 0;
    }
    if (id == 23 || id == 24)
    {
        work_full = id == 23;
        revision_on_submit = id == 24;
        CHECK(CS_Submit(&service, &ipc, &request, now, &reply) == SYS_OK && pump(3));
        if (id == 23)
        {
            CHECK(!submits && !module_ops && !work_pending);
            work_full = 0;
            result_full = ack_full = 1;
            CHECK(pump(30) && submits == 1 && module_ops == 4 && execution.outstanding);
            CHECK(CS_Query(&service, &ipc, &reply.id, &temporary) == SYS_OK);
            CHECK(temporary.phase == CS_EXECUTING);
            result_full = 0;
            CHECK(pump(4) && service.exec_seen && execution.outstanding);
            ack_full = 0;
            CHECK(pump(2) && !execution.outstanding && module_ops == 4 && acknowledge == 4);
            CHECK(CS_Query(&service, &ipc, &reply.id, &temporary) == SYS_OK);
            CHECK(temporary.phase == CS_FINAL && temporary.effect == CS_EFFECT_EXECUTION);
            work_pending = 1; /* 旧work仍到达，不能再执行或接替当前身份。 */
            CHECK(pump(3) && module_ops == 4 && !result_pending);
        }
        else
        {
            CHECK(submits == 1 && !module_ops && cs_server.phase);
            CHECK(pump(30));
            CHECK(CS_Query(&service, &ipc, &reply.id, &temporary) == SYS_OK);
            CHECK(temporary.phase == CS_FINAL && temporary.execution_state == CS_EXEC_REFUSED);
            CHECK(temporary.actual_result == SYS_ERR_INVALID_STATE && !module_ops &&
                  temporary.effect == CS_EFFECT_NONE);
        }
        return 0;
    }
    if (id == 25)
    {
        service.next_ticket = UINT32_MAX; /* 白盒探测耗尽护栏，无量产状态注入接口。 */
        CHECK(CS_Submit(&service, &ipc, &request, now, &reply) == SYS_ERR_NO_RESOURCE);
        service.next_session = UINT32_MAX;
        CHECK(CS_Open(&service, &ble, &second) == SYS_ERR_NO_RESOURCE);
        CHECK(!service.active && !submits);
        return 0;
    }
    if (id == 7)
        request.revision++;
    if (id == 8)
        revision_on_submit = 1;
    if (id == 9)
        deny_plan = 3;
    if (id == 10)
        deny_rollback = 1;
    if (id == 11 || id == 14)
    {
        busy_submit = 1;
        request.timeout = id == 11 ? 100 : 10;
    }
    if (id == 12)
    {
        wait_module = 1;
        request.timeout = 12;
    }
    if (id == 13)
        wrong_result = 1;
    if (id == 15 || id == 16 || id == 17)
        fail_start = 1;
    if (id == 16)
        fail_rollback = 1;
    if (id == 17)
        busy_consume = 1;
    if (id == 18)
        now = UINT32_MAX - 3U;
    if (id == 19)
        request.profile = 2; /* 合法目录条目，但未绑定，不能运行时重配。 */
    if (id == 20)
        recurse = 1;
    if (id == 21)
    {
        current.states[0] = current.states[1] = LC_ACTIVE;
        request.operation = CS_STOP;
        request.stop_mask = 1U;
    }

    CHECK(CS_Submit(&service, &ipc, &request, now, &reply) == SYS_OK);
    CHECK(reply.phase == CS_ACCEPTED && !submits && !module_ops);
    CHECK(CS_Submit(&service, &ipc, &request, now, &temporary) == SYS_OK);
    CHECK(CS_IdEqual(&reply.id, &temporary.id) && service.next_ticket == 1);
    request.timeout++;
    CHECK(CS_Submit(&service, &ipc, &request, now, &temporary) == SYS_ERR_PROTOCOL);
    request.timeout--;
    CHECK(CS_Open(&service, &usb, &second) == SYS_OK);
    {
        CSRequest other = make_request(second, 1, CS_START);
        CHECK(CS_Submit(&service, &usb, &other, now, &temporary) == SYS_ERR_BUSY);
    }
    CHECK(CS_Query(&service, &usb, &reply.id, &temporary) == SYS_ERR_NOT_FOUND);
    if (id == 22)
        deny_command = 1;
    if (id == 11)
    {
        CHECK(pump(3) && !submits && !module_ops);
        busy_submit = 0;
    }
    if (id == 12)
    {
        CHECK(pump(20) && submits == 1);
        CHECK(CS_Query(&service, &ipc, &reply.id, &temporary) == SYS_OK);
        CHECK(temporary.phase == CS_EXECUTING && temporary.first_error == SYS_ERR_TIMEOUT);
        CHECK(CS_Cancel(&service, &ipc, &reply.id) == SYS_ERR_BUSY);
        wait_module = 0;
    }
    if (id == 17)
    {
        CHECK(pump(70) && submits == 1 && consumes);
        CHECK(CS_Query(&service, &ipc, &reply.id, &temporary) == SYS_OK);
        CHECK(temporary.phase == CS_EXECUTING && temporary.first_error == SYS_ERR_IO);
        busy_consume = 0;
    }
    if (id == 18)
    {
        CHECK(CS_Cancel(&service, &ipc, &reply.id) == SYS_OK &&
              CS_Cancel(&service, &ipc, &reply.id) == SYS_OK);
    }
    CHECK(pump(40));
    if (id == 22)
    {
        CHECK(!submits && !module_ops);
        deny_command = 0;
    }
    CHECK(CS_Query(&service, &ipc, &reply.id, &temporary) == SYS_OK && temporary.phase == CS_FINAL);
    if (id == 7 || id == 8 || id == 9 || id == 10 || id == 14 || id == 18 || id == 19 || id == 21 ||
        id == 22)
    {
        CHECK(!submits && !module_ops && temporary.first_error);
        if (id == 14)
            CHECK(temporary.first_error == SYS_ERR_TIMEOUT);
        if (id == 18)
            CHECK(temporary.effect == CS_EFFECT_CANCELLED);
        return 0;
    }
    CHECK(submits == 1 && temporary.effect == CS_EFFECT_EXECUTION && reads && consumes);
    if (id == 12)
    {
        CHECK(temporary.first_error == SYS_ERR_TIMEOUT && acknowledge);
        return 0;
    }
    if (id == 15 || id == 16 || id == 17)
    {
        CHECK(temporary.first_error == SYS_ERR_IO && temporary.actual_result == SYS_ERR_IO);
        if (id == 16)
        {
            CHECK(temporary.execution_state == EM_BLOCKED &&
                  temporary.recovery_error == SYS_ERR_PROTOCOL);
            request.sequence++;
            CHECK(CS_Submit(&service, &ipc, &request, now, &reply) == SYS_ERR_INVALID_STATE);
        }
        else
            CHECK(temporary.execution_state == EM_DONE && current.states[0] == LC_UNCONFIGURED &&
                  current.states[1] == LC_UNCONFIGURED);
        return 0;
    }
    CHECK(!temporary.first_error && current.states[0] == LC_ACTIVE &&
          current.states[1] == LC_ACTIVE);
    CHECK(operations[0][0] == 3 && operations[1][0] == 4 && operations[2][0] == 3 &&
          operations[3][0] == 4);
    if (id == 13)
        CHECK(reads == 2);
    if (id == 20)
        CHECK(recurse_result == SYS_ERR_BUSY);
    CHECK(CS_Close(&service, &ipc, 81, session) == SYS_ERR_BUSY);
    reply = temporary;
    reply.actual_result = SYS_ERR_IO;
    CHECK(CS_Confirm(&service, &ipc, &reply) == SYS_ERR_PROTOCOL);
    CHECK(CS_Confirm(&service, &usb, &temporary) == SYS_ERR_PROTOCOL);
    CHECK(CS_Confirm(&service, &ipc, &temporary) == SYS_OK);
    request.sequence++;
    request.operation = CS_STOP;
    request.stop_mask = 2U;
    CHECK(CS_Submit(&service, &ipc, &request, now, &reply) == SYS_OK && pump(16));
    CHECK(CS_Query(&service, &ipc, &reply.id, &temporary) == SYS_OK && !temporary.first_error);
    CHECK(current.states[0] == LC_ACTIVE && current.states[1] == LC_INACTIVE);
    CHECK(CS_Confirm(&service, &ipc, &temporary) == SYS_OK);
    CHECK(CS_Close(&service, &ipc, 81, session) == SYS_OK);
    CHECK(CS_Query(&service, &ipc, &reply.id, &temporary) == SYS_ERR_NOT_FOUND);
    return 0;
}
