#include "../../CM7/ApplicationV2/Modules/USB/usb_endpoint.h"
#include "../../CM7/ApplicationV2/Modules/Camera/camera_module.h"
#include <string.h>

uint32_t g_failure;
uint32_t g_trace[16];
uint32_t g_sizes[8];
uint32_t g_trace_count;
#define TEST(x)                                                                                    \
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
            return SYS_ERR_PROTOCOL;                                                               \
    } while (0)

static VUSB usb;
static VCamera camera;
static MRRelease authorized_release;
static unsigned jpeg, sd_owned;
static VUSBClient client;
static VUSBEndpoint ep;
static EMEndpoint iface;
static MRContext mr;
static MRGrant sd;
static LCRequest cmd, wire_request;
static VUSBReply wire_reply;
static LCCompletion wire_ack, done;
static AppLifecycleMessage_t life;
static MRRequest staged;
static VUSBDelivery delivery;
static MRReceipt ack;
static uint8_t data[38400];
static unsigned actor, now, started, connected, abort_once, cpu_read, hold_dma, stage_ready;
static unsigned unsafe_return, commits, return_sent, ack_wait, ack_full, release_fail, prepare_full;
static unsigned fallback, duplicate, bad_ack, cap_fail, stop_fail, release_attempts, hardware_ready;
static unsigned sr, sp, sa, full_request, full_reply, full_ack, notification_count;
static unsigned lifecycle_full, startup_fault, owned, released, class_ready, lifecycle_ready;
static unsigned stop_if_idle_hold, release_ack_hold, work_return, offer_ready, retired_fact, physical_calls,
    stopped_calls;
static const CoreFrameRate rate = {10, 1};
static const CoreMediaFormat format = {1, 3, 1, 160, 120, 320, 38400, 38400, 1, 1, 1, 32, 32, 0};
static const CoreMediaFormat jpeg_format = {1, 4, 2, 320, 240, 0, 50000, 50000, 1, 2, 2, 4, 1, 0};
static const V2ModuleDesc desc[] = {{{2}, 1, 0}, {{3}, 1, 0}};
static const V2Registry registry = {desc, 2, 0};

static int usb_owner(void *p)
{
    (void)p;
    return actor == 1;
}

static int sup_owner(void *p)
{
    (void)p;
    return actor == 2;
}

static bool cam_owner(void *p)
{
    (void)p;
    return actor == 3;
}

static SysResult_t get_caps(void *p, MRSourceCaps *out)
{
    (void)p;
    if (cap_fail)
        return SYS_ERR_IO;
    *out = jpeg ? (MRSourceCaps){MR_JPEG, MR_CAP_USB | MR_CAP_SD, 2, 2}
                : (MRSourceCaps){MR_RAW, MR_CAP_USB, 1, 1};
    return SYS_OK;
}

static SysResult_t role(void *p, const MRGrant *g, uint32_t *out)
{
    (void)p;
    GUARD(g->consumer == APP_CONSUMER_USB || (jpeg && MRGrant_Equal(g, &sd)));
    *out = g->consumer == APP_CONSUMER_USB ? MR_USB : MR_SD;
    return SYS_OK;
}

static SysResult_t grant_valid(void *p, const MRGrant *g)
{
    (void)p;
    return g->consumer == APP_CONSUMER_USB || (jpeg && MRGrant_Equal(g, &sd))
               ? SYS_OK : SYS_ERR_PROTOCOL;
}

static SysResult_t obtain(void *p, const MRGrant *g, const MRHandoff *h, MRMedia *m, MRView *v)
{
    (void)p;
    GUARD(g->session == h->session);
    if (g->consumer == APP_CONSUMER_STORAGE)
    {
        GUARD(jpeg && owned && !sd_owned);
        sd_owned = 1;
    }
    else
    {
        GUARD(!owned);
        physical_calls++;
        owned = 1;
        released = 0;
    }
    *m = (MRMedia){jpeg ? MR_JPEG : MR_RAW, g->session, h->sequence, g->generation};
    *v = jpeg ? (MRView){data, 12, data + 12, 20} : (MRView){data, sizeof(data), 0, 0};
    return SYS_OK;
}

