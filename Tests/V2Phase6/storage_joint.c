#include "../../CM7/ApplicationV2/Modules/Storage/storage_endpoint.h"
#include "../../CM7/ApplicationV2/Modules/Camera/camera_module.h"
#include "sd_block.h"
#include <string.h>

uint32_t g_failure;
uint32_t g_trace_count, g_trace[96][8];
uint32_t g_sizes[8];
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
            if (!g_failure)                                                                        \
                g_failure = __LINE__;                                                              \
            return SYS_ERR_PROTOCOL;                                                               \
        }                                                                                          \
    } while (0)

#define ACT_STORAGE 1U
#define ACT_SUPERVISOR 2U
#define ACT_CAMERA 3U
#define E_MOUNT 1U
#define E_OPEN 2U
#define E_WRITE 3U
#define E_SYNC 4U
#define E_CLOSE 5U
#define E_UNMOUNT 6U
#define E_POST 7U
#define E_GET 8U
#define E_RETURN 9U
#define E_ACK 10U
#define E_CONSUMED 11U
#define E_RETIRED 12U
#define E_RELEASE 13U
#define E_RECOVER 14U

static VStorage storage;
static VStorageFS file;
static VStorageClient client;
static VStorageEndpoint endpoint;
static EMEndpoint em;
static VCamera camera;
static MRContext mr;
static V2CompositionCandidate candidate;
static EMPlan plan;
static const uint32_t cold_modules[] = {LC_UNCONFIGURED, LC_UNCONFIGURED};
static const uint32_t active_modules[] = {LC_ACTIVE, LC_ACTIVE};
static VStorageConfig config;
static MRGrant grant, usb_grant, credit_grant;
static MRSlot credit_slot;
static MRRequest handoff, usb_request;
static MRMedia usb_media;
static MRView usb_view;
static VStorageOffer offer_message;
static MRReceipt receipt_message, consume_message, retired_message;
static MRRelease camera_release;
static AppLifecycleMessage_t request_msg, response_msg, release_msg;
static LCRequest command, req_wire;
static VStorageReply reply_wire;
static LCCompletion ack_wire, completion;
static uint32_t actor, now, case_id;
static uint32_t pending_req, pending_ack, pending_reply, block_req, block_ack, block_reply;
static uint32_t pending_life, pending_response, pause_release, invalid_release, stale_reply;
static uint32_t credit, cancel_credit, offer, return_pending, receipt, consumed, retired;
static uint32_t pause_camera, pause_offer, pause_receipt, pause_consume, stale_offer, stale_receipt;
static uint32_t deny_acquire, fail_mount, fail_close, fail_finish, fail_unmount, fail_readback;
static uint32_t fail_write, dma_fault, dma_active, mounted, has_file, open_mode, fs_fault;
static uint32_t wrote_payload, writes_attempted, mounts, opens, closes, unmounts, recoveries;
static uint32_t writes, syncs, io_position, io_size, creates, reads, exists;
static uint32_t sd_held, usb_held, native_gets, media_returns, native_returns, release_requests;
static uint32_t post_calls, return_calls, consumption_calls, pause_post, pause_return,
    transport_signals;
static uint8_t bytes[4096];
static const uint8_t jpeg[] = {0xff, 0xd8, 0xff, 0xc0, 0, 8, 8,    0,    240, 1,
                               64,   1,    0xff, 0xda, 0, 2, 0x34, 0xff, 0xd9};
static const CoreMediaFormat jpeg_format = {1, 4, 2, 320, 240, 0, 50000, 50000, 1, 2, 2, 4, 1, 0};
static const VStorageCaps caps = {70000, 60};
static const V2Provided capture = {{1}, 1, 0};
static const V2Requirement needs_capture = {{1}, 1, 0, 1, 0, {3}, 0};
static const V2ModuleDesc modules[] = {{.id = {3},
                                        .major = 1,
                                        .provided = &capture,
                                        .provided_count = 1,
                                        .lifecycle_ops = &VCamera_LifecycleOps},
                                       {.id = {4},
                                        .major = 1,
                                        .required = &needs_capture,
                                        .required_count = 1,
                                        .lifecycle_ops = &VStorage_LifecycleOps}};
static const V2Registry registry = {modules, 2, 0};

static void event(uint32_t type)
{
    uint32_t *r;
    if (g_trace_count >= 96)
        return;
    r = g_trace[g_trace_count++];
    r[0] = type;
    r[1] = actor;
    r[2] = now;
    r[3] = grant.generation;
    r[4] = handoff.h.sequence;
    r[5] = handoff.s.generation;
    r[6] = storage.frame.state;
    r[7] = sd_held;
}

static int storage_owner(void *p)
{
    (void)p;
    return actor == ACT_STORAGE;
}

static int supervisor_owner(void *p)
{
    (void)p;
    return actor == ACT_SUPERVISOR;
}

static bool camera_owner(void *p)
{
    (void)p;
    return actor == ACT_CAMERA;
}

/* 运行真实V2适配、AVI和JPEG。仅物理卷/文件IO使用内存替身。 */
bool SD_Block_IsOwner(SdBlockClient_t whom)
{
    return whom == SD_BLOCK_CLIENT_FATFS && storage_owner(NULL) && mounted;
}

