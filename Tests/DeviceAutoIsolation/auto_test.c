/* Actual CM4 registry and Initcall code; no hardware, no production edits. */
#include "device.h"
#include "framework_init.h"
int strcmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
#include "../../CM4/Device/Src/device.c"
#include "../../CM4/Framework/framework_init.c"

typedef struct { Device_t base; uint32_t id; } SimDevice;
static SysResult_t SimInit(Device_t *dev);
static const DeviceLifecycleOps_t ops = { SimInit, NULL, NULL };
static SimDevice a = { {"a",DEVICE_STATE_UNINIT,{NULL,NULL},&ops,true}, 1U };
static SimDevice b = { {"b",DEVICE_STATE_UNINIT,{NULL,NULL},&ops,false}, 2U };
static SimDevice c = { {"c",DEVICE_STATE_UNINIT,{NULL,NULL},&ops,true}, 3U };
static SimDevice extra = { {"extra",DEVICE_STATE_UNINIT,{NULL,NULL},&ops,true}, 4U };
DEVICE_EXPORT(a);
DEVICE_EXPORT(b);
DEVICE_EXPORT(c);

typedef struct {
    uint32_t failure_line;
    int32_t result;
    int32_t secondary_result;
    uint32_t callbacks;
    uint32_t callback_trace;
    uint32_t level_trace;
    uint32_t registered;
    uint32_t state_a;
    uint32_t state_b;
    uint32_t state_c;
    uint32_t state_extra;
    uint32_t list_checks;
} TestResult;
volatile TestResult g_auto_result;
static uint32_t fail_id;
static uint32_t callback_mode;
static bool once;
#define CHECK(condition) do { if (!(condition)) { if (!g_auto_result.failure_line) g_auto_result.failure_line=__LINE__; return; } } while (0)

static SysResult_t SimInit(Device_t *dev)
{
    SimDevice *sim = FRAMEWORK_CONTAINER_OF(dev,SimDevice,base);
    g_auto_result.callbacks++;
    g_auto_result.callback_trace=g_auto_result.callback_trace*10U+sim->id;
    if (sim->id==1U && !once) {
        once=true;
        if (callback_mode==1U) g_auto_result.secondary_result=Device_Init(dev);
        if (callback_mode==2U) g_auto_result.secondary_result=Device_Register(&extra.base);
    }
    return sim->id==fail_id ? SYS_ERR_IO : SYS_OK;
}
static SysResult_t Platform(void) { g_auto_result.level_trace=g_auto_result.level_trace*10U+1U; return SYS_OK; }
static SysResult_t Media(void) { g_auto_result.level_trace=g_auto_result.level_trace*10U+4U; return SYS_OK; }
static SysResult_t Service(void) { g_auto_result.level_trace=g_auto_result.level_trace*10U+5U; return SYS_OK; }
static SysResult_t Application(void) { g_auto_result.level_trace=g_auto_result.level_trace*10U+6U; return SYS_OK; }
FRAMEWORK_INIT_REGISTER(Application,INIT_LEVEL_APPLICATION);
FRAMEWORK_INIT_REGISTER(Service,INIT_LEVEL_SERVICE);
FRAMEWORK_INIT_REGISTER(Media,INIT_LEVEL_MEDIA);
FRAMEWORK_INIT_REGISTER(Platform,INIT_LEVEL_PLATFORM);