static SysResult_t validate(void *p, const MRGrant *g, const MRMedia *m, const MRView *v)
{
    (void)p;
    GUARD((g->consumer == APP_CONSUMER_USB ? owned : sd_owned) &&
          m->session == g->session && v->data == data);
    return SYS_OK;
}

static SysResult_t quiet(void *p, const MRGrant *g, const MRMedia *m)
{
    (void)p;
    GUARD(g->session == m->session);
    return cpu_read || hold_dma ? SYS_ERR_BUSY : SYS_OK;
}

static SysResult_t release(void *p, const MRGrant *g, const MRMedia *m)
{
    GUARD(!quiet(p, g, m) && owned);
    release_attempts++;
    if (release_fail)
    {
        release_fail--;
        return SYS_ERR_IO;
    }
    owned = 0;
    released = 1;
    return SYS_OK;
}

static SysResult_t release_proof(void *p, const MRGrant *g, const MRMedia *m)
{
    (void)p;
    GUARD(released && !owned && m->session == g->session);
    return SYS_OK;
}

static SysResult_t exit_safe(void *p, const MRGrant *g)
{
    (void)p;
    return (g->consumer == APP_CONSUMER_USB ? owned : sd_owned) ? SYS_ERR_BUSY : SYS_OK;
}

static const MRSourceOps backend = {cam_owner, get_caps, role,    grant_valid,   obtain,
                                    validate,  quiet,    release, release_proof, exit_safe};

static int camera_port_owner(void *p)
{
    return cam_owner(p);
}

static SysResult_t camera_layout(void *p, const CoreMediaFormat *f, const CameraConfig_t *c,
                                 const MRSourceCaps *caps)
{
    (void)p;
    GUARD(c->width == f->width && f->backend == caps->backend);
    return SYS_OK;
}

static SysResult_t camera_gate(void *p, const LCRequest *r, uint32_t phase)
{
    (void)p; (void)r; (void)phase;
    return SYS_ERR_NOT_SUPPORTED;
}

static SysResult_t camera_request(void *p, const LCRequest *r, const CameraConfig_t *c)
{
    (void)p; (void)r; (void)c;
    return SYS_ERR_NOT_SUPPORTED;
}

static SysResult_t camera_read(void *p, const V2ExecId *id, VCameraResult *r)
{
    (void)p; (void)id; (void)r;
    return SYS_ERR_NOT_SUPPORTED;
}

static SysResult_t camera_consume(void *p, const VCameraResult *r)
{
    (void)p; (void)r;
    return SYS_ERR_NOT_SUPPORTED;
}

static SysResult_t camera_facts(void *p, VCameraObservation *o)
{
    (void)p; (void)o;
    return SYS_ERR_NOT_SUPPORTED;
}

static SysResult_t camera_release_id(void *p, const MRRelease *r)
{
    (void)p;
    return r->request == authorized_release.request &&
           MRGrant_Equal(&r->grant, &authorized_release.grant) ? SYS_OK : SYS_ERR_PROTOCOL;
}

static void camera_unused(void *p)
{
    (void)p;
}

static const VCameraOps camera_ops = {camera_port_owner, camera_layout, camera_gate, camera_request,
    camera_read, camera_consume, camera_facts, camera_release_id,
    camera_unused, camera_unused, camera_unused, camera_unused};

static SysResult_t lifecycle_send(void *p, const AppLifecycleMessage_t *m)
{
    (void)p;
    if (lifecycle_full || lifecycle_ready)
        return SYS_ERR_NO_RESOURCE;
    life = *m;
    lifecycle_ready = 1;
    return SYS_OK;
}