SysResult_t SD_Block_GetStatus(SdBlockStatus_t *s)
{
    GUARD(storage_owner(NULL) && mounted);
    *s = (SdBlockStatus_t){true, !dma_fault, dma_active != 0, dma_fault != 0};
    return SYS_OK;
}

SysResult_t StorageFS_GetDiagnostics(StorageFsDiagnostics_t *d)
{
    GUARD(storage_owner(NULL));
    memset(d, 0, sizeof(*d));
    d->state = fs_fault ? 3 : mounted ? 2 : 1;
    d->open_files = has_file;
    return SYS_OK;
}

SysResult_t StorageFS_Mount(bool writable, uint8_t partition)
{
    GUARD(storage_owner(NULL) && writable && !partition && !mounted);
    mounts++;
    event(E_MOUNT);
    if (fail_mount)
        return SYS_ERR_IO;
    mounted = 1U;
    return SYS_OK;
}

SysResult_t StorageFS_Open(const char *path, uint32_t mode, StorageFile_t *f)
{
    GUARD(storage_owner(NULL) && mounted && !has_file && !strcmp(path, config.path));
    opens++;
    event(E_OPEN);
    if (mode == (STORAGE_FS_OPEN_WRITE | STORAGE_FS_OPEN_CREATE_NEW))
    {
        if (exists)
            return SYS_ERR_ALREADY_EXISTS;
        creates++;
        exists = 1;
        io_size = 0;
    }
    else
    {
        GUARD(mode == STORAGE_FS_OPEN_READ && exists);
    }
    open_mode = mode;
    io_position = 0;
    has_file = 1;
    *f = 2;
    return SYS_OK;
}

SysResult_t StorageFS_Write(StorageFile_t f, const void *data, uint32_t n, uint32_t *done)
{
    GUARD(storage_owner(NULL) && has_file && f == 2 && open_mode != STORAGE_FS_OPEN_READ);
    GUARD(n <= sizeof(bytes) - io_position);
    writes++;
    event(E_WRITE);
    if (data == jpeg || data == jpeg + 7)
    {
        writes_attempted++;
        if (fail_write)
        {
            *done = 0;
            if (fail_write == 2)
                dma_active = dma_fault = fs_fault = 1;
            return SYS_ERR_IO;
        }
        wrote_payload++;
    }
    memcpy(bytes + io_position, data, n);
    io_position += n;
    if (io_size < io_position)
        io_size = io_position;
    *done = n;
    return SYS_OK;
}

SysResult_t StorageFS_Seek(StorageFile_t f, uint32_t offset)
{
    GUARD(storage_owner(NULL) && has_file && f == 2 && offset <= io_size);
    io_position = offset;
    return SYS_OK;
}

SysResult_t StorageFS_GetSize(StorageFile_t f, uint32_t *n)
{
    GUARD(storage_owner(NULL) && has_file && f == 2);
    *n = io_size;
    return SYS_OK;
}

SysResult_t StorageFS_Read(StorageFile_t f, void *data, uint32_t n, uint32_t *got)
{
    GUARD(storage_owner(NULL) && has_file && f == 2 && open_mode == STORAGE_FS_OPEN_READ);
    GUARD(n <= io_size - io_position);
    memcpy(data, bytes + io_position, n);
    if (fail_readback && n)
        ((uint8_t *)data)[0] ^= 1U;
    io_position += n;
    *got = n;
    reads++;
    return SYS_OK;
}

SysResult_t StorageFS_Sync(StorageFile_t f)
{
    GUARD(storage_owner(NULL) && has_file && f == 2 && !dma_active);
    syncs++;
    event(E_SYNC);
    return fail_finish ? SYS_ERR_IO : SYS_OK;
}

SysResult_t StorageFS_Close(StorageFile_t f)
{
    GUARD(storage_owner(NULL) && has_file && f == 2 && !dma_active);
    closes++;
    event(E_CLOSE);
    if (fail_close)
    {
        fs_fault = 1;
        return SYS_ERR_IO;
    }
    has_file = 0;
    return SYS_OK;
}

SysResult_t StorageFS_Unmount(void)
{
    GUARD(storage_owner(NULL) && mounted && !has_file && !dma_active);
    unmounts++;
    event(E_UNMOUNT);
    if (fail_unmount)
    {
        fs_fault = 1;
        return SYS_ERR_IO;
    }
    mounted = 0;
    return SYS_OK;
}

SysResult_t StorageFS_Recover(void)
{
    GUARD(storage_owner(NULL) && mounted && fs_fault);
    recoveries++;
    event(E_RECOVER);
    if (case_id == 14 && recoveries == 1)
        return SYS_ERR_IO;
    mounted = has_file = fs_fault = dma_active = dma_fault = 0;
    return SYS_OK;
}

static SysResult_t caps_for_source(void *p, MRSourceCaps *out)
{
    (void)p;
    *out = (MRSourceCaps){MR_JPEG, MR_CAP_SD | MR_CAP_USB, 2, 2};
    return SYS_OK;
}

