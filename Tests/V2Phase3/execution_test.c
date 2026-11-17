#include "../../CM7/ApplicationV2/Resource/media_resource.h"
#include "../../CM7/ApplicationV2/Supervisor/execution_manager.h"
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
volatile uint32_t g_failure, g_trace_count, g_sizes[8];
uint32_t g_trace[1024][8];
static uint32_t who, now;
static void trace(uint32_t op, const LCRequest *r, SysResult_t z, uint32_t fact)
{
    uint32_t *t;
    if (g_trace_count >= 1024)
        return;
    t = g_trace[g_trace_count];
    t[0] = op;
    t[1] = r->id.module;
    t[2] = r->id.request;
    t[3] = r->id.operation;
    t[4] = r->expected;
    t[5] = fact;
    t[6] = (uint32_t)z;
    t[7] = now;
    g_trace_count++;
}
typedef struct
{
    LCContext lc;
    LCRequest queued;
    uint32_t queued_valid, begin_count, begin_op[6], accepted_at, delay;
    uint32_t fail_op, fail_fact;
    SysResult_t failure;
    SysResult_t guard, precondition;
    uint32_t submit_full, consume_full;
    uint32_t accepted_count, complete_count, consume_count, reenter, read_wrong, consume_error;
} Mock;
static Mock m[3];
static EMContext em;
static EMPlan plan;
static V2CompositionCandidate candidate;
static V2ModuleDesc desc[3];
static V2Registry registry;
static V2ResolverScratch scratch;
static V2Issue issue;
static V2Provided provided[3];
static V2Requirement require[2];
static uint32_t snapshot[16];
static int lc_owner(void *p)
{
    Mock *a = p;
    return who == a->lc.module || (!a->lc.magic && who > 0 && who <= 3);
}
static int supervisor_owner(void *p)
{
    (void)p;
    return who == 100;
}
static SysResult_t can_begin(void *p, const LCRequest *r)
{
    Mock *a = p;
    trace(10, r, a->precondition, 0);
    return a->precondition;
}
static SysResult_t begin(void *p, const LCRequest *r)
{
    Mock *a = p;
    a->begin_count++;
    a->begin_op[r->id.operation]++;
    a->accepted_at = now;
    trace(11, r, 0, 0);
    if (a->reenter)
    {
        uint32_t s;
        SysResult_t e;
        if (LC_Status(&a->lc, &s, &e) != SYS_ERR_BUSY)
            return SYS_ERR_IO;
    }
    return SYS_OK;
}
static SysResult_t prove(void *p, const LCRequest *r, uint32_t f)
{
    Mock *a = p;
    trace(12, r, a->guard, f);
    return a->guard;
}
static const struct V2LifecycleOps ops = {lc_owner, can_begin, begin, prove};
static SysResult_t submit(void *p, const LCRequest *r)
{
    Mock *a = p;
    trace(1, r, 0, 0);
    if (a->submit_full)
        return SYS_ERR_NO_RESOURCE;
    if (a->queued_valid)
        return !memcmp(&a->queued, r, sizeof(*r)) ? SYS_OK : SYS_ERR_BUSY;
    a->queued = *r;
    a->queued_valid = 1;
    a->accepted_count++;
    return SYS_OK;
}
static SysResult_t read_result(void *p, const V2ExecId *id, LCCompletion *v)
{
    Mock *a = p;
    uint32_t old = who;
    SysResult_t z;
    who = a->lc.module;
    z = LC_Read(&a->lc, id, v);
    who = old;
    if (!z && a->read_wrong == 1)
        v->id.request++;
    if (!z && a->read_wrong == 2)
        v->fact = LC_ACTIVE;
    return z;
}
static SysResult_t consume(void *p, const LCCompletion *v)
{
    Mock *a = p;
    uint32_t old = who;
    SysResult_t z;
    if (a->consume_full)
        return SYS_ERR_NO_RESOURCE;
    if (a->consume_error)
        return SYS_ERR_IO;
    who = a->lc.module;
    z = LC_Confirm(&a->lc, v);
    who = old;
    if (!z)
        a->consume_count++;
    return z;
}
static void pump(void)
{
    uint32_t i;
    for (i = 0; i < 3; i++)
    {
        Mock *a = &m[i];
        who = i + 1;
        if (a->queued_valid)
        {
            SysResult_t z = LC_Request(&a->lc, &a->queued, now);
            trace(2, &a->queued, z, 0);
            if (!z)
                a->queued_valid = 0;
        }
        LC_Process(&a->lc, now);
        if (a->lc.pending && a->lc.dispatched && !a->lc.decided && now - a->accepted_at >= a->delay)
        {
            uint32_t f = LC_Target(a->lc.request.id.operation);
            SysResult_t z = SYS_OK;
            if (a->fail_op == a->lc.request.id.operation)
            {
                z = a->failure;
                f = a->fail_fact;
                a->fail_op = 0;
            }
            z = LC_Complete(&a->lc, &a->lc.request.id, z, f);
            trace(13, &a->lc.request, z, f);
            a->complete_count++;
        }
        LC_Process(&a->lc, now);
    }
    who = 100;
}
static void rounds(uint32_t n)
{
    while (n--)
    {
        who = 100;
        EM_Process(&em, now);
        pump();
        now++;
    }
}
static SysResult_t setup(void)
{
    uint32_t i;
    V2ModuleId root[2] = {{2}, {3}};
    EMEndpoint e[3];
    memset(m, 0, sizeof(m));
    memset(&em, 0, sizeof(em));
    memset(&candidate, 0, sizeof(candidate));
    memset(desc, 0, sizeof(desc));
    memset(snapshot, 0, sizeof(snapshot));
    memset(&scratch, 0, sizeof(scratch));
    now = 0;
    for (i = 0; i < 3; i++)
    {
        who = i + 1;
        m[i].delay = 1;
        if (LC_Init(&m[i].lc, i + 1, 7, &ops, &m[i]))
            return SYS_ERR_IO;
        provided[i].id.value = i + 11;
        provided[i].major = 1;
        provided[i].minor = 0;
        desc[i].id.value = i + 1;
        desc[i].major = 1;
        desc[i].provided = &provided[i];
        desc[i].provided_count = 1;
        desc[i].lifecycle_ops = &ops;
        snapshot[i] = LC_UNCONFIGURED;
        e[i].module = i + 1;
        e[i].port = &m[i];
        e[i].submit = submit;
        e[i].read = read_result;
        e[i].consume = consume;
    }
    memset(require, 0, sizeof(require));
    for (i = 0; i < 2; i++)
    {
        require[i].id.value = 11;
        require[i].min_major = 1;
        require[i].max_major = 1;
        desc[i + 1].required = &require[i];
        desc[i + 1].required_count = 1;
    }
    registry.modules = desc;
    registry.count = 3;
    registry.reserved = 0;
    candidate.candidate_only = 1;
    if (V2Resolver_Resolve(&registry, root, 2, &scratch, &candidate.modules, &issue))
        return SYS_ERR_IO;
    who = 100;
    return EM_Init(&em, 7, e, 3, supervisor_owner, NULL);
}
static SysResult_t build(uint32_t kind, uint16_t mask, uint32_t cleanup)
{
    return EM_BuildPlan(&registry, &candidate, snapshot, mask, kind, cleanup, &plan);
}
static int start_all(void)
{
    who = 100;
    if (build(EM_START, 0, 0) || EM_Load(&em, &plan, 1000))
        return 0;
    rounds(100);
    return em.state == EM_DONE && !em.first_error;
}
static LCRequest request(uint32_t seq, uint32_t op, uint32_t expected)
{
    LCRequest r;
    memset(&r, 0, sizeof(r));
    r.id.session = 7;
    r.id.module = 1;
    r.id.request = seq;
    r.id.operation = op;
    r.expected = expected;
    r.timeout = 10;
    return r;
}
static void direct_done(LCRequest *r, SysResult_t result, uint32_t fact)
{
    who = 1;
    LC_Request(&m[0].lc, r, now);
    LC_Process(&m[0].lc, now);
    LC_Complete(&m[0].lc, &r->id, result, fact);
    LC_Process(&m[0].lc, now);
}
/* Controlled real Resource guard integration, with no physical frame pool. */
static MRContext resource;
static uint32_t resource_safe;
static MRGrant grant = {1, 1, 1, 1};
static MRRelease release_id = {{1, 1, 1, 1}, 2};
static bool ro(void *p)
{
    (void)p;
    return who == 1;
}
static SysResult_t caps(void *p, MRSourceCaps *c)
{
    (void)p;
    c->backend = MR_RAW;
    c->owners = MR_CAP_USB;
    c->concurrent = 1;
    c->spans = 1;
    return 0;
}
static SysResult_t role(void *p, const MRGrant *g, uint32_t *v)
{
    (void)p;
    (void)g;
    *v = MR_CAP_USB;
    return 0;
}
static SysResult_t gv(void *p, const MRGrant *g)
{
    (void)p;
    return MRGrant_Equal(g, &grant) ? 0 : SYS_ERR_PROTOCOL;
}
static SysResult_t get(void *p, const MRGrant *g, const MRHandoff *h, MRMedia *v, MRView *w)
{
    (void)p;
    (void)g;
    (void)h;
    (void)v;
    (void)w;
    return SYS_ERR_NOT_FOUND;
}
static SysResult_t valid(void *p, const MRGrant *g, const MRMedia *v, const MRView *w)
{
    (void)p;
    (void)g;
    (void)v;
    (void)w;
    return 0;
}
static SysResult_t two(void *p, const MRGrant *g, const MRMedia *v)
{
    (void)p;
    (void)g;
    (void)v;
    return 0;
}
static SysResult_t safe(void *p, const MRGrant *g)
{
    (void)p;
    (void)g;
    return resource_safe ? 0 : SYS_ERR_BUSY;
}
static const MRSourceOps source_ops = {ro, caps, role, gv, get, valid, two, two, two, safe};
static SysResult_t real_guard(void *p, const LCRequest *r)
{
    (void)p;
    (void)r;
    return MR_VerifyExit(&resource, &release_id);
}
static const struct V2LifecycleOps resource_ops = {lc_owner, real_guard, begin, prove};
static SysResult_t send_receipt(void *p, const MRReceipt *r)
{
    (void)p;
    (void)r;
    return 0;
}
int EM_Run(uint32_t id)
{
    LCRequest r, r2;
    LCCompletion v, v2;
    uint32_t i, s;
    SysResult_t z, e;
    g_failure = 0;
    g_trace_count = 0;
    CHECK(setup() == SYS_OK);
    who = 1;
    switch (id)
    {
    case 0:
        g_sizes[0] = sizeof(LCContext);
        g_sizes[1] = sizeof(EMContext);
        g_sizes[2] = sizeof(EMPlan);
        g_sizes[3] = sizeof(LCCompletion);
        g_sizes[4] = sizeof(LCRequest);
        g_sizes[5] = sizeof(struct V2LifecycleOps);
        g_sizes[6] = sizeof(V2ExecId);
        g_sizes[7] = sizeof(EMEndpoint);
        CHECK(sizeof(V2ExecId) == 16);
        break;
    case 1:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        CHECK(LC_Request(&m[0].lc, &r, 0) == 0);
        CHECK(m[0].lc.state == 17);
        CHECK(!m[0].begin_count);
        CHECK(LC_Read(&m[0].lc, &r.id, &v) == SYS_ERR_NOT_FOUND);
        break;
    case 2:
        r = request(1, LC_ACTIVATE, LC_UNCONFIGURED);
        CHECK(LC_Request(&m[0].lc, &r, 0) == SYS_ERR_INVALID_STATE);
        break;
    case 3:
        r = request(1, LC_CONFIGURE, LC_ACTIVE);
        CHECK(LC_Request(&m[0].lc, &r, 0) == SYS_ERR_INVALID_STATE);
        break;
    case 4:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        CHECK(LC_Request(&m[0].lc, &r, 0) == 0);
        CHECK(LC_Request(&m[0].lc, &r, 1) == 0);
        LC_Process(&m[0].lc, 1);
        LC_Process(&m[0].lc, 2);
        CHECK(m[0].begin_count == 1);
        break;
    case 5:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        CHECK(LC_Request(&m[0].lc, &r, 0) == 0);
        r.timeout++;
        CHECK(LC_Request(&m[0].lc, &r, 0) == SYS_ERR_PROTOCOL);
        break;
    case 6:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        CHECK(LC_Request(&m[0].lc, &r, 0) == 0);
        r.id.request++;
        CHECK(LC_Request(&m[0].lc, &r, 0) == SYS_ERR_BUSY);
        break;
    case 7:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        r.id.session++;
        CHECK(LC_Request(&m[0].lc, &r, 0) == SYS_ERR_PROTOCOL);
        break;
    case 8:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        r.id.module++;
        CHECK(LC_Request(&m[0].lc, &r, 0) == SYS_ERR_PROTOCOL);
        break;
    case 9:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        CHECK(LC_Request(&m[0].lc, &r, 0) == 0);
        CHECK(LC_Complete(&m[0].lc, &r.id, 0, LC_INACTIVE) == SYS_ERR_INVALID_STATE);
        break;
    case 10:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        LC_Request(&m[0].lc, &r, 0);
        LC_Process(&m[0].lc, 0);
        r.id.request++;
        CHECK(LC_Complete(&m[0].lc, &r.id, 0, LC_INACTIVE) == SYS_ERR_PROTOCOL);
        CHECK(!m[0].lc.decided);
        break;
    case 11:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        LC_Request(&m[0].lc, &r, 0);
        LC_Process(&m[0].lc, 0);
        CHECK(LC_Complete(&m[0].lc, &r.id, 0, LC_ACTIVE) == SYS_ERR_PROTOCOL);
        break;
    case 12:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        direct_done(&r, 0, LC_INACTIVE);
        CHECK(LC_Read(&m[0].lc, &r.id, &v) == 0);
        CHECK(v.result == 0 && v.fact == LC_INACTIVE);
        break;
    case 13:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        direct_done(&r, SYS_ERR_IO, LC_ERROR);
        CHECK(LC_Read(&m[0].lc, &r.id, &v) == 0);
        CHECK(v.result == SYS_ERR_IO && v.fact == LC_ERROR);
        break;
    case 14:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        m[0].guard = SYS_ERR_BUSY;
        direct_done(&r, 0, LC_INACTIVE);
        CHECK(!m[0].lc.terminal && m[0].begin_count == 1);
        LC_Process(&m[0].lc, 2);
        CHECK(m[0].begin_count == 1);
        m[0].guard = 0;
        LC_Process(&m[0].lc, 3);
        CHECK(m[0].lc.terminal);
        break;
    case 15:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        m[0].guard = SYS_ERR_IO;
        direct_done(&r, 0, LC_INACTIVE);
        CHECK(!m[0].lc.terminal);
        m[0].guard = 0;
        LC_Process(&m[0].lc, 2);
        CHECK(LC_Read(&m[0].lc, &r.id, &v) == 0);
        CHECK(v.result == SYS_ERR_IO && v.actual_result == 0);
        break;
    case 16:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        LC_Request(&m[0].lc, &r, 0);
        LC_Process(&m[0].lc, 0);
        LC_Process(&m[0].lc, 11);
        CHECK(m[0].lc.pending && !m[0].lc.terminal && m[0].begin_count == 1);
        CHECK(LC_Complete(&m[0].lc, &r.id, 0, LC_INACTIVE) == 0);
        LC_Process(&m[0].lc, 12);
        LC_Read(&m[0].lc, &r.id, &v);
        CHECK(v.result == SYS_ERR_TIMEOUT && v.actual_result == 0);
        break;
    case 17:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        m[0].precondition = SYS_ERR_BUSY;
        LC_Request(&m[0].lc, &r, 0);
        LC_Process(&m[0].lc, 0);
        LC_Process(&m[0].lc, 11);
        CHECK(!m[0].begin_count && m[0].lc.terminal);
        break;
    case 18:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        direct_done(&r, 0, LC_INACTIVE);
        LC_Read(&m[0].lc, &r.id, &v);
        v2 = v;
        v2.fact = LC_ACTIVE;
        CHECK(LC_Confirm(&m[0].lc, &v2) == SYS_ERR_PROTOCOL);
        CHECK(m[0].lc.pending);
        CHECK(LC_Confirm(&m[0].lc, &v) == 0);
        CHECK(LC_Confirm(&m[0].lc, &v) == 0);
        break;
    case 19:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        direct_done(&r, 0, LC_INACTIVE);
        LC_Read(&m[0].lc, &r.id, &v);
        LC_Confirm(&m[0].lc, &v);
        r2 = request(2, LC_ACTIVATE, LC_INACTIVE);
        LC_Request(&m[0].lc, &r2, 0);
        CHECK(LC_Complete(&m[0].lc, &r.id, 0, LC_INACTIVE) == SYS_ERR_PROTOCOL);
        break;
    case 20:
        CHECK(LC_Seal(&m[0].lc) == 0);
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        CHECK(LC_Request(&m[0].lc, &r, 0) == SYS_ERR_INVALID_STATE);
        break;
    case 21:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        m[0].reenter = 1;
        direct_done(&r, 0, LC_INACTIVE);
        CHECK(m[0].lc.terminal);
        break;
    case 22:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        who = 2;
        CHECK(LC_Request(&m[0].lc, &r, 0) == SYS_ERR_INVALID_STATE);
        break;
    case 23:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        direct_done(&r, SYS_ERR_IO, LC_ERROR);
        LC_Read(&m[0].lc, &r.id, &v);
        LC_Confirm(&m[0].lc, &v);
        r = request(2, LC_ERROR_PROCESS, LC_ERROR);
        m[0].precondition = SYS_ERR_BUSY;
        LC_Request(&m[0].lc, &r, 0);
        LC_Process(&m[0].lc, 0);
        CHECK(m[0].begin_count == 1);
        m[0].precondition = 0;
        LC_Process(&m[0].lc, 1);
        LC_Complete(&m[0].lc, &r.id, 0, LC_UNCONFIGURED);
        LC_Process(&m[0].lc, 1);
        CHECK(m[0].lc.state == LC_UNCONFIGURED);
        break;
    case 24:
        r = request(0xFFFFFFFFU, LC_CONFIGURE, LC_UNCONFIGURED);
        direct_done(&r, 0, LC_INACTIVE);
        LC_Read(&m[0].lc, &r.id, &v);
        LC_Confirm(&m[0].lc, &v);
        r = request(1, LC_ACTIVATE, LC_INACTIVE);
        CHECK(LC_Request(&m[0].lc, &r, 0) == SYS_ERR_PROTOCOL);
        break;
    case 25:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        r.timeout = 0;
        CHECK(LC_Request(&m[0].lc, &r, 0) == SYS_ERR_INVALID_ARG);
        break;
    case 26:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        LC_Request(&m[0].lc, &r, 0xFFFFFFFCU);
        LC_Process(&m[0].lc, 0xFFFFFFFCU);
        LC_Process(&m[0].lc, 7);
        CHECK(m[0].lc.first_error == SYS_ERR_TIMEOUT);
        break;
    case 27:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        direct_done(&r, 0, LC_INACTIVE);
        CHECK(LC_Complete(&m[0].lc, &r.id, 0, LC_INACTIVE) == 0);
        CHECK(LC_Complete(&m[0].lc, &r.id, SYS_ERR_IO, LC_ERROR) == SYS_ERR_PROTOCOL);
        break;
    case 28:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        direct_done(&r, 0, LC_INACTIVE);
        LC_Read(&m[0].lc, &r.id, &v);
        LC_Confirm(&m[0].lc, &v);
        LC_Seal(&m[0].lc);
        r = request(2, LC_CLEANUP, LC_INACTIVE);
        direct_done(&r, 0, LC_UNCONFIGURED);
        CHECK(m[0].lc.state == LC_UNCONFIGURED);
        break;
    case 29:
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        direct_done(&r, 0, LC_INACTIVE);
        CHECK(LC_Status(&m[0].lc, &s, &e) == 0 && s == LC_INACTIVE);
        break;
    default:
        break;
    }
    if (id < 30)
        return 0;
    who = 100;
    switch (id)
    {
    case 30:
        CHECK(build(EM_START, 0, 0) == 0 && plan.count == 6);
        CHECK(plan.steps[0].module == 1 && plan.steps[3].module == 1);
        CHECK(plan.steps[5].module == 3);
        break;
    case 31:
        CHECK(start_all());
        for (i = 0; i < 3; i++)
            CHECK(m[i].lc.state == LC_ACTIVE && m[i].begin_count == 2);
        break;
    case 32:
        CHECK(start_all());
        for (i = 0; i < 3; i++)
            snapshot[i] = LC_ACTIVE;
        CHECK(build(EM_STOP, 7, 1) == 0 && plan.count == 6);
        CHECK(plan.steps[0].module == 3 && plan.steps[1].module == 2 && plan.steps[2].module == 1);
        CHECK(EM_Load(&em, &plan, 1000) == 0);
        rounds(100);
        CHECK(em.state == EM_DONE);
        for (i = 0; i < 3; i++)
            CHECK(m[i].lc.state == LC_UNCONFIGURED);
        break;
    case 33:
        CHECK(start_all());
        for (i = 0; i < 3; i++)
            snapshot[i] = LC_ACTIVE;
        CHECK(build(EM_STOP, 2, 0) == 0 && plan.count == 1);
        CHECK(EM_Load(&em, &plan, 1000) == 0);
        rounds(30);
        CHECK(m[1].lc.state == LC_INACTIVE && m[0].lc.state == LC_ACTIVE &&
              m[2].lc.state == LC_ACTIVE);
        break;
    case 34:
        CHECK(start_all());
        for (i = 0; i < 3; i++)
            snapshot[i] = LC_ACTIVE;
        CHECK(build(EM_STOP, 1, 0) == SYS_ERR_BUSY);
        break;
    case 35:
        CHECK(start_all());
        for (i = 0; i < 3; i++)
            snapshot[i] = LC_ACTIVE;
        CHECK(build(EM_STOP, 6, 0) == 0);
        CHECK(EM_Load(&em, &plan, 1000) == 0);
        rounds(40);
        CHECK(m[0].lc.state == LC_ACTIVE && m[1].lc.state == LC_INACTIVE &&
              m[2].lc.state == LC_INACTIVE);
        break;
    case 36:
        m[2].fail_op = LC_ACTIVATE;
        m[2].failure = SYS_ERR_IO;
        m[2].fail_fact = LC_INACTIVE;
        CHECK(build(EM_START, 0, 0) == 0);
        CHECK(EM_Load(&em, &plan, 1000) == 0);
        rounds(150);
        CHECK(em.state == EM_DONE && em.first_error == SYS_ERR_IO);
        for (i = 0; i < 3; i++)
            CHECK(m[i].lc.state == LC_UNCONFIGURED);
        break;
    case 37:
        m[1].fail_op = LC_CONFIGURE;
        m[1].failure = SYS_ERR_IO;
        m[1].fail_fact = LC_ERROR;
        CHECK(build(EM_START, 0, 0) == 0);
        EM_Load(&em, &plan, 1000);
        rounds(50);
        CHECK(em.state == EM_BLOCKED && m[0].lc.state == LC_INACTIVE && m[1].lc.state == LC_ERROR);
        break;
    case 38:
        CHECK(start_all());
        for (i = 0; i < 3; i++)
            snapshot[i] = LC_ACTIVE;
        m[2].fail_op = LC_DEACTIVATE;
        m[2].failure = SYS_ERR_IO;
        m[2].fail_fact = LC_ERROR;
        build(EM_STOP, 7, 1);
        EM_Load(&em, &plan, 1000);
        rounds(40);
        CHECK(em.state == EM_BLOCKED && m[0].lc.state == LC_ACTIVE && m[1].lc.state == LC_ACTIVE);
        break;
    case 39:
        m[0].delay = 20;
        build(EM_START, 0, 0);
        EM_Load(&em, &plan, 5);
        rounds(10);
        CHECK(em.first_error == SYS_ERR_TIMEOUT && em.state == EM_RUNNING && em.stage == 2);
        CHECK(m[0].lc.pending && !m[0].lc.terminal);
        m[0].delay = 1;
        rounds(150);
        CHECK(em.first_error == SYS_ERR_TIMEOUT && em.state == EM_DONE);
        break;
    case 40:
        m[0].submit_full = 1;
        build(EM_START, 0, 0);
        EM_Load(&em, &plan, 1000);
        rounds(5);
        CHECK(em.issued == 1 && !m[0].begin_count && !m[0].accepted_count);
        m[0].submit_full = 0;
        rounds(100);
        CHECK(em.state == EM_DONE && m[0].accepted_count == 2);
        break;
    case 41:
        m[0].consume_full = 1;
        build(EM_START, 0, 0);
        EM_Load(&em, &plan, 1000);
        rounds(15);
        CHECK(em.stage == 3 && m[0].lc.pending && m[0].begin_count == 1);
        m[0].consume_full = 0;
        rounds(100);
        CHECK(em.state == EM_DONE);
        break;
    case 42:
        m[0].read_wrong = 1;
        build(EM_START, 0, 0);
        EM_Load(&em, &plan, 1000);
        rounds(20);
        CHECK(em.state == EM_BLOCKED && em.first_error == SYS_ERR_PROTOCOL && m[0].lc.pending);
        break;
    case 43:
        build(EM_START, 0, 0);
        EM_Load(&em, &plan, 1000);
        rounds(1);
        CHECK(EM_Load(&em, &plan, 1000) == SYS_ERR_BUSY);
        break;
    case 44:
        m[2].fail_op = LC_ACTIVATE;
        m[2].failure = SYS_ERR_IO;
        m[2].fail_fact = LC_INACTIVE;
        m[1].fail_op = LC_DEACTIVATE;
        m[1].failure = SYS_ERR_PROTOCOL;
        m[1].fail_fact = LC_ERROR;
        build(EM_START, 0, 0);
        EM_Load(&em, &plan, 1000);
        rounds(150);
        CHECK(em.state == EM_BLOCKED && em.first_error == SYS_ERR_IO &&
              em.recovery_error == SYS_ERR_PROTOCOL);
        CHECK(m[0].lc.state == LC_ACTIVE);
        break;
    case 45:
        m[0].guard = SYS_ERR_BUSY;
        build(EM_START, 0, 0);
        EM_Load(&em, &plan, 1000);
        rounds(20);
        CHECK(em.stage == 2 && !m[0].lc.terminal && m[0].begin_count == 1);
        m[0].guard = 0;
        rounds(100);
        CHECK(em.state == EM_DONE);
        break;
    case 46:
        m[0].precondition = SYS_ERR_BUSY;
        build(EM_START, 0, 0);
        EM_Load(&em, &plan, 1000);
        rounds(10);
        CHECK(!m[0].begin_count && em.stage == 2);
        m[0].precondition = 0;
        rounds(100);
        CHECK(em.state == EM_DONE);
        break;
    case 47:
        candidate.modules.dependencies[1] = 0;
        CHECK(build(EM_START, 0, 0) == SYS_ERR_PROTOCOL);
        break;
    case 48:
        candidate.modules.order[0].value = 2;
        candidate.modules.order[1].value = 1;
        CHECK(build(EM_START, 0, 0) == SYS_ERR_PROTOCOL);
        break;
    case 49:
        candidate.candidate_only = 0;
        CHECK(build(EM_START, 0, 0) == SYS_ERR_INVALID_ARG);
        break;
    case 50:
        plan.count = 3;
        plan.kind = 99;
        who = 100;
        CHECK(EM_Load(&em, &plan, 1000) == SYS_ERR_INVALID_ARG);
        break;
    case 51:
        build(EM_START, 0, 0);
        EM_Load(&em, &plan, 1000);
        em.issued = 0xFFFFFFFFU;
        rounds(3);
        CHECK(em.first_error == SYS_ERR_NO_RESOURCE && !m[0].begin_count);
        break;
    case 52:
        CHECK(start_all());
        for (i = 0; i < 3; i++)
            snapshot[i] = LC_ACTIVE;
        CHECK(build(EM_START, 0, 0) == 0 && plan.count == 0);
        CHECK(EM_Load(&em, &plan, 1000) == 0);
        rounds(2);
        CHECK(em.state == EM_DONE);
        break;
    case 53:
        CHECK(start_all());
        for (i = 0; i < 3; i++)
            snapshot[i] = LC_ACTIVE;
        build(EM_STOP, 7, 1);
        EM_Load(&em, &plan, 1000);
        rounds(100);
        for (i = 0; i < 3; i++)
            snapshot[i] = LC_UNCONFIGURED;
        build(EM_START, 0, 0);
        EM_Load(&em, &plan, 1000);
        rounds(100);
        CHECK(em.state == EM_DONE && !em.first_error);
        for (i = 0; i < 3; i++)
            CHECK(m[i].lc.state == LC_ACTIVE);
        break;
    case 54:
        m[2].fail_op = LC_ACTIVATE;
        m[2].failure = SYS_ERR_IO;
        m[2].fail_fact = LC_ACTIVE;
        build(EM_START, 0, 0);
        EM_Load(&em, &plan, 1000);
        rounds(150);
        CHECK(em.state == EM_DONE && em.first_error == SYS_ERR_IO &&
              m[2].begin_op[LC_DEACTIVATE] == 1);
        break;
    case 55:
        CHECK(start_all());
        for (i = 0; i < 3; i++)
            snapshot[i] = LC_ACTIVE;
        m[1].guard = SYS_ERR_BUSY;
        build(EM_STOP, 2, 0);
        EM_Load(&em, &plan, 1000);
        rounds(20);
        CHECK(em.stage == 2 && m[1].lc.state == LC_TRANSITION_BASE + LC_DEACTIVATE);
        CHECK(m[0].lc.state == LC_ACTIVE);
        m[1].guard = 0;
        rounds(25);
        CHECK(em.state == EM_DONE);
        break;
    case 56:
        who = 1;
        memset(&resource, 0, sizeof(resource));
        {
            MRSource source = {&source_ops, NULL};
            CHECK(MR_Init(&resource, &source) == 0);
        }
        CHECK(MR_AssociateGrant(&resource, &grant) == 0);
        CHECK(MR_Close(&resource, &release_id) == 0);
        m[0].lc.ops = &resource_ops;
        resource_safe = 0;
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        LC_Request(&m[0].lc, &r, 0);
        CHECK(LC_Process(&m[0].lc, 0) == SYS_ERR_BUSY && !m[0].begin_count);
        resource_safe = 1;
        LC_Process(&m[0].lc, 1);
        CHECK(m[0].begin_count == 1);
        break;
    case 57:
        m[0].submit_full = 1;
        build(EM_START, 0, 0);
        EM_Load(&em, &plan, 3);
        rounds(10);
        CHECK(em.state == EM_DONE && em.first_error == SYS_ERR_TIMEOUT && m[0].accepted_count == 0);
        break;
    case 58:
        build(EM_START, 0, 0);
        EM_Load(&em, &plan, 1000);
        who = 1;
        CHECK(EM_Process(&em, 0) == SYS_ERR_INVALID_STATE);
        break;
    case 59:
        CHECK(start_all());
        for (i = 0; i < 3; i++)
            snapshot[i] = LC_ACTIVE;
        build(EM_STOP, 7, 0);
        EM_Load(&em, &plan, 1000);
        rounds(80);
        CHECK(em.state == EM_DONE);
        for (i = 0; i < 3; i++)
            CHECK(m[i].lc.state == LC_INACTIVE);
        break;
    case 60:
        who = 1;
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        m[0].precondition = SYS_ERR_IO;
        LC_Request(&m[0].lc, &r, 0);
        LC_Process(&m[0].lc, 0);
        CHECK(!m[0].begin_count && LC_Read(&m[0].lc, &r.id, &v) == 0 && v.result == SYS_ERR_IO &&
              v.fact == LC_UNCONFIGURED);
        break;
    case 61:
        who = 1;
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        direct_done(&r, 0, LC_INACTIVE);
        LC_Read(&m[0].lc, &r.id, &v);
        LC_Confirm(&m[0].lc, &v);
        CHECK(LC_Request(&m[0].lc, &r, 2) == 0);
        LC_Process(&m[0].lc, 2);
        CHECK(m[0].begin_count == 1);
        CHECK(LC_Read(&m[0].lc, &r.id, &v2) == 0 && !memcmp(&v, &v2, sizeof(v)));
        break;
    case 62:
        who = 1;
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        memset(&v, 0xA5, sizeof(v));
        v2 = v;
        CHECK(LC_Read(&m[0].lc, &r.id, &v) == SYS_ERR_NOT_FOUND && !memcmp(&v, &v2, sizeof(v)));
        break;
    case 63:
        build(EM_START, 0, 0);
        plan.kind = EM_STOP;
        CHECK(EM_Load(&em, &plan, 1000) == SYS_ERR_INVALID_ARG);
        break;
    case 64:
        who = 1;
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        LC_Request(&m[0].lc, &r, 0);
        LC_Process(&m[0].lc, 0);
        r.id.session++;
        CHECK(LC_Complete(&m[0].lc, &r.id, 0, LC_INACTIVE) == SYS_ERR_PROTOCOL && !m[0].lc.decided);
        break;
    case 65:
        m[0].read_wrong = 2;
        build(EM_START, 0, 0);
        EM_Load(&em, &plan, 1000);
        rounds(20);
        CHECK(em.state == EM_BLOCKED && m[0].lc.pending && em.first_error == SYS_ERR_PROTOCOL);
        break;
    case 66:
        build(EM_START, 0, 0);
        plan.steps[0].module = 99;
        CHECK(EM_Load(&em, &plan, 1000) == SYS_ERR_INVALID_ARG);
        break;
    case 67:
        m[0].consume_error = 1;
        build(EM_START, 0, 0);
        EM_Load(&em, &plan, 1000);
        rounds(20);
        CHECK(em.state == EM_BLOCKED && em.first_error == SYS_ERR_IO && m[0].lc.pending &&
              m[0].begin_count == 1);
        break;
    case 68:
        CHECK(start_all());
        for (i = 0; i < 3; i++)
            snapshot[i] = LC_ACTIVE;
        build(EM_STOP, 2, 0);
        EM_Load(&em, &plan, 1000);
        m[1].guard = SYS_ERR_BUSY;
        rounds(20);
        CHECK(m[1].lc.state == 19 && em.stage == 2);
        who = 2;
        r = request(100, LC_ACTIVATE, LC_INACTIVE);
        r.id.module = 2;
        CHECK(LC_Request(&m[1].lc, &r, now) == SYS_ERR_BUSY);
        break;
    case 69:
        candidate.modules.order[1] = candidate.modules.order[0];
        CHECK(build(EM_START, 0, 0) == SYS_ERR_PROTOCOL);
        break;
    case 70:
        who = 1;
        CHECK(LC_Init(&m[0].lc, 1, 7, &ops, &m[0]) == SYS_ERR_INVALID_ARG);
        who = 100;
        {
            EMEndpoint ep = {1, &m[0], submit, read_result, consume};
            CHECK(EM_Init(&em, 7, &ep, 1, supervisor_owner, NULL) == SYS_ERR_INVALID_ARG);
        }
        break;
    case 71:
        who = 1;
        memset(&resource, 0, sizeof(resource));
        resource_safe = 1;
        {
            MRSource source = {&source_ops, NULL};
            MRBridge b = {1, 0, 1};
            MRSlot slot = {0, 0, 0};
            MRRequest h;
            MRMedia media;
            MRView view;
            MRReceipt receipt;
            CHECK(MR_Init(&resource, &source) == 0 && MR_AssociateGrant(&resource, &grant) == 0);
            CHECK(MR_Stage(&resource, &grant, &b, &slot, &h) == 0 &&
                  MR_Commit(&resource, &h) == 0 && MR_Admit(&resource, &h) == 0);
            CHECK(MR_Close(&resource, &release_id) == 0);
            CHECK(MR_Obtain(&resource, &h, &media, &view) == SYS_ERR_INVALID_STATE);
            m[0].lc.ops = &resource_ops;
            r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
            LC_Request(&m[0].lc, &r, 0);
            CHECK(LC_Process(&m[0].lc, 0) == SYS_ERR_BUSY && !m[0].begin_count);
            CHECK(MR_ReadReceipt(&resource, &h, &receipt) == 0);
            CHECK(MR_FlushReceipt(&resource, &receipt, send_receipt, NULL) == 0);
            CHECK(MR_ConfirmConsumed(&resource, &receipt) == 0 &&
                  MR_Retire(&resource, &receipt) == 0);
            LC_Process(&m[0].lc, 1);
            CHECK(m[0].begin_count == 1);
        }
        break;
    case 72:
        who = 1;
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        m[0].guard = SYS_ERR_BUSY;
        direct_done(&r, SYS_ERR_IO, LC_INACTIVE);
        LC_Process(&m[0].lc, 20);
        m[0].guard = 0;
        LC_Process(&m[0].lc, 21);
        LC_Read(&m[0].lc, &r.id, &v);
        CHECK(v.result == SYS_ERR_IO && v.actual_result == SYS_ERR_IO);
        break;
    case 73:
        who = 1;
        r = request(1, LC_CONFIGURE, LC_UNCONFIGURED);
        direct_done(&r, 0, LC_INACTIVE);
        CHECK(LC_Read(&m[0].lc, &r.id, (LCCompletion *)&m[0].lc) == SYS_ERR_INVALID_ARG);
        break;
    case 74:
        build(EM_START, 0, 0);
        EM_Load(&em, &plan, 1000);
        {
            LCRequest saved;
            rounds(1);
            saved = m[0].queued_valid ? m[0].queued : m[0].lc.request;
            CHECK(!memcmp(&saved, &em.current, sizeof(saved)));
        }
        break;
    case 75:
        CHECK(start_all());
        for (i = 0; i < 3; i++)
            snapshot[i] = LC_ACTIVE;
        snapshot[0] = LC_INACTIVE;
        CHECK(build(EM_START, 0, 0) == SYS_ERR_INVALID_STATE);
        break;
    case 76:
        CHECK(start_all());
        for (i = 0; i < 3; i++)
            snapshot[i] = LC_ACTIVE;
        {
            V2ModuleId root = {1};
            memset(&scratch, 0, sizeof(scratch));
            CHECK(V2Resolver_Resolve(&registry, &root, 1, &scratch, &candidate.modules, &issue) ==
                  0);
        }
        CHECK(build(EM_STOP, 1, 0) == SYS_ERR_BUSY);
        CHECK(m[1].lc.state == LC_ACTIVE && m[2].lc.state == LC_ACTIVE);
        break;
    case 77:
        memset(&plan, 0xA5, sizeof(plan));
        {
            EMPlan saved = plan;
            candidate.modules.dependencies[1] = 0;
            CHECK(build(EM_START, 0, 0) == SYS_ERR_PROTOCOL &&
                  !memcmp(&saved, &plan, sizeof(plan)));
        }
        break;
    case 78:
        m[0].fail_op = LC_CONFIGURE;
        m[0].failure = SYS_ERR_IO;
        m[0].fail_fact = LC_UNCONFIGURED;
        m[0].consume_full = 1;
        build(EM_START, 0, 0);
        EM_Load(&em, &plan, 20);
        rounds(10);
        CHECK(em.stage == 3 && em.first_error == SYS_ERR_IO);
        rounds(30);
        CHECK(em.first_error == SYS_ERR_IO && m[0].lc.pending);
        m[0].consume_full = 0;
        rounds(30);
        CHECK(em.state == EM_DONE && em.first_error == SYS_ERR_IO);
        break;
    default:
        return 2;
    }
    z = 0;
    (void)z;
    return 0;
}