static SysResult_t lifecycle_recv(void *p, AppLifecycleMessage_t *out)
{
    MRRelease r;
    VCameraExitEvidence proof;
    SysResult_t z = SYS_OK;
    (void)p;
    if (!lifecycle_ready || (stop_if_idle_hold && life.operation == APP_LIFECYCLE_STOP_IF_IDLE))
        return SYS_ERR_NOT_FOUND;
    if (life.operation == APP_LIFECYCLE_RELEASE)
    {
        r.grant = staged.g.session ? staged.g : usb.consumer.grant;
        r.request = life.identity.request_id;
        actor = 3;
        authorized_release = r;
        z = MR_Close(&mr, &r);
        if (!z)
            z = VCamera_QueryExitEvidence(&camera, &r, &proof);
        actor = 1;
        GUARD(!z);
    }
    else if (life.operation == APP_LIFECYCLE_ACQUIRE)
    {
        MRGrant g = {life.identity.session_id, life.identity.consumer_id,
                     life.identity.consumer_generation, life.identity.request_id};
        actor = 3;
        z = MR_AssociateGrant(&mr, &g);
        actor = 1;
    }
    else if (life.operation == APP_LIFECYCLE_STOP_IF_IDLE && sd.session)
        z = SYS_ERR_BUSY;
    GUARD(!App_LifecycleMakeReply(&life, 9, z, out));
    if (life.operation == APP_LIFECYCLE_RELEASE && !fallback)
        out->completion_flags = APP_LIFECYCLE_QUALIFICATION_RELEASED;
    lifecycle_ready = 0;
    return SYS_OK;
}

static const VUSBConsumerOps client_ops = {usb_owner, lifecycle_send, lifecycle_recv};

static SysResult_t prepare(void *p, const MRGrant *g)
{
    SysResult_t z;
    MRBridge b = {1, 0, commits + 1};
    MRSlot slot = {0};
    (void)p;
    if (prepare_full)
        return SYS_ERR_NO_RESOURCE;
    actor = 3;
    z = MR_AssociateGrant(&mr, g);
    if (!z)
        z = MR_Stage(&mr, g, &b, &slot, &staged);
    actor = 1;
    stage_ready = !z;
    return z;
}

static SysResult_t prepared(void *p, MRRequest *r)
{
    (void)p;
    if (!stage_ready)
        return SYS_ERR_NOT_FOUND;
    stage_ready = 0;
    *r = staged;
    return SYS_OK;
}

static SysResult_t commit(void *p, const MRRequest *r)
{
    SysResult_t z;
    (void)p;
    actor = 3;
    z = MR_Commit(&mr, r);
    if (!z)
        z = MR_Admit(&mr, r);
    if (!z)
        z = MR_Obtain(&mr, r, &delivery.data.media, &delivery.data.view);
    if (!z)
    {
        delivery.data.bridge = r->b;
        delivery.data.bridge.lease = ++commits;
        z = MR_BindBridge(&mr, r, &delivery.data.bridge);
    }
    actor = 1;
    delivery.request = *r;
    delivery.kind = VUF_FRAME;
    offer_ready = !z;
    return z;
}

static SysResult_t cancel_frame_request(void *p, const MRRequest *r)
{
    SysResult_t z;
    (void)p;
    actor = 3;
    z = MR_CancelLocal(&mr, r);
    actor = 1;
    return z;
}

static SysResult_t cancelled(void *p, const MRRequest *r)
{
    (void)p;
    GUARD(r->h.sequence == staged.h.sequence && !owned);
    return SYS_OK;
}

static SysResult_t offer(void *p, VUSBDelivery *d)
{
    (void)p;
    if (!offer_ready)
        return SYS_ERR_NOT_FOUND;
    *d = delivery;
    if (duplicate)
    {
        duplicate--;
        d->request.h.sequence--;
    }
    else
        offer_ready = 0;
    return SYS_OK;
}

static SysResult_t submit_uvc(void *p, const MRRequest *r, const VUSBFrameData *v)
{
    (void)p;
    GUARD(owned && r->h.sequence == v->media.id);
    cpu_read = 1;
    return SYS_OK;
}