static SysResult_t role(void *p, const MRGrant *g, uint32_t *out)
{
    (void)p;
    if (MRGrant_Equal(g, &grant))
        *out = MR_SD;
    else if (MRGrant_Equal(g, &usb_grant) && usb_grant.session)
        *out = MR_USB;
    else
        return SYS_ERR_PROTOCOL;
    return SYS_OK;
}

static SysResult_t valid_grant(void *p, const MRGrant *g)
{
    uint32_t r;
    return role(p, g, &r);
}

static SysResult_t obtain(void *p, const MRGrant *g, const MRHandoff *h, MRMedia *m, MRView *v)
{
    (void)p;
    GUARD(camera_owner(NULL) && h->session == g->session);
    if (g->consumer == APP_CONSUMER_STORAGE)
    {
        GUARD(!sd_held);
        sd_held = 1;
        native_gets++;
        event(E_GET);
    }
    else
    {
        GUARD(!usb_held);
        usb_held = 1;
    }
    *m = (MRMedia){MR_JPEG, g->session, h->sequence, g->generation};
    *v = (MRView){jpeg, 7, jpeg + 7, sizeof(jpeg) - 7};
    return SYS_OK;
}

static SysResult_t validate(void *p, const MRGrant *g, const MRMedia *m, const MRView *v)
{
    (void)p;
    GUARD(camera_owner(NULL) && m->session == g->session && v->data == jpeg);
    return (g->consumer == APP_CONSUMER_STORAGE ? sd_held : usb_held) ? SYS_OK
                                                                      : SYS_ERR_INVALID_STATE;
}

static SysResult_t readers(void *p, const MRGrant *g, const MRMedia *m)
{
    (void)p;
    GUARD(g->session == m->session);
    return dma_active ? SYS_ERR_BUSY : SYS_OK;
}

static SysResult_t release(void *p, const MRGrant *g, const MRMedia *m)
{
    GUARD(camera_owner(NULL) && sd_held && g->consumer == APP_CONSUMER_STORAGE &&
          !readers(p, g, m));
    native_returns++;
    if (case_id == 10 && native_returns == 1)
        return SYS_ERR_IO;
    sd_held = 0;
    media_returns++;
    return SYS_OK;
}

static SysResult_t released(void *p, const MRGrant *g, const MRMedia *m)
{
    (void)p;
    GUARD(camera_owner(NULL) && m->session == g->session);
    return !sd_held && media_returns ? SYS_OK : SYS_ERR_BUSY;
}

static SysResult_t exit_safe(void *p, const MRGrant *g)
{
    (void)p;
    GUARD(camera_owner(NULL));
    return (g->consumer == APP_CONSUMER_STORAGE ? sd_held : usb_held) ? SYS_ERR_BUSY : SYS_OK;
}

static const MRSourceOps source_ops = {camera_owner, caps_for_source, role,    valid_grant,
                                       obtain,       validate,        readers, release,
                                       released,     exit_safe};

static SysResult_t post(void *p, const MRGrant *g, const MRSlot *s)
{
    (void)p;
    GUARD(storage_owner(NULL) && MRGrant_Equal(g, &grant) && s->session == g->session);
    post_calls++;
    if (pause_post)
        return SYS_ERR_NO_RESOURCE;
    GUARD(!credit);
    credit = 1;
    credit_grant = *g;
    credit_slot = *s;
    event(E_POST);
    return SYS_OK;
}

static SysResult_t withdraw(void *p, const MRGrant *g, const MRSlot *s)
{
    (void)p;
    GUARD(storage_owner(NULL) && MRGrant_Equal(g, &credit_grant) &&
          !memcmp(s, &credit_slot, sizeof(*s)));
    cancel_credit = credit;
    return SYS_OK;
}

static SysResult_t read_offer(void *p, VStorageOffer *out)
{
    (void)p;
    GUARD(storage_owner(NULL));
    if (!offer || pause_offer)
        return SYS_ERR_NOT_FOUND;
    *out = offer_message;
    if (stale_offer)
    {
        stale_offer = 0;
        out->request.s.generation--;
    }
    else
        offer = 0;
    return SYS_OK;
}

static SysResult_t post_return(void *p, const MRRequest *r, const MRMedia *m)
{
    (void)p;
    GUARD(storage_owner(NULL) && !dma_active && !memcmp(r, &handoff, sizeof(*r)) &&
          MRMedia_Equal(m, &offer_message.media));
    return_calls++;
    if (pause_return)
        return SYS_ERR_NO_RESOURCE;
    GUARD(!return_pending && sd_held);
    return_pending = 1;
    event(E_RETURN);
    return SYS_OK;
}

static SysResult_t send_receipt(void *p, const MRReceipt *r)
{
    (void)p;
    GUARD(camera_owner(NULL) && !sd_held && r->physical_returned && !receipt);
    if (pause_receipt)
        return SYS_ERR_NO_RESOURCE;
    receipt_message = *r;
    receipt = 1;
    event(E_ACK);
    return SYS_OK;
}

static SysResult_t read_receipt(void *p, MRReceipt *t)
{
    (void)p;
    GUARD(storage_owner(NULL));
    if (!receipt)
        return SYS_ERR_NOT_FOUND;
    *t = receipt_message;
    if (stale_receipt)
    {
        stale_receipt = 0;
        t->m.generation++;
    }
    else
        receipt = 0;
    return SYS_OK;
}