static void ListCheck(unsigned expected)
{
    unsigned count=0U;
    FrameworkListNode_t *p=s_device_head.next;
    g_auto_result.list_checks++;
    CHECK(s_device_head.next->prev==&s_device_head && s_device_head.prev->next==&s_device_head);
    while (p!=&s_device_head) {
        CHECK(count<expected);
        CHECK(p==&a.base.node || p==&b.base.node || p==&c.base.node || p==&extra.base.node);
        CHECK(p->next->prev==p && p->prev->next==p);
        count++; p=p->next;
    }
    CHECK(count==expected);
    g_auto_result.registered=count;
}
static void Run(unsigned test_id)
{
    if (test_id==1U) { a.base.auto_init=false; c.base.auto_init=false; }
    if (test_id==4U) b.base.name="";
    if (test_id==5U) b.base.state=DEVICE_STATE_READY;
    if (test_id==6U || test_id==7U) b.base.lifecycle=NULL;
    if (test_id==7U) b.base.auto_init=true;
    if (test_id==8U) fail_id=3U;
    if (test_id==13U) callback_mode=1U;
    if (test_id==14U) callback_mode=2U;
    if (test_id==9U) {
        g_auto_result.result=Device_RegisterAll();
        CHECK(g_auto_result.result==SYS_OK); ListCheck(3U);
        g_auto_result.secondary_result=Device_RegisterAll();
        CHECK(g_auto_result.secondary_result==SYS_ERR_ALREADY_EXISTS);
        CHECK(g_auto_result.callbacks==0U); ListCheck(3U); return;
    }
    if (test_id==10U || test_id==11U) {
        CHECK(Device_RegisterAll()==SYS_OK);
        if (test_id==11U) { CHECK(Device_Init(&a.base)==SYS_OK); }
        else { CHECK(Device_InitAll()==SYS_OK); }
        g_auto_result.result=Device_InitAll();
        CHECK(g_auto_result.result==SYS_ERR_INVALID_STATE);
        CHECK(g_auto_result.callbacks==(test_id==10U ? 2U : 1U)); ListCheck(3U); return;
    }
    if (test_id==12U) {
        g_auto_result.result=Device_InitAll();
        CHECK(g_auto_result.result==SYS_OK && g_auto_result.callbacks==0U); ListCheck(0U); return;
    }
    g_auto_result.result=Framework_InitRuntime();
    if (test_id>=2U && test_id<=5U) {
        CHECK(g_auto_result.result==(test_id==2U || test_id==4U ? SYS_ERR_INVALID_ARG :
            (test_id==3U ? SYS_ERR_ALREADY_EXISTS : SYS_ERR_INVALID_STATE)));
        CHECK(g_auto_result.callbacks==0U && g_auto_result.level_trace==1U);
        CHECK(Device_Find("a")==&a.base && Device_Find("c")==NULL);
        ListCheck(1U); return;
    }
    if (test_id==7U) {
        CHECK(g_auto_result.result==SYS_ERR_NOT_SUPPORTED);
        CHECK(g_auto_result.callback_trace==1U && g_auto_result.level_trace==1U);
        CHECK(a.base.state==DEVICE_STATE_READY && b.base.state==DEVICE_STATE_UNINIT && c.base.state==DEVICE_STATE_UNINIT);
        ListCheck(3U); return;
    }
    if (test_id==8U) {
        CHECK(g_auto_result.result==SYS_ERR_IO);
        CHECK(g_auto_result.callback_trace==13U && g_auto_result.level_trace==1U);
        CHECK(a.base.state==DEVICE_STATE_READY && c.base.state==DEVICE_STATE_ERROR);
        ListCheck(3U); return;
    }
    CHECK(g_auto_result.result==SYS_OK && g_auto_result.level_trace==1456U);
    CHECK(Device_Find("a")==&a.base && Device_Find("b")==&b.base && Device_Find("c")==&c.base);
    ListCheck(test_id==14U ? 4U : 3U);
    CHECK(b.base.state==DEVICE_STATE_UNINIT);
    if (test_id==1U) {
        CHECK(g_auto_result.callbacks==0U && a.base.state==DEVICE_STATE_UNINIT && c.base.state==DEVICE_STATE_UNINIT);
    } else if (test_id==14U) {
        CHECK(g_auto_result.secondary_result==SYS_OK && g_auto_result.callback_trace==134U);
        CHECK(extra.base.state==DEVICE_STATE_READY);
    } else {
        CHECK(a.base.state==DEVICE_STATE_READY && c.base.state==DEVICE_STATE_READY);
        /* Desired safety property: self-reentrant Init must not run twice. */
        CHECK(g_auto_result.callbacks==2U && g_auto_result.callback_trace==13U);
    }
}
uint32_t Auto_TestRun(unsigned test_id)
{
    Run(test_id);
    g_auto_result.state_a=a.base.state;
    g_auto_result.state_b=b.base.state;
    g_auto_result.state_c=c.base.state;
    g_auto_result.state_extra=extra.base.state;
    return g_auto_result.failure_line;
}