static SysResult_t readers_safe(void *p, const MRRequest *r, const VUSBFrameData *v)
{
    GUARD(r->h.sequence == v->media.id);
    return quiet(p, &r->g, &v->media);
}

static SysResult_t return_frame(void *p, const MRRequest *r, const VUSBFrameData *v)
{
    (void)p;
    if (readers_safe(p, r, v))
        unsafe_return++;
    GUARD(!work_return && !readers_safe(p, r, v));
    work_return = 1;
    return_sent++;
    return SYS_OK;
}

static SysResult_t flush_receipt(void *p, const MRReceipt *r)
{
    (void)p;
    GUARD(actor == 3 && r->physical_returned && !owned);
    if (ack_full)
        return SYS_ERR_NO_RESOURCE;
    ack = *r;
    ack_wait = 1;
    return SYS_OK;
}

static SysResult_t recv_receipt(void *p, MRReceipt *out)
{
    (void)p;
    if (!ack_wait || release_ack_hold)
        return SYS_ERR_NOT_FOUND;
    *out = ack;
    if (bad_ack)
    {
        bad_ack--;
        out->bridge.lease++;
    }
    else
        ack_wait = 0;
    return SYS_OK;
}

static SysResult_t consumed(void *p, const MRReceipt *r)
{
    SysResult_t z;
    (void)p;
    actor = 3;
    z = MR_ConfirmConsumed(&mr, r);
    if (!z)
        z = MR_Retire(&mr, r);
    actor = 1;
    retired_fact = !z;
    return z;
}

static SysResult_t retired(void *p, const MRReceipt *r)
{
    (void)p;
    if (!retired_fact)
        return SYS_ERR_BUSY;
    GUARD(r->serial == ack.serial);
    retired_fact = 0;
    return SYS_OK;
}

static const VUSBFrameOps frame_ops = {
    usb_owner,    prepare,  prepared,   commit,       cancel_frame_request,
    cancelled,    offer,    submit_uvc, readers_safe, return_frame,
    recv_receipt, consumed, retired};

static SysResult_t start(void *p, uint32_t mode, const VideoStreamConfig_t *n)
{
    (void)p;
    GUARD(mode == VU_MODE_UVC && n->fps == 10 && usb.consumer.grant.session);
    started++;
    hardware_ready = 1;
    return startup_fault ? SYS_ERR_IO : SYS_OK;
}

static void mode_process(void *p)
{
    (void)p;
    if (work_return)
    {
        MRReceipt r;
        SysResult_t z;
        actor = 3;
        z = MR_Return(&mr, &staged, &delivery.data.media);
        /* IO can be a frozen terminal result after the return has reconciled. */
        if (z == SYS_OK || z == SYS_ERR_IO)
            z = MR_ReadReceipt(&mr, &staged, &r);
        if (!z)
            z = MR_FlushReceipt(&mr, &r, flush_receipt, 0);
        actor = 1;
        if (!z)
            work_return = 0;
    }
}

static SysResult_t cancel(void *p)
{
    (void)p;
    if (abort_once)
    {
        abort_once--;
        return SYS_ERR_IO;
    }
    cpu_read = 0;
    return hold_dma ? SYS_ERR_BUSY : SYS_OK;
}

static void uvc_process(void *p)
{
    (void)p;
}

static SysResult_t stop_mode(void *p)
{
    (void)p;
    GUARD(!cpu_read && !hold_dma && !owned && !ack_wait && usb.frame.phase == VUF_IDLE);
    stopped_calls++;
    if (stop_fail)
    {
        stop_fail--;
        return SYS_ERR_IO;
    }
    hardware_ready = 0;
    return SYS_OK;
}