static SysResult_t post_consumed(void *p, const MRReceipt *t)
{
    (void)p;
    GUARD(storage_owner(NULL));
    consumption_calls++;
    if (pause_consume)
        return SYS_ERR_NO_RESOURCE;
    GUARD(!consumed);
    consume_message = *t;
    consumed = 1;
    event(E_CONSUMED);
    return SYS_OK;
}

static SysResult_t read_retired(void *p, const MRReceipt *t)
{
    (void)p;
    GUARD(storage_owner(NULL));
    if (!retired)
        return SYS_ERR_BUSY;
    GUARD(!memcmp(t, &retired_message, sizeof(*t)));
    retired = 0;
    return SYS_OK;
}

static const VStorageFrameOps frame_ops = {post,         withdraw,      read_offer,  post_return,
                                           read_receipt, post_consumed, read_retired};

static SysResult_t life_send(void *p, const AppLifecycleMessage_t *m)
{
    (void)p;
    GUARD(storage_owner(NULL) && App_LifecycleValidate(m) && m->source == APP_LIFECYCLE_STORAGE &&
          m->identity.consumer_id == APP_CONSUMER_STORAGE);
    if (pending_life || pending_response)
        return SYS_ERR_NO_RESOURCE;
    request_msg = *m;
    pending_life = 1U;
    return SYS_OK;
}

static SysResult_t life_recv(void *p, AppLifecycleMessage_t *m)
{
    (void)p;
    GUARD(storage_owner(NULL));
    if (!pending_response)
        return SYS_ERR_NOT_FOUND;
    *m = response_msg;
    if (stale_reply)
    {
        stale_reply = 0;
        m->identity.consumer_generation++;
    }
    else
        pending_response = 0U;
    return SYS_OK;
}

static const VStorageConsumerOps life_ops = {storage_owner, life_send, life_recv};

static int camera_port_owner(void *p)
{
    return camera_owner(p);
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
    (void)p;
    (void)r;
    (void)phase;
    return SYS_ERR_NOT_SUPPORTED;
}

static SysResult_t camera_request(void *p, const LCRequest *r, const CameraConfig_t *c)
{
    (void)p;
    (void)r;
    (void)c;
    return SYS_ERR_NOT_SUPPORTED;
}

static SysResult_t camera_read(void *p, const V2ExecId *id, VCameraResult *r)
{
    (void)p;
    (void)id;
    (void)r;
    return SYS_ERR_NOT_SUPPORTED;
}

static SysResult_t camera_consume(void *p, const VCameraResult *r)
{
    (void)p;
    (void)r;
    return SYS_ERR_NOT_SUPPORTED;
}

static SysResult_t camera_facts(void *p, VCameraObservation *o)
{
    (void)p;
    (void)o;
    return SYS_ERR_NOT_SUPPORTED;
}

static SysResult_t camera_release_id(void *p, const MRRelease *r)
{
    (void)p;
    return r->request == camera_release.request && MRGrant_Equal(&r->grant, &camera_release.grant)
               ? SYS_OK
               : SYS_ERR_PROTOCOL;
}

static void camera_unused(void *p)
{
    (void)p;
}

static const VCameraOps camera_ops = {camera_port_owner, camera_layout,     camera_gate,
                                      camera_request,    camera_read,       camera_consume,
                                      camera_facts,      camera_release_id, camera_unused,
                                      camera_unused,     camera_unused,     camera_unused};

static SysResult_t camera_messages(void)
{
    AppLifecycleMessage_t *m = &request_msg;
    SysResult_t z = SYS_OK;
    if (!pending_life || pending_response ||
        (pause_release && m->operation == APP_LIFECYCLE_RELEASE))
        return SYS_OK;
    if (m->operation == APP_LIFECYCLE_ACQUIRE)
    {
        if (deny_acquire)
            z = SYS_ERR_BUSY;
        else
        {
            grant = (MRGrant){m->identity.session_id, m->identity.consumer_id,
                              m->identity.consumer_generation, m->identity.request_id};
            z = MR_AssociateGrant(&mr, &grant);
        }
    }
    else if (m->operation == APP_LIFECYCLE_RELEASE)
    {
        VCameraExitEvidence e;
        GUARD(!sd_held && !return_pending && !consumed && !receipt &&
              m->identity.request_id != grant.acquire &&
              m->identity.consumer_generation == grant.generation);
        camera_release = (MRRelease){grant, m->identity.request_id};
        z = MR_Close(&mr, &camera_release);
        if (!z)
            z = VCamera_QueryExitEvidence(&camera, &camera_release, &e);
        if (z)
            return z;
        release_requests++;
        release_msg = *m;
        event(E_RELEASE);
    }
    else if (m->operation == APP_LIFECYCLE_STOP_IF_IDLE && usb_held)
        z = SYS_ERR_BUSY;
    GUARD(!App_LifecycleMakeReply(m, 9, z, &response_msg));
    if (m->operation == APP_LIFECYCLE_RELEASE && !invalid_release)
        response_msg.completion_flags = APP_LIFECYCLE_QUALIFICATION_RELEASED;
    pending_life = 0U;
    pending_response = 1U;
    return SYS_OK;
}

