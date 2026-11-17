#include "../../CM7/ApplicationV2/Resource/media_resource.h"
#include <string.h>
volatile uint32_t g_failure;
volatile uint32_t g_sizes[8];
volatile uint32_t g_trace_count;
volatile uint32_t g_trace[1024][8];
static void event(uint32_t op, const MRGrant *g, uint32_t key, SysResult_t result)
{
    uint32_t i = g_trace_count;
    if (i >= 1024)
        return;
    g_trace[i][0] = op;
    g_trace[i][1] = g->session;
    g_trace[i][2] = g->consumer;
    g_trace[i][3] = g->generation;
    g_trace[i][4] = g->acquire;
    g_trace[i][5] = key;
    g_trace[i][6] = (uint32_t)result;
    g_trace[i][7] = i;
    g_trace_count = i + 1;
}
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            g_failure = __LINE__;                                                                  \
            return __LINE__;                                                                       \
        }                                                                                          \
    } while (0)
static MRContext c;
static struct
{
    uint32_t raw, owner, held[4], gets, releases, sends, dma, fail, pending, queue, deny, io, bad,
        reenter, partial, grant_generation, media_generation, abort, noframe, uncertain;
} f;
static uint8_t data[32];
static MRGrant usb = {1, 1, 1, 7}, sd = {1, 2, 1, 8}, internal = {1, 3, 1, 9};
static MRSource source;
static MRRequest u, s;
static MRMedia um, sm;
static MRView view;
static MRBridge bridge = {2, 3, 4};
static MRSlot slot = {1, 0, 1};
static MRReceipt t, st;
static MRRelease ur = {{1, 1, 1, 7}, 19}, sr = {{1, 2, 1, 8}, 20};
static bool owner(void *x)
{
    (void)x;
    return f.owner != 0;
}
static SysResult_t caps(void *x, MRSourceCaps *o)
{
    (void)x;
    o->backend = f.raw ? 1 : 2;
    o->owners = 7;
    o->spans = f.raw ? 1 : 2;
    o->concurrent = f.bad ? f.bad : (f.raw ? 1 : 2);
    return 0;
}
static SysResult_t valid(void *x, const MRGrant *g)
{
    (void)x;
    return f.deny || g->session != 1 || g->generation != f.grant_generation ? SYS_ERR_INVALID_STATE
                                                                            : 0;
}
static SysResult_t obtain(void *x, const MRGrant *g, const MRHandoff *h, MRMedia *m, MRView *v)
{
    (void)x;
    (void)h;
    f.gets++;
    if (f.noframe) return SYS_ERR_NOT_FOUND;
    if (f.uncertain) { f.held[g->consumer] = 1; return SYS_ERR_IO; }
    if (f.held[g->consumer])
        return SYS_ERR_BUSY;
    /* Controlled backend completes its prior internal original use before a new SD-only loan. */
    if (!f.held[1] && !f.held[2]) f.held[3] = 0;
    if (!f.held[1] && !f.held[2] && !f.held[3])
        f.media_generation++;
    if (g->consumer == MR_SD && !f.held[1]) f.held[3] = 1;
    f.held[g->consumer] = 1;
    m->backend = f.raw ? 1 : 2;
    m->session = 3;
    m->id = 11;
    m->generation = f.media_generation;
    v->data = data;
    v->bytes = 16;
    v->data2 = f.raw ? 0 : data + 16;
    v->bytes2 = f.raw ? 0 : 16;
    return 0;
}
static SysResult_t validate(void *x, const MRGrant *g, const MRMedia *m, const MRView *v)
{
    (void)x;
    (void)g;
    (void)m;
    (void)v;
    return f.io ? SYS_ERR_IO
           : (!f.held[g->consumer] || m->session != 3 || m->id != 11 ||
              m->generation != f.media_generation)
               ? SYS_ERR_NOT_FOUND
               : 0;
}
static SysResult_t quiet(void *x, const MRGrant *g, const MRMedia *m)
{
    (void)x;
    (void)g;
    (void)m;
    return f.abort ? SYS_ERR_IO : f.dma ? SYS_ERR_BUSY : 0;
}
static SysResult_t release(void *x, const MRGrant *g, const MRMedia *m)
{
    (void)x;
    (void)m;
    f.releases++;
    if (f.fail)
        return SYS_ERR_IO;
    if (!f.held[g->consumer])
        return SYS_ERR_INVALID_STATE;
    f.held[g->consumer] = 0;
    return f.partial ? SYS_ERR_IO : 0;
}
static SysResult_t released(void *x, const MRGrant *g, const MRMedia *m)
{
    (void)x;
    (void)m;
    return f.pending || f.held[g->consumer] ? SYS_ERR_BUSY : 0;
}
static SysResult_t safe(void *x, const MRGrant *g)
{
    (void)x;
    return f.dma || f.held[g->consumer] || (g->consumer == MR_SD && f.held[3]) ? SYS_ERR_BUSY : 0;
}
static SysResult_t role(void *x, const MRGrant *g, uint32_t *out)
{
    (void)x;
    if (g->consumer > 3)
        return SYS_ERR_NOT_SUPPORTED;
    *out = 1U << (g->consumer - 1);
    return 0;
}
static const MRSourceOps ops = {owner,    caps,  role,    valid,    obtain,
                                validate, quiet, release, released, safe};