static SysResult_t observe(void *p, VUSBObservation *o)
{
    (void)p;
    o->flags = hardware_ready ? VUSB_MODE_STARTED : VUSB_MODE_STOPPED;
    if (class_ready)
        o->flags |= VUSB_CLASS_READY;
    if (connected)
        o->flags |= VUSB_HOST_CONFIGURED | VUSB_CAN_SUBMIT;
    if (cpu_read)
        o->flags |= VUSB_FRAME_OWNED;
    if (!cpu_read && !hold_dma)
        o->flags |= VUSB_READERS_QUIET;
    if (!owned && !work_return)
        o->flags |= VUSB_BRIDGE_IDLE;
    o->result = SYS_OK;
    return SYS_OK;
}

static const VUSBOps usb_ops = {usb_owner,   start,     mode_process, cancel,
                                uvc_process, stop_mode, observe};
#define QUEUE(T, N, OCC, FULL)                                                                     \
    static SysResult_t send_##N(void *p, const T *m)                                               \
    {                                                                                              \
        (void)p;                                                                                   \
        if (OCC || FULL)                                                                           \
            return SYS_ERR_NO_RESOURCE;                                                            \
        wire_##N = *m;                                                                             \
        OCC = 1;                                                                                   \
        return SYS_OK;                                                                             \
    }                                                                                              \
    static SysResult_t recv_##N(void *p, T *m)                                                     \
    {                                                                                              \
        (void)p;                                                                                   \
        if (!OCC)                                                                                  \
            return SYS_ERR_NOT_FOUND;                                                              \
        *m = wire_##N;                                                                             \
        OCC = 0;                                                                                   \
        return SYS_OK;                                                                             \
    }
QUEUE(LCRequest, request, sr, full_request)
QUEUE(VUSBReply, reply, sp, full_reply)
QUEUE(LCCompletion, ack, sa, full_ack)

static SysResult_t notify(void *p, uint32_t role_id)
{
    (void)p;
    (void)role_id;
    notification_count++;
    return SYS_OK;
}
static const VUSBTransportOps transport_ops = {usb_owner,    sup_owner,  send_request,
                                               recv_request, send_reply, recv_reply,
                                               send_ack,     recv_ack,   notify};

static SysResult_t pump(unsigned rounds)
{
    SysResult_t z = SYS_OK;
    actor = 1;
    while (rounds--)
    {
        z = VUSBEndpoint_RunRound(&ep, ++now);
        if (z != SYS_OK && z != SYS_ERR_NO_RESOURCE)
            break;
    }
    actor = 2;
    return z;
}

static SysResult_t request(uint32_t op, uint32_t state)
{
    cmd.id.module = 2;
    cmd.id.session = 7;
    cmd.id.operation = op;
    cmd.id.request++;
    cmd.expected = state;
    cmd.timeout = 100;
    return iface.submit(iface.port, &cmd);
}

static SysResult_t consume(void)
{
    SysResult_t z = iface.read(iface.port, &cmd.id, &done);
    return z ? z : iface.consume(iface.port, &done);
}