static SysResult_t camera_frames(void)
{
    MRBridge empty = {0};
    SysResult_t z;
    if (credit && !offer && (!pause_camera || cancel_credit))
    {
        memset(&offer_message, 0, sizeof(offer_message));
        if (cancel_credit)
        {
            offer_message.request.g = credit_grant;
            offer_message.request.s = credit_slot;
            offer_message.kind = VSF_DELIVERY_WITHDRAWN;
            cancel_credit = 0U;
        }
        else
        {
            z = MR_Stage(&mr, &credit_grant, &empty, &credit_slot, &handoff);
            GUARD(!z && !MR_Commit(&mr, &handoff) && !MR_Admit(&mr, &handoff));
            GUARD(!MR_Obtain(&mr, &handoff, &offer_message.media, &offer_message.view));
            if (case_id == 21)
                offer_message.view.bytes2--;
            offer_message.request = handoff;
            offer_message.kind = VSF_DELIVERY_FRAME;
            offer_message.timestamp = now;
        }
        offer = 1;
        credit = 0;
    }
    if (return_pending)
    {
        MRReceipt t;
        z = MR_Return(&mr, &handoff, &offer_message.media);
        GUARD(z == SYS_OK || z == SYS_ERR_IO);
        z = MR_ReadReceipt(&mr, &handoff, &t);
        if (!z)
        {
            z = MR_FlushReceipt(&mr, &t, send_receipt, NULL);
            if (!z)
                return_pending = 0U;
            else
                GUARD(z == SYS_ERR_NO_RESOURCE);
        }
        else
            GUARD(z == SYS_ERR_BUSY);
    }
    if (consumed)
    {
        GUARD(!MR_ConfirmConsumed(&mr, &consume_message) && !MR_Retire(&mr, &consume_message));
        consumed = 0;
        retired_message = consume_message;
        retired = 1;
        event(E_RETIRED);
    }
    return SYS_OK;
}

#define QUEUE(T, N, PENDING, BLOCK)                                                                \
    static SysResult_t send_##N(void *p, const T *m)                                               \
    {                                                                                              \
        (void)p;                                                                                   \
        if (PENDING || BLOCK)                                                                      \
            return SYS_ERR_NO_RESOURCE;                                                            \
        N##_wire = *m;                                                                             \
        PENDING = 1U;                                                                              \
        return SYS_OK;                                                                             \
    }                                                                                              \
    static SysResult_t recv_##N(void *p, T *m)                                                     \
    {                                                                                              \
        (void)p;                                                                                   \
        if (!PENDING)                                                                              \
            return SYS_ERR_NOT_FOUND;                                                              \
        *m = N##_wire;                                                                             \
        PENDING = 0U;                                                                              \
        return SYS_OK;                                                                             \
    }
QUEUE(LCRequest, req, pending_req, block_req)
QUEUE(VStorageReply, reply, pending_reply, block_reply)
QUEUE(LCCompletion, ack, pending_ack, block_ack)

static SysResult_t notify(void *p, uint32_t target)
{
    (void)p;
    (void)target;
    transport_signals++;
    return SYS_ERR_BUSY; /* 提示可合并或失败，使用者仍轮询可靠队列。 */
}
static const VStorageTransportOps transport_ops = {storage_owner, supervisor_owner, send_req,
                                                   recv_req,      send_reply,       recv_reply,
                                                   send_ack,      recv_ack,         notify};

static int pump(uint32_t cycles)
{
    while (cycles--)
    {
        SysResult_t z;
        ++now;
        actor = ACT_CAMERA;
        if (camera_frames() || camera_messages())
            return 0;
        actor = ACT_STORAGE;
        z = VStorageEndpoint_RunRound(&endpoint, now);
        if (z != SYS_OK && z != SYS_ERR_NO_RESOURCE)
            return 0;
    }
    actor = ACT_SUPERVISOR;
    return g_failure == 0;
}

static SysResult_t submit(uint32_t op, uint32_t expected)
{
    command.id.session = 7;
    command.id.module = 4;
    command.id.request++;
    command.id.operation = op;
    command.expected = expected;
    command.timeout = 60;
    return em.submit(em.port, &command);
}

static SysResult_t consume(void)
{
    SysResult_t z = em.read(em.port, &command.id, &completion);
    return z ? z : em.consume(em.port, &completion);
}