static SysResult_t send(void *x, const MRReceipt *r)
{
    (void)x;
    f.sends++;
    event(7, &r->g, r->serial, f.queue ? SYS_ERR_NO_RESOURCE : 0);
    if (f.reenter)
        return MR_ConfirmConsumed(&c, r);
    return f.queue ? SYS_ERR_NO_RESOURCE : 0;
}
static SysResult_t ack(MRContext *x, const MRReceipt *r)
{
    SysResult_t z = MR_ConfirmConsumed(x, r);
    event(8, &r->g, r->serial, z);
    return z;
}
static SysResult_t retire(MRContext *x, const MRReceipt *r)
{
    SysResult_t z = MR_Retire(x, r);
    event(9, &r->g, r->serial, z);
    return z;
}
static SysResult_t get(MRContext *x, const MRRequest *r, MRMedia *m, MRView *v)
{
    SysResult_t z = MR_Obtain(x, r, m, v);
    event(3, &r->g, r->h.sequence, z);
    return z;
}
static SysResult_t give(MRContext *x, const MRRequest *r, const MRMedia *m)
{
    SysResult_t z = MR_Return(x, r, m);
    event(4, &r->g, r->h.sequence, z);
    return z;
}
static SysResult_t admit(MRContext *x, const MRRequest *r)
{
    SysResult_t z = MR_Admit(x, r);
    event(2, &r->g, r->h.sequence, z);
    return z;
}
static SysResult_t commit(MRContext *x, const MRRequest *r)
{
    SysResult_t z = MR_Commit(x, r);
    event(1, &r->g, r->h.sequence, z);
    return z;
}
static SysResult_t close(MRContext *x, const MRRelease *r)
{
    SysResult_t z = MR_Close(x, r);
    event(5, &r->grant, r->request, z);
    return z;
}
static SysResult_t verify(MRContext *x, const MRRelease *r)
{
    SysResult_t z = MR_VerifyExit(x, r);
    event(10, &r->grant, r->request, z);
    return z;
}
#define MR_ConfirmConsumed ack
#define MR_Retire retire
#define MR_Obtain get
#define MR_Return give
#define MR_Admit admit
#define MR_Commit commit
#define MR_Close close
#define MR_VerifyExit verify
static int init(uint32_t raw)
{
    memset(&c, 0, sizeof(c));
    memset(&f, 0, sizeof(f));
    f.owner = 1;
    f.raw = raw;
    f.grant_generation = 1;
    source.ops = &ops;
    source.context = 0;
    return MR_Init(&c, &source) || MR_AssociateGrant(&c, &usb) || MR_AssociateGrant(&c, &sd);
}
static int start(const MRGrant *g, MRRequest *r, MRMedia *m)
{
    if (g->consumer == MR_USB) { bridge.command++; bridge.lease++; }
    if (g->consumer == MR_SD) slot.generation++;
    return MR_Stage(&c, g, &bridge, &slot, r) || MR_Commit(&c, r) || MR_Admit(&c, r) ||
           MR_Obtain(&c, r, m, &view);
}
static int finish(MRRequest *r, MRMedia *m, MRReceipt *receipt)
{
    return MR_Return(&c, r, m) || MR_ReadReceipt(&c, r, receipt) ||
           MR_FlushReceipt(&c, receipt, send, 0) || MR_ConfirmConsumed(&c, receipt) ||
           MR_Retire(&c, receipt);
}
int MR_Run(uint32_t id)
{
    uint32_t n;
    MRRequest wrong;
    MRMedia badm;
    MRReceipt badt;
    MRGrant badg;
    MRSourceCaps cp;
    MRRelease badr;
    SysResult_t z;
    g_failure = 0;
    g_trace_count = 0;
    CHECK(!init(id == 1 || id == 2));
    if (id == 0)
    {
        g_sizes[0] = sizeof(c);
        g_sizes[1] = sizeof(MRRequest);
        g_sizes[2] = sizeof(MRReceipt);
        g_sizes[3] = sizeof(MRRecord);
        g_sizes[4] = sizeof(MRReceiptRecord);
        g_sizes[5] = sizeof(MRAdmission);
        g_sizes[6] = sizeof(MRSourceOps);
        g_sizes[7] = sizeof(MRView);
        CHECK(MRSource_Query(&source, &cp) == 0 && cp.backend == 2);
        return 0;
    }
    if (id == 1)
    {
        CHECK(!start(&usb, &u, &um));
        CHECK(!view.data2 && view.bytes2 == 0);
        CHECK(!finish(&u, &um, &t) && f.releases == 1);
        return 0;
    }
    if (id == 2)
    {
        CHECK(!start(&usb, &u, &um));
        CHECK(MR_Stage(&c, &sd, &bridge, &slot, &s) == 0);
        CHECK(!MR_Commit(&c, &s) && !MR_Admit(&c, &s));
        CHECK(MR_Obtain(&c, &s, &sm, &view) == SYS_ERR_BUSY && f.gets == 1);
        return 0;
    }
    if (id == 3 || id == 4)
    {
        CHECK(!start(&usb, &u, &um) && !start(&sd, &s, &sm));
        CHECK(MRMedia_Equal(&um, &sm));
        CHECK(view.bytes2 == 16);
        CHECK(!finish(id == 3 ? &u : &s, id == 3 ? &um : &sm, &t));
        CHECK(f.held[id == 3 ? 2 : 1] == 1);
        CHECK(!finish(id == 3 ? &s : &u, id == 3 ? &sm : &um, &st));
        CHECK(f.releases == 2);
        return 0;
    }
    if (id == 5)
    {
        CHECK(!start(&sd, &s, &sm) && f.held[3]);
        CHECK(!MR_Close(&c, &sr) && !finish(&s, &sm, &t));
        CHECK(MR_VerifyExit(&c, &sr) == SYS_ERR_BUSY);
        f.held[3] = 0; /* controlled backend internal-use completion, not a qualification */
        CHECK(!MR_VerifyExit(&c, &sr));
        return 0;
    }
    if (id == 37)
    {
        CHECK(!start(&usb, &u, &um));
        CHECK(!MR_Return(&c, &u, &um) && !MR_ReadReceipt(&c, &u, &t));
        CHECK(!MR_FlushReceipt(&c, &t, send, 0));
        for (n = 0; n < 100; n++)
        {
            CHECK(!start(&sd, &s, &sm) && !finish(&s, &sm, &st));
        }
        CHECK(!MR_ConfirmConsumed(&c, &t) && !MR_Retire(&c, &t));
        CHECK(f.releases == 101);
        return 0;
    }
    if (id == 38)
    {
        CHECK(!start(&usb, &u, &um));
        f.partial = 1;
        CHECK(MR_Return(&c, &u, &um) == SYS_ERR_IO && !f.held[1]);
        f.partial = 0;
        CHECK(MR_Return(&c, &u, &um) == SYS_ERR_IO && f.releases == 1);
        CHECK(!MR_ReadReceipt(&c, &u, &t) && t.result == SYS_ERR_IO && t.physical_returned == 1);
        return 0;
    }
    if (id == 39)
    {
        uint32_t number = UINT32_MAX - 1;
        CHECK(!MRNumber_Advance(&number) && number == UINT32_MAX);
        CHECK(MRNumber_Advance(&number) == SYS_ERR_NO_RESOURCE && number == UINT32_MAX);
        CHECK(MRNumber_Advance(0) == SYS_ERR_INVALID_ARG);
        return 0;
    }
    if (id == 40)
    {
        CHECK(!start(&usb, &u, &um) && !MR_Close(&c, &ur) && !finish(&u, &um, &t));
        badg = usb;
        badg.generation = 2;
        badg.acquire = 21;
        f.grant_generation = 2;
        CHECK(!MR_AssociateGrant(&c, &badg));
        CHECK(MR_Close(&c, &ur) != 0);
        CHECK(!start(&badg, &s, &sm));
        CHECK(MR_Return(&c, &u, &um) != 0 && f.held[1]);
        CHECK(sm.generation != um.generation);
        return 0;
    }
    if (id == 41)
    {
        CHECK(!MR_Stage(&c, &usb, &bridge, &slot, &u) && !MR_Commit(&c, &u));
        f.deny = 1;
        CHECK(!MR_Admit(&c, &u));
        CHECK(!MR_ReadReceipt(&c, &u, &t) && t.result != 0 && f.gets == 0);
        return 0;
    }
    if (id == 42)
    {
        CHECK(!MR_Stage(&c, &usb, &bridge, &slot, &u));
        wrong = u;
        CHECK(MR_Admit(&c, &u) != 0);
        CHECK(!memcmp(&wrong, &u, sizeof(u)));
        CHECK(!MR_Commit(&c, &u) && !MR_Admit(&c, &u) && !MR_Obtain(&c, &u, &um, &view));
        return 0;
    }
    if (id == 43)
    {
        f.io = 1;
        CHECK(start(&usb, &u, &um) != 0 && f.held[1]);
        CHECK(!MR_Close(&c, &ur) && MR_VerifyExit(&c, &ur) == SYS_ERR_BUSY);
        CHECK(MR_ReadReceipt(&c, &u, &t) == SYS_ERR_BUSY && f.gets == 1 && f.releases == 0);
        CHECK(MR_Obtain(&c, &u, &um, &view) == SYS_ERR_IO && f.gets == 1);
        CHECK(MR_Admit(&c, &u) == SYS_ERR_IO && f.gets == 1);
        return 0;
    }
    if (id == 44)
    {
        badg = usb;
        badg.consumer = 333;
        CHECK(MR_AssociateGrant(&c, &badg) == SYS_ERR_NOT_SUPPORTED && f.gets == 0);
        return 0;
    }
    if (id == 45)
    {
        f.bad = 7;
        CHECK(MRSource_Query(&source, &cp) == SYS_OK && cp.concurrent == 7);
        return 0;
    }
    if (id == 46)
    {
        CHECK(!start(&usb, &u, &um) && !start(&sd, &s, &sm));
        CHECK(!MR_Close(&c, &sr) && !finish(&s, &sm, &st));
        CHECK(!MR_VerifyExit(&c, &sr) && f.held[1]);
        CHECK(!MR_Close(&c, &ur) && !finish(&u, &um, &t));
        CHECK(!MR_VerifyExit(&c, &ur));
        return 0;
    }
    if (id == 47)
    {
        CHECK(!start(&usb, &u, &um));
        f.dma = 3;
        CHECK(MR_Return(&c, &u, &um) == SYS_ERR_BUSY && f.releases == 0);
        f.dma = 2;
        CHECK(MR_Return(&c, &u, &um) == SYS_ERR_BUSY && f.releases == 0);
        f.dma = 0;
        CHECK(!finish(&u, &um, &t) && f.releases == 1);
        return 0;
    }
    if (id == 48)
    {
        CHECK(!start(&usb, &u, &um));
        f.abort = 1;
        CHECK(MR_Return(&c, &u, &um) == SYS_ERR_IO && f.releases == 0);
        CHECK(MR_ReadReceipt(&c, &u, &t) == SYS_ERR_BUSY);
        f.abort = 0;
        CHECK(!finish(&u, &um, &t));
        return 0;
    }
    if (id == 49)
    {
        CHECK(MR_AssociateGrant(&c, &internal) == SYS_ERR_NOT_SUPPORTED && f.gets == 0);
        return 0;
    }
    if (id == 50)
    {
        f.noframe = 1;
        CHECK(start(&usb, &u, &um) != 0);
        CHECK(!MR_ReadReceipt(&c, &u, &t) && t.result == SYS_ERR_NOT_FOUND && !t.m.id && !t.physical_returned);
        CHECK(!MR_FlushReceipt(&c, &t, send, 0) && !MR_ConfirmConsumed(&c, &t));
        CHECK(!MR_Retire(&c, &t) && f.releases == 0);
        return 0;
    }
    if (id == 51)
    {
        f.uncertain = 1;
        CHECK(start(&usb, &u, &um) != 0 && f.held[1]);
        CHECK(MR_ReadReceipt(&c, &u, &t) == SYS_ERR_BUSY);
        CHECK(!MR_Close(&c, &ur) && MR_VerifyExit(&c, &ur) == SYS_ERR_BUSY);
        CHECK(MR_Obtain(&c, &u, &um, &view) == SYS_ERR_IO && f.gets == 1);
        return 0;
    }
    if (id >= 52 && id <= 55)
    {
        MRBridge actual = {2, 99, 4};
        MRRequest resolved;
        MRMedia native;
        bridge.lease = 0;
        CHECK(!MR_Stage(&c, &usb, &bridge, &slot, &u));
        CHECK(!MR_Commit(&c, &u) && !MR_Admit(&c, &u));
        CHECK(MR_BindBridge(&c, &u, &actual) == SYS_ERR_INVALID_STATE);
        CHECK(!MR_Obtain(&c, &u, &um, &view) && f.gets == 1);
        if (id == 52)
        {
            CHECK(!MR_BindBridge(&c, &u, &actual));
            CHECK(!MR_LookupBridge(&c, &actual, &resolved, &native));
            CHECK(!memcmp(&u, &resolved, sizeof(u)) && MRMedia_Equal(&um, &native));
            CHECK(!finish(&resolved, &native, &t) && t.bridge.lease == 99);
            CHECK(MR_LookupBridge(&c, &actual, &resolved, &native) == SYS_ERR_NOT_FOUND);
        }
        if (id == 53)
        {
            actual.command++;
            CHECK(MR_BindBridge(&c, &u, &actual) == SYS_ERR_INVALID_ARG);
            CHECK(f.held[1] && f.gets == 1);
        }
        if (id == 54)
        {
            CHECK(!MR_BindBridge(&c, &u, &actual) && !MR_BindBridge(&c, &u, &actual));
            actual.lease++;
            CHECK(MR_BindBridge(&c, &u, &actual) == SYS_ERR_PROTOCOL);
            CHECK(f.gets == 1 && f.releases == 0);
        }
        if (id == 55)
        {
            CHECK(!MR_Close(&c, &ur));
            CHECK(!finish(&u, &um, &t) && t.physical_returned && !t.bridge.lease);
            CHECK(!MR_VerifyExit(&c, &ur));
        }
        return 0;
    }
    if (id == 6)
    {
        CHECK(!MR_Stage(&c, &usb, &bridge, &slot, &u));
        CHECK(!MR_CancelLocal(&c, &u));
        CHECK(MR_Admit(&c, &u) != 0);
        CHECK(!MR_Stage(&c, &usb, &bridge, &slot, &s) && s.h.sequence > u.h.sequence);
        return 0;
    }
    if (id == 7)
    {
        CHECK(!MR_Stage(&c, &usb, &bridge, &slot, &u) && !MR_Commit(&c, &u));
        CHECK(MR_CancelLocal(&c, &u) != 0);
        CHECK(!MR_Close(&c, &ur) && !MR_Admit(&c, &u));
        CHECK(MR_Obtain(&c, &u, &um, &view) != 0 && f.gets == 0);
        CHECK(!MR_ReadReceipt(&c, &u, &t) && t.result != 0);
        return 0;
    }
    if (id == 8)
    {
        CHECK(!MR_Close(&c, &ur));
        CHECK(MR_Stage(&c, &usb, &bridge, &slot, &u) != 0);
        CHECK(!MR_VerifyExit(&c, &ur));
        CHECK(MR_AssociateGrant(&c, &usb) != 0);
        return 0;
    }
    if (id == 9)
    {
        CHECK(!MR_Stage(&c, &usb, &bridge, &slot, &u) && !MR_Commit(&c, &u) && !MR_Admit(&c, &u));
        CHECK(!MR_Close(&c, &ur));
        CHECK(MR_Obtain(&c, &u, &um, &view) != 0 && f.gets == 0);
        return 0;
    }
    if (id == 10)
    {
        f.bad = 8;
        CHECK(MRSource_Query(&source, &cp) == SYS_ERR_NOT_SUPPORTED);
        return 0;
    }
    if (id == 11)
    {
        f.owner = 0;
        CHECK(MR_Stage(&c, &usb, &bridge, &slot, &u) == SYS_ERR_INVALID_STATE);
        return 0;
    }
    if (id == 12)
    {
        CHECK(MR_Init(&c, &source) != 0);
        return 0;
    }
    CHECK(!start(&usb, &u, &um));
    switch (id)
    {
    case 13:
        CHECK(!MR_Admit(&c, &u));
        CHECK(!MR_Obtain(&c, &u, &sm, &view) && f.gets == 1);
        break;
    case 14:
        wrong = u;
        wrong.g.consumer = 2;
        CHECK(MR_Return(&c, &wrong, &um) != 0 && f.releases == 0);
        break;
    case 15:
        badm = um;
        badm.id++;
        CHECK(MR_Return(&c, &u, &badm) != 0 && f.releases == 0);
        break;
    case 16:
        wrong = u;
        wrong.g.generation++;
        CHECK(MR_Return(&c, &wrong, &um) != 0);
        break;
    case 17:
        f.dma = 1;
        CHECK(MR_Return(&c, &u, &um) == SYS_ERR_BUSY && f.releases == 0 && f.held[1]);
        f.dma = 0;
        CHECK(!finish(&u, &um, &t));
        break;
    case 18:
        f.fail = 1;
        CHECK(MR_Return(&c, &u, &um) == SYS_ERR_IO && f.held[1]);
        f.fail = 0;
        CHECK(MR_Return(&c, &u, &um) == SYS_ERR_IO && !f.held[1]);
        CHECK(!MR_ReadReceipt(&c, &u, &t) && t.result == SYS_ERR_IO && t.physical_returned == 1);
        CHECK(MR_Return(&c, &u, &um) == SYS_ERR_IO && f.releases == 2);
        break;
    case 19:
        f.pending = 1;
        CHECK(MR_Return(&c, &u, &um) == SYS_ERR_BUSY && !f.held[1]);
        CHECK(MR_Return(&c, &u, &um) == SYS_ERR_BUSY && f.releases == 1);
        f.pending = 0;
        CHECK(!MR_Return(&c, &u, &um) && f.releases == 1);
        break;
    case 20:
        CHECK(!MR_Return(&c, &u, &um));
        CHECK(!MR_Return(&c, &u, &um) && f.releases == 1);
        break;
    case 21:
        CHECK(!MR_Return(&c, &u, &um) && !MR_ReadReceipt(&c, &u, &t));
        CHECK(MR_ConfirmConsumed(&c, &t) != 0);
        CHECK(MR_Retire(&c, &t) == SYS_ERR_BUSY);
        break;
    case 22:
        CHECK(!MR_Return(&c, &u, &um) && !MR_ReadReceipt(&c, &u, &t));
        f.queue = 1;
        CHECK(MR_FlushReceipt(&c, &t, send, 0) == SYS_ERR_NO_RESOURCE);
        CHECK(MR_ConfirmConsumed(&c, &t) != 0);
        f.queue = 0;
        CHECK(!MR_FlushReceipt(&c, &t, send, 0) && f.sends == 2);
        break;
    case 23:
        CHECK(!MR_Return(&c, &u, &um) && !MR_ReadReceipt(&c, &u, &t));
        CHECK(!MR_FlushReceipt(&c, &t, send, 0) && !MR_FlushReceipt(&c, &t, send, 0) &&
              f.sends == 1);
        CHECK(MR_Retire(&c, &t) == SYS_ERR_BUSY);
        break;
    case 24:
        CHECK(!MR_Return(&c, &u, &um) && !MR_ReadReceipt(&c, &u, &t));
        CHECK(!MR_FlushReceipt(&c, &t, send, 0));
        badt = t;
        badt.m.generation++;
        CHECK(MR_ConfirmConsumed(&c, &badt) != 0);
        CHECK(!MR_ConfirmConsumed(&c, &t) && !MR_ConfirmConsumed(&c, &t));
        break;
    case 25:
        CHECK(!finish(&u, &um, &t));
        CHECK(MR_Return(&c, &u, &um) != 0 && f.releases == 1);
        CHECK(!start(&usb, &s, &sm));
        CHECK(MR_ConfirmConsumed(&c, &t) != 0 && f.held[1]);
        break;
    case 26:
        CHECK(!MR_Close(&c, &ur));
        CHECK(MR_VerifyExit(&c, &ur) == SYS_ERR_BUSY);
        CHECK(!MR_Return(&c, &u, &um) && !MR_ReadReceipt(&c, &u, &t));
        CHECK(MR_VerifyExit(&c, &ur) == SYS_ERR_BUSY);
        CHECK(!MR_FlushReceipt(&c, &t, send, 0) && !MR_ConfirmConsumed(&c, &t) &&
              !MR_Retire(&c, &t));
        CHECK(!MR_VerifyExit(&c, &ur));
        break;
    case 27:
        CHECK(!start(&sd, &s, &sm) && !MR_Close(&c, &ur));
        CHECK(!finish(&u, &um, &t));
        CHECK(!MR_VerifyExit(&c, &ur) && f.held[2]);
        CHECK(!finish(&s, &sm, &st));
        break;
    case 28:
        CHECK(!MR_Close(&c, &ur) && !MR_Close(&c, &ur));
        badr = ur;
        badr.request++;
        CHECK(MR_Close(&c, &badr) == SYS_ERR_BUSY);
        CHECK(MR_VerifyExit(&c, &badr) != 0);
        break;
    case 29:
        CHECK(!MR_Return(&c, &u, &um) && !MR_ReadReceipt(&c, &u, &t));
        f.reenter = 1;
        CHECK(MR_FlushReceipt(&c, &t, send, 0) == SYS_ERR_BUSY);
        f.reenter = 0;
        CHECK(!MR_FlushReceipt(&c, &t, send, 0));
        break;
    case 30:
        for (n = 0; n < MR_HANDOFF_MAX - 1; n++)
        {
            CHECK(!MR_Return(&c, &u, &um));
            CHECK(!start(&usb, &u, &um));
        }
        CHECK(!MR_Return(&c, &u, &um));
        bridge.command++; bridge.lease++;
        CHECK(MR_Stage(&c, &usb, &bridge, &slot, &u) == SYS_ERR_NO_RESOURCE);
        break;
    case 31:
        CHECK(!MR_Close(&c, &ur) && !finish(&u, &um, &t));
        f.dma = 1;
        CHECK(MR_VerifyExit(&c, &ur) == SYS_ERR_BUSY);
        break;
    case 32:
        for (n = 0; n < 100; n++)
        {
            CHECK(!finish(&u, &um, &t));
            CHECK(!start(&usb, &u, &um));
        }
        CHECK(f.gets == 101 && f.releases == 100);
        break;
    case 33:
        CHECK(!MR_Close(&c, &ur));
        badg = usb;
        badg.generation = 2;
        CHECK(MR_AssociateGrant(&c, &badg) == SYS_ERR_BUSY);
        break;
    case 34:
        wrong = u;
        wrong.b.command++;
        CHECK(MR_Return(&c, &wrong, &um) != 0);
        break;
    case 35:
        CHECK(!start(&sd, &s, &sm));
        wrong = s;
        wrong.s.generation++;
        CHECK(MR_Return(&c, &wrong, &sm) != 0 && f.held[2]);
        break;
    case 36:
        CHECK(MR_ReadReceipt(&c, &u, &t) == SYS_ERR_BUSY);
        CHECK(f.releases == 0);
        break;
    default:
        CHECK(0);
    }
    z = 0;
    return z;
}