int USB_Run(uint32_t id)
{
    static V2CompositionCandidate candidate;
    MRSource source = {&backend, 0};
    VCameraResources camera_resources = {source, source, &mr, &mr};
    VUSBTransport transport = {&transport_ops, 0};
    VUSBStatus status;
    CoreFrameRate invalid_rate = {25, 2};
    VideoStreamConfig_t config;
    jpeg = id == 18;
    actor = 3;
    TEST(MR_Init(&mr, &source) == SYS_OK);
    TEST(VCamera_Init(&camera, 3, 7, &camera_ops, 0, &camera_resources) == SYS_OK);
    actor = 1;
    TEST(VUSB_Init(&usb, 2, 7, &usb_ops, 0, &client_ops, 0, &frame_ops, 0) == SYS_OK);
    TEST(VUSBEndpoint_Init(&ep, &usb, &transport) == SYS_OK);
    TEST(VUSB_QueryCapabilities(&usb, &format, &invalid_rate, &config) == SYS_ERR_NOT_SUPPORTED);
    candidate.candidate_only = candidate.media.pipeline_mask = candidate.media.entity_mask = 1;
    candidate.modules.selected_mask = 3;
    candidate.modules.count = 2;
    candidate.modules.order[0].value = 3;
    candidate.modules.order[1].value = 2;
    candidate.media.format = jpeg ? jpeg_format : format;
    actor = 3;
    TEST(VCamera_BindCandidate(&camera, &registry, &candidate) == SYS_OK);
    actor = 1;
    TEST(VUSB_BindCandidate(&usb, &registry, &candidate, VU_MODE_UVC, &rate) == SYS_OK);
    actor = 2;
    TEST(VUSBClient_Init(&client, 2, 7, &transport) == SYS_OK);
    TEST(VUSBClient_Endpoint(&client, &iface) == SYS_OK);
    g_sizes[0] = sizeof(VUSB);
    g_sizes[1] = sizeof(VUSBEndpoint);
    g_sizes[2] = sizeof(VUSBClient);
    g_sizes[3] = sizeof(AppLifecycleMessage_t);
    g_sizes[4] = sizeof(VUSBFrame);
    g_sizes[5] = sizeof(VUSBConsumer);
    g_sizes[6] = offsetof(VUSB, consumer);
    g_sizes[7] = offsetof(VUSB, frame);
    if (id == 1)
    {
        full_request = 1;
        TEST(request(LC_CONFIGURE, LC_UNCONFIGURED) == SYS_ERR_NO_RESOURCE);
        TEST(pump(3) == SYS_OK && !ep.active);
        full_request = 0;
        TEST(iface.submit(iface.port, &cmd) == SYS_OK);
        TEST(iface.submit(iface.port, &cmd) == SYS_OK);
        full_reply = 1;
        TEST(pump(4) == SYS_ERR_NO_RESOURCE);
        full_reply = 0;
        TEST(pump(1) == SYS_OK);
        full_ack = 1;
        TEST(consume() == SYS_ERR_NO_RESOURCE && ep.active);
        TEST(pump(2) == SYS_OK && ep.active);
        TEST(iface.submit(iface.port, &cmd) == SYS_OK);
        full_ack = 0;
    }
    else
    {
        TEST(request(LC_CONFIGURE, LC_UNCONFIGURED) == SYS_OK);
        TEST(pump(3) == SYS_OK);
    }
    TEST(consume() == SYS_OK);
    TEST(pump(1) == SYS_OK && !ep.active && !started);
    if (id == 1)
    {
        wire_request = cmd;
        sr = 1;
        TEST(pump(1) == SYS_OK && !ep.active && !sp);
    }
    TEST(request(LC_ACTIVATE, LC_INACTIVE) == SYS_OK);
    if (id == 2)
        lifecycle_full = 1;
    if (id == 12)
        startup_fault = 1;
    TEST(pump(6) == SYS_OK);
    if (id == 2)
    {
        TEST(!started && !usb.consumer.grant.session &&
             usb.consumer.request.identity.request_id == 1);
        lifecycle_full = 0;
        TEST(pump(6) == SYS_OK);
    }
    if (id == 12)
    {
        TEST(pump(16) == SYS_OK && consume() == SYS_OK && done.result == SYS_ERR_IO);
        TEST(done.fact == LC_ERROR && stopped_calls == 1 && !usb.consumer.grant.session);
        return 0;
    }
    TEST(started == 1 && !physical_calls && !sp);
    if (id == 3)
    {
        wire_reply.completion.id = cmd.id;
        wire_reply.completion.id.session--;
        sp = 1;
        TEST(iface.read(iface.port, &cmd.id, &done) == SYS_ERR_NOT_FOUND);
    }
    class_ready = 1;
    TEST(pump(3) == SYS_OK && consume() == SYS_OK);
    TEST(done.result == SYS_OK && done.fact == LC_ACTIVE && !physical_calls);
    if (id == 4)
        duplicate = 1;
    connected = 1;
    if (id == 13)
    {
        TEST(pump(2) == SYS_OK && usb.frame.phase == VUF_PREPARED);
        TEST(request(LC_DEACTIVATE, LC_ACTIVE) == SYS_OK);
        TEST(pump(18) == SYS_OK && consume() == SYS_OK && done.result == SYS_OK);
        TEST(!physical_calls && !return_sent && stopped_calls == 1);
        return 0;
    }
    TEST(pump(7) == SYS_OK && physical_calls == 1 && cpu_read && owned);
    if (id == 5)
    {
        connected = 0; /* reset/disconnect/suspend: 停止新需求，资格仍有效。 */
        TEST(pump(3) == SYS_OK && usb.consumer.phase == VUC_GRANTED && !return_sent);
    }
    if (id == 6 || id == 16)
        abort_once = 1;
    if (id == 7)
        stop_fail = 1;
    if (id == 8)
        fallback = 1;
    if (id == 9)
        release_fail = 1;
    if (id == 10)
        bad_ack = 1;
    if (id == 11 || id == 18)
        sd = (MRGrant){9, APP_CONSUMER_STORAGE, 1, 24};
    if (id == 18)
    {
        MRRequest request_sd;
        MRMedia media_sd;
        MRView view_sd;
        MRBridge bridge_sd = {0};
        MRSlot slot_sd = {9, 1, 1};
        actor = 3;
        TEST(MR_AssociateGrant(&mr, &sd) == SYS_OK);
        TEST(MR_Stage(&mr, &sd, &bridge_sd, &slot_sd, &request_sd) == SYS_OK);
        TEST(MR_Commit(&mr, &request_sd) == SYS_OK && MR_Admit(&mr, &request_sd) == SYS_OK);
        TEST(MR_Obtain(&mr, &request_sd, &media_sd, &view_sd) == SYS_OK);
        TEST(sd_owned && owned && view_sd.data == delivery.data.view.data &&
             view_sd.bytes2 && media_sd.id != delivery.data.media.id);
        actor = 2;
    }
    hold_dma = 1;
    TEST(request(LC_DEACTIVATE, LC_ACTIVE) == SYS_OK);
    TEST(pump(5) == SYS_OK);
    TEST(owned && !return_sent && !unsafe_return && !stopped_calls && usb.consumer.grant.session);
    if (id == 6 || id == 16)
    {
        TEST(consume() == SYS_OK && done.result == SYS_ERR_IO && done.fact == LC_ERROR);
        TEST(!release_attempts && physical_calls == 1);
    }
    if (id == 15)
    {
        LCRequest concurrent = cmd;
        concurrent.id.request++;
        concurrent.id.operation = LC_ACTIVATE;
        concurrent.expected = LC_INACTIVE;
        TEST(iface.submit(iface.port, &concurrent) == SYS_ERR_BUSY);
        TEST(pump(3) == SYS_OK && iface.submit(iface.port, &concurrent) == SYS_ERR_BUSY);
        TEST(started == 1 && owned && !return_sent);
    }
    if (id == 14)
    {
        TEST(pump(110) == SYS_OK && owned && !stopped_calls && !sp);
        TEST(iface.read(iface.port, &cmd.id, &done) == SYS_ERR_NOT_FOUND);
    }
    hold_dma = 0;
    ack_full = 1;
    TEST(pump(7) == SYS_OK && return_sent == 1 && !owned && !stopped_calls);
    TEST(usb.consumer.grant.session && usb.frame.phase == VUF_WAIT_ACK);
    ack_full = 0;
    release_ack_hold = 1;
    TEST(pump(3) == SYS_OK && !stopped_calls && ack_wait && usb.consumer.grant.session);
    release_ack_hold = 0;
    stop_if_idle_hold = id == 17;
    TEST(pump(20) == SYS_OK);
    if (id == 8)
    {
        TEST(stopped_calls == 1 && usb.consumer.phase == VUC_BLOCKED && usb.consumer.grant.session);
        TEST(consume() == SYS_OK && done.result == SYS_ERR_PROTOCOL && done.fact == LC_ERROR);
        return 0;
    }
    if (id != 6 && id != 16)
        TEST(consume() == SYS_OK);
    if (id == 17)
    {
        TEST(done.result == SYS_OK && done.fact == LC_INACTIVE);
        TEST(pump(4) == SYS_OK && usb.consumer.phase == VUC_IDLE_REQUEST &&
             !usb.consumer.grant.session && stopped_calls == 1);
        stop_if_idle_hold = 0;
    }
    TEST(pump(2) == SYS_OK && usb.consumer.phase == VUC_IDLE && !usb.consumer.grant.session);
    TEST(physical_calls == 1 && return_sent == 1 && released && stopped_calls == (id == 7 ? 2 : 1));
    TEST(release_attempts == (id == 9 ? 2 : 1));
    TEST(done.result == (id == 6 || id == 7 || id == 9 || id == 16 ? SYS_ERR_IO
                         : id == 14                                ? SYS_ERR_TIMEOUT
                                                                   : SYS_OK));
    actor = 1;
    TEST(VUSB_QueryStatus(&usb, &status) == SYS_OK);
    TEST(!status.drain && status.camera_stop_result == (id == 11 || id == 18 ? SYS_ERR_BUSY : SYS_OK));
    TEST((id != 6 && id != 7 && id != 9 && id != 16) || status.first_fault == SYS_ERR_IO);
    actor = 2;
    TEST(request(id == 6 || id == 7 || id == 9 || id == 16 ? LC_ERROR_PROCESS : LC_CLEANUP,
                 done.fact) == SYS_OK);
    TEST(pump(4) == SYS_OK && consume() == SYS_OK && done.fact == LC_UNCONFIGURED);
    if (id == 16)
    {
        uint32_t old_h = usb.frame.highest_h;
        MRGrant old_g = staged.g;
        actor = 1;
        TEST(VUSB_BindCandidate(&usb, &registry, &candidate, VU_MODE_UVC, &rate) == SYS_ERR_BUSY);
        TEST(pump(1) == SYS_OK);
        actor = 1;
        TEST(VUSB_BindCandidate(&usb, &registry, &candidate, VU_MODE_UVC, &rate) == SYS_OK);
        actor = 2;
        TEST(request(LC_CONFIGURE, LC_UNCONFIGURED) == SYS_OK);
        TEST(pump(3) == SYS_OK && consume() == SYS_OK);
        TEST(request(LC_ACTIVATE, LC_INACTIVE) == SYS_OK);
        TEST(pump(9) == SYS_OK && consume() == SYS_OK);
        TEST(pump(8) == SYS_OK && usb.consumer.grant.generation > old_g.generation);
        TEST(usb.frame.highest_h > old_h && physical_calls == 2 && started == 2);
        actor = 1;
        TEST(VUSB_QueryStatus(&usb, &status) == SYS_OK && status.first_fault == SYS_ERR_IO &&
             status.active_fault == SYS_OK);
        TEST(VUSB_QueryExitEvidence(&usb, &old_g) == SYS_ERR_PROTOCOL);
    }
    TEST(id != 18 || (sd_owned && !owned && physical_calls == 1));
    g_trace[0] = notification_count;
    g_trace[1] = physical_calls;
    g_trace[2] = release_attempts;
    g_trace[3] = stopped_calls;
    g_trace[4] = commits;
    g_trace[5] = return_sent;
    g_trace_count = 1;
    return 0;
}

/* 真实运行的代码不得走未接入的 Legacy 硬件入口。 */
__asm void USB_UnboundTrap(void)
{
    BKPT 0
    B USB_UnboundTrap
}
void App_CameraQualificationAccess(void)
{
    USB_UnboundTrap();
}
void App_MessagesIsOwner(void)
{
    USB_UnboundTrap();
}
void Platform_SyncGetException(void)
{
    USB_UnboundTrap();
}