static uint32_t find_event(uint32_t type)
{
    uint32_t i;
    for (i = 0; i < g_trace_count; ++i)
        if (g_trace[i][0] == type)
            return i + 1U;
    return 0U;
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
    uint32_t ms;
    VStorageConfig request = config;
    (void)e;
    (void)c;
    (void)i;
    request.sampling = *r;
    return VStorage_ValidateFormat(f, &request, &caps, &ms);
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
    V2EntityBinding binding[4] = {{{1}, 0, {3}}, {{2}, 0, {3}}, {{3}, 0, {3}}, {{4}, 0, {4}}};
    V2ModuleId root = {4};
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
    result = V2Composition_Validate(&registry, &graph, binding, 4, &desired, resource_preflight,
                                    &composition_scratch, &candidate, &issue, &ge);
    if (result)
        g_failure = 10000U + ge.code;
    return result == 0;
}
int Storage_Run(uint32_t id)
{
    MRSource source = {&source_ops, 0};
    VCameraResources camera_resources = {{0}, source, 0, &mr};
    VStoragePorts ports = {&VStorageFS_Ops, &file, &frame_ops, 0, &life_ops, 0};
    VStorageTransport transport = {&transport_ops, 0};
    VStorageConfig alternative;
    uint32_t interval, flags;
    SysResult_t result;
    int snapshot = id == 1 || id == 7 || id == 12;
    case_id = id;
    g_sizes[0] = sizeof(VStorage);
    g_sizes[1] = sizeof(VStorageFS);
    g_sizes[2] = sizeof(VStorageClient);
    g_sizes[3] = sizeof(VStorageEndpoint);
    g_sizes[4] = sizeof(VStorageFrame);
    g_sizes[5] = sizeof(AppLifecycleMessage_t);
    g_sizes[6] = offsetof(VStorage, consumer);
    g_sizes[7] = offsetof(VStorage, frame);

    config = (VStorageConfig){
        snapshot ? VST_SNAPSHOT : VST_AVI, 0, 65536, 4, {10, 1}, "0:/VID00001.AVI"};
    if (snapshot)
        memcpy(config.path, "0:/IMG00001.JPG", 16U);
    actor = ACT_SUPERVISOR;
    CHECK(composed() && candidate.modules.count == 2 && candidate.modules.order[0].value == 3 &&
          candidate.modules.order[1].value == 4 && candidate.media.sinks[0].sink.value == 4);
    CHECK(EM_BuildPlan(&registry, &candidate, cold_modules, 0, EM_START, 0, &plan) == SYS_OK);
    CHECK(plan.count == 4 && plan.steps[0].module == 3 && plan.steps[1].module == 4 &&
          plan.steps[2].module == 3 && plan.steps[3].module == 4);
    CHECK(EM_BuildPlan(&registry, &candidate, active_modules, 1U, EM_STOP, 0, &plan) ==
          SYS_ERR_BUSY);
    CHECK(EM_BuildPlan(&registry, &candidate, active_modules, 2U, EM_STOP, 0, &plan) == SYS_OK);
    CHECK(plan.count == 1 && plan.steps[0].module == 4 && plan.steps[0].operation == LC_DEACTIVATE);
    actor = ACT_CAMERA;
    CHECK(MR_Init(&mr, &source) == SYS_OK);
    CHECK(VCamera_Init(&camera, 3, 7, &camera_ops, 0, &camera_resources) == SYS_OK);
    CHECK(VCamera_BindCandidate(&camera, &registry, &candidate) == SYS_OK);
    actor = ACT_STORAGE;
    CHECK(VStorageFS_Init(&file, storage_owner, 0) == SYS_OK);
    CHECK(VStorage_Init(&storage, 4, 7, &caps, &ports) == SYS_OK);
    CHECK(VStorage_BindCandidate(&storage, &registry, &candidate, &config) == SYS_OK);
    CHECK(VStorageEndpoint_Init(&endpoint, &storage, &transport) == SYS_OK);
    CHECK(VStorage_QueryCapabilities(&storage, &jpeg_format, &config, &interval) == SYS_OK);
    CHECK(interval == (snapshot ? 0U : 100U) && !mounts);
    alternative = config;
    alternative.path[3] = '/';
    CHECK(VStorage_QueryCapabilities(&storage, &jpeg_format, &alternative, &interval) ==
          SYS_ERR_INVALID_ARG);
    if (!snapshot)
    {
        alternative = config;
        alternative.sampling.den = 2;
        CHECK(VStorage_QueryCapabilities(&storage, &jpeg_format, &alternative, &interval) ==
              SYS_ERR_NOT_SUPPORTED);
    }
    actor = ACT_SUPERVISOR;
    CHECK(VStorageClient_Init(&client, 4, 7, &transport) == SYS_OK);
    CHECK(VStorageClient_Endpoint(&client, &em) == SYS_OK);
    if (id == 2)
    {
        block_req = 1;
        CHECK(submit(LC_CONFIGURE, LC_UNCONFIGURED) == SYS_ERR_NO_RESOURCE && pump(2));
        CHECK(!endpoint.active);
        block_req = 0;
        CHECK(em.submit(em.port, &command) == SYS_OK && em.submit(em.port, &command) == SYS_OK);
        block_reply = 1;
        CHECK(pump(5) && endpoint.active && !pending_reply);
        block_reply = 0;
        CHECK(pump(1));
        block_ack = 1;
        CHECK(consume() == SYS_ERR_NO_RESOURCE && pump(2) && endpoint.active);
        block_ack = 0;
    }
    else
    {
        CHECK(submit(LC_CONFIGURE, LC_UNCONFIGURED) == SYS_OK && pump(4));
    }
    CHECK(consume() == SYS_OK && completion.fact == LC_INACTIVE && !mounts);
    CHECK(pump(1) && !endpoint.active);
    if (id == 2)
    {
        req_wire = command;
        pending_req = 1;
        CHECK(pump(1) && !endpoint.active);
    }
    if (id == 3)
        deny_acquire = 1;
    if (id == 4)
        fail_mount = 1;
    if (id == 11)
        stale_reply = 1;
    if (id == 17)
        exists = 1;
    if (id == 5)
        pause_post = 1;
    if (id == 6)
        pause_camera = 1;
    if (id == 18)
        pause_offer = 1;
    CHECK(submit(LC_ACTIVATE, LC_INACTIVE) == SYS_OK && pump(id == 11 ? 8 : 7));
    if (id == 3 || id == 4 || id == 17)
    {
        CHECK(consume() == SYS_OK);
        CHECK(completion.result == (id == 3   ? SYS_ERR_BUSY
                                    : id == 4 ? SYS_ERR_IO
                                              : SYS_ERR_ALREADY_EXISTS));
        CHECK(pump(15) && !mounted && !has_file && !native_gets);
        CHECK(mounts == (id == 3 ? 0U : 1U) && release_requests == (id == 3 ? 0U : 1U));
        CHECK(id != 17 || (exists && !creates && !writes));
        return 0;
    }
    CHECK(consume() == SYS_OK && completion.fact == LC_ACTIVE && mounts == 1 && creates == 1);
    if (id == 5 || id == 6 || id == 18)
    {
        CHECK(pump(1));
        CHECK(storage.frame.state == (id == 5 ? VSF_POST : VSF_WAIT));
        CHECK(submit(LC_DEACTIVATE, LC_ACTIVE) == SYS_OK && pump(4));
        pause_offer = 0;
        CHECK(pump(18) && consume() == SYS_OK && !mounted && !sd_held && release_requests == 1);
        CHECK(native_gets == (id == 18 ? 1U : 0U));
        CHECK(completion.result == (id == 18 ? SYS_OK : SYS_ERR_NOT_FOUND));
        return 0;
    }
    if (id == 8 || id == 9)
        pause_receipt = 1;
    if (id == 19)
        pause_return = 1;
    if (id == 11)
        stale_offer = stale_receipt = 1;
    if (id == 13 || id == 14)
        fail_write = 2;
    if (id == 15)
        fail_write = 1;
    if (id == 16)
        invalid_release = 1;
    if (id == 7)
        fail_readback = 1;
    if (id == 12)
        pause_consume = 1;
    if (id == 20 || id == 22)
        pause_release = 1;

    if (id == 2)
    {
        MRBridge b = {1, 0, 1};
        MRSlot slot = {0};
        usb_grant = (MRGrant){9, APP_CONSUMER_USB, 1, 31};
        actor = ACT_CAMERA;
        CHECK(MR_AssociateGrant(&mr, &usb_grant) == SYS_OK);
        CHECK(MR_Stage(&mr, &usb_grant, &b, &slot, &usb_request) == SYS_OK);
        CHECK(MR_Commit(&mr, &usb_request) == SYS_OK && MR_Admit(&mr, &usb_request) == SYS_OK);
        CHECK(MR_Obtain(&mr, &usb_request, &usb_media, &usb_view) == SYS_OK);
        actor = ACT_SUPERVISOR;
    }
    CHECK(pump(15) && native_gets == 1);
    if (id == 8)
        fail_finish = 1;
    if (id == 9)
        fail_close = 1;
    if (id == 11)
        fail_unmount = 1;
    if (id == 19)
    {
        CHECK(sd_held && return_calls > 1U && !media_returns && !release_requests);
        pause_return = 0;
    }
    if (id == 12)
    {
        CHECK(storage.frame.state == VSF_CONSUME && consumption_calls && !release_requests);
        CHECK(find_event(E_ACK) && !find_event(E_RETIRED));
        pause_consume = 0;
    }
    if (id == 8 || id == 9)
    {
        CHECK(storage.frame.state == VSF_ACK && !sd_held && !release_requests);
        pause_receipt = 0;
    }
    if (id == 13 || id == 14)
    {
        CHECK(sd_held && !return_calls && !release_requests && mounted);
        CHECK(submit(LC_DEACTIVATE, LC_ACTIVE) == SYS_OK && pump(5));
        CHECK(consume() == SYS_OK && completion.result == SYS_ERR_IO &&
              completion.fact == LC_ERROR);
        CHECK(submit(LC_ERROR_PROCESS, LC_ERROR) == SYS_OK && pump(20));
        CHECK(consume() == SYS_OK);
        if (id == 14)
        {
            CHECK(completion.result == SYS_ERR_IO && sd_held && !release_requests && mounted);
            CHECK(submit(LC_ERROR_PROCESS, LC_ERROR) == SYS_OK && pump(22) && consume() == SYS_OK);
        }
        CHECK(completion.result == SYS_OK && completion.fact == LC_UNCONFIGURED);
        CHECK(!sd_held && !mounted && release_requests == 1 && storage.first_fault == SYS_ERR_IO);
        CHECK(!(release_msg.completion_flags &
                (APP_LIFECYCLE_FILE_FINISHED | APP_LIFECYCLE_FILE_CLOSED)));
        return 0;
    }
    CHECK(submit(LC_DEACTIVATE, LC_ACTIVE) == SYS_OK && pump(15));
    if (id == 22)
    {
        CHECK(pump(70) && !pending_reply && !release_requests && storage.consumer.grant.session);
        pause_release = 0;
    }
    if (id == 20)
    {
        actor = ACT_STORAGE;
        CHECK(VStorage_QueryExitEvidence(&storage, &grant, &flags) == SYS_OK);
        CHECK(flags & APP_LIFECYCLE_FRAMES_DRAINED);
        actor = ACT_SUPERVISOR;
        CHECK(!release_requests && !pending_reply && !sd_held);
        pause_release = 0;
    }
    CHECK(pump(15) && consume() == SYS_OK);
    result = id == 7 || id == 21                                      ? SYS_ERR_PROTOCOL
             : id == 8 || id == 9 || id == 10 || id == 11 || id == 15 ? SYS_ERR_IO
             : id == 16                                               ? SYS_ERR_PROTOCOL
             : id == 22                                               ? SYS_ERR_TIMEOUT
                                                                      : SYS_OK;
    CHECK(completion.result == result &&
          completion.fact == (result && id != 22 ? LC_ERROR : LC_INACTIVE));
    CHECK(pump(6) && media_returns == 1 && release_requests == 1 && !sd_held);
    CHECK(find_event(E_RETURN) < find_event(E_ACK) && find_event(E_ACK) < find_event(E_CONSUMED) &&
          find_event(E_CONSUMED) < find_event(E_RETIRED) &&
          find_event(E_RETIRED) < find_event(E_RELEASE));
    CHECK(usb_held == (id == 2));
    CHECK(id != 2 || storage.consumer.camera_stop_result == SYS_ERR_BUSY);
    CHECK(id != 7 || (reads == 1 && !(release_msg.completion_flags & APP_LIFECYCLE_FILE_FINISHED)));
    CHECK(id != 8 || (syncs && !(release_msg.completion_flags & APP_LIFECYCLE_FILE_FINISHED)));
    CHECK(id != 9 || (mounted && has_file && !unmounts));
    CHECK(id != 10 || (native_returns == 2 && storage.first_fault == SYS_ERR_IO));
    CHECK(id != 11 || (mounted && !has_file &&
                       !(release_msg.completion_flags & APP_LIFECYCLE_VOLUME_RELEASED)));
    CHECK(id != 15 || (writes_attempted == 1 && !wrote_payload));
    CHECK(id != 16 || (storage.consumer.grant.session && storage.consumer.phase == VSC_BLOCKED));
    CHECK(id == 16 || !storage.consumer.grant.session);
    CHECK(id != 21 || !writes_attempted);
    if (!result)
    {
        CHECK(!mounted && !has_file && storage.frames == 1 && syncs && closes && unmounts);
        CHECK(find_event(E_SYNC) < find_event(E_CLOSE) &&
              find_event(E_CLOSE) < find_event(E_UNMOUNT));
        CHECK((release_msg.completion_flags &
               (APP_LIFECYCLE_FILE_FINISHED | APP_LIFECYCLE_FILE_CLOSED |
                APP_LIFECYCLE_VOLUME_RELEASED)) ==
              (APP_LIFECYCLE_FILE_FINISHED | APP_LIFECYCLE_FILE_CLOSED |
               APP_LIFECYCLE_VOLUME_RELEASED));
        if (snapshot)
            CHECK(reads == 1 && io_size == sizeof(jpeg) && !memcmp(bytes, jpeg, io_size));
        else
            CHECK(io_size > sizeof(jpeg) && !memcmp(bytes, "RIFF", 4U));
    }
    if (id == 23)
    {
        MRGrant old = grant;
        uint32_t old_slot = handoff.s.generation;
        CHECK(submit(LC_CLEANUP, LC_INACTIVE) == SYS_OK && pump(4) && consume() == SYS_OK);
        CHECK(pump(1));
        config.path[10] = '2';
        exists = 0;
        actor = ACT_STORAGE;
        CHECK(VStorage_BindCandidate(&storage, &registry, &candidate, &config) == SYS_OK);
        CHECK(VStorage_QueryExitEvidence(&storage, &old, &flags) == SYS_ERR_PROTOCOL);
        actor = ACT_SUPERVISOR;
        CHECK(submit(LC_CONFIGURE, LC_UNCONFIGURED) == SYS_OK && pump(4) && consume() == SYS_OK);
        CHECK(submit(LC_ACTIVATE, LC_INACTIVE) == SYS_OK && pump(10) && consume() == SYS_OK);
        CHECK(pump(12) && grant.generation > old.generation && handoff.s.generation > old_slot);
        CHECK(native_gets == 2 && media_returns == 2 && release_requests == 1);
    }
    return 0;
}

/* 测试故意没有旧任务启动路径和真实传感器。 */
__asm void Storage_UnboundTrap(void)
{
    BKPT 0
    B Storage_UnboundTrap
}
void App_CameraQualificationAccess(void)
{
    Storage_UnboundTrap();
}
void App_MessagesIsOwner(void)
{
    Storage_UnboundTrap();
}
void Platform_SyncGetException(void)
{
    Storage_UnboundTrap();
}
