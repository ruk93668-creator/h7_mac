/* Actual CM4 Device and Initcall implementations; test-only callbacks. */
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
static SysResult_t Init(Device_t *dev);
static SysResult_t Start(Device_t *dev);
static SysResult_t Stop(Device_t *dev);
static const DeviceLifecycleOps_t ops={Init,Start,Stop};
static const DeviceLifecycleOps_t no_init={NULL,Start,Stop};
static const DeviceLifecycleOps_t no_start={Init,NULL,Stop};
static const DeviceLifecycleOps_t no_stop={Init,Start,NULL};
static SimDevice manual={{"manual",DEVICE_STATE_UNINIT,{NULL,NULL},&ops,false},1U};
static SimDevice automatic={{"automatic",DEVICE_STATE_UNINIT,{NULL,NULL},&ops,true},2U};
DEVICE_EXPORT(manual);
DEVICE_EXPORT(automatic);

typedef struct {
    uint32_t failure_line;
    int32_t outer_result;
    int32_t nested_init;
    int32_t nested_start;
    int32_t nested_stop;
    uint32_t init_calls;
    uint32_t start_calls;
    uint32_t stop_calls;
    uint32_t busy_seen;
    uint32_t final_state;
    uint32_t auto_state;
    uint32_t level_trace;
    uint32_t list_count;
    uint32_t auto_init_calls;
} Result;
volatile Result g_busy_result;
static uint32_t reentry_phase;
static uint32_t failing_phase;
static bool nested_once;
static bool callback_invalid;
#define CHECK(c) do { if (!(c)) { if (!g_busy_result.failure_line) g_busy_result.failure_line=__LINE__; return; } } while (0)

static SysResult_t Callback(Device_t *dev,unsigned phase)
{
    if ((dev!=&manual.base && dev!=&automatic.base) || dev->state!=DEVICE_STATE_BUSY)
        callback_invalid=true;
    else g_busy_result.busy_seen++;
    if (phase==1U) { g_busy_result.init_calls++; if (dev==&automatic.base) g_busy_result.auto_init_calls++; }
    if (phase==2U) g_busy_result.start_calls++;
    if (phase==3U) g_busy_result.stop_calls++;
    if (phase==reentry_phase && !nested_once) {
        nested_once=true; /* Keep a broken guard from causing unbounded recursion in the test. */
        g_busy_result.nested_init=Device_Init(dev);
        g_busy_result.nested_start=Device_Start(dev);
        g_busy_result.nested_stop=Device_Stop(dev);
        if (dev->state!=DEVICE_STATE_BUSY) callback_invalid=true;
    }
    return phase==failing_phase ? SYS_ERR_IO : SYS_OK;
}
static SysResult_t Init(Device_t *dev) { return Callback(dev,1U); }
static SysResult_t Start(Device_t *dev) { return Callback(dev,2U); }
static SysResult_t Stop(Device_t *dev) { return Callback(dev,3U); }
static SysResult_t Platform(void) { g_busy_result.level_trace=1U; return SYS_OK; }
static SysResult_t Media(void) { g_busy_result.level_trace=g_busy_result.level_trace*10U+4U; return SYS_OK; }
static SysResult_t Service(void) { g_busy_result.level_trace=g_busy_result.level_trace*10U+5U; return SYS_OK; }
static SysResult_t Application(void) { g_busy_result.level_trace=g_busy_result.level_trace*10U+6U; return SYS_OK; }
FRAMEWORK_INIT_REGISTER(Application,INIT_LEVEL_APPLICATION);
FRAMEWORK_INIT_REGISTER(Media,INIT_LEVEL_MEDIA);
FRAMEWORK_INIT_REGISTER(Platform,INIT_LEVEL_PLATFORM);
FRAMEWORK_INIT_REGISTER(Service,INIT_LEVEL_SERVICE);

static void ListCheck(unsigned expected)
{
    FrameworkListNode_t *p=s_device_head.next;
    unsigned count=0U;
    CHECK(s_device_head.next->prev==&s_device_head && s_device_head.prev->next==&s_device_head);
    while (p!=&s_device_head) {
        CHECK(count<expected);
        CHECK(p==&manual.base.node || p==&automatic.base.node);
        CHECK(p->next->prev==p && p->prev->next==p);
        count++;p=p->next;
    }
    CHECK(count==expected);
    g_busy_result.list_count=count;
}
static void Run(unsigned test_id)
{
    if (test_id==10U) {
        g_busy_result.outer_result=Framework_InitRuntime();
        CHECK(g_busy_result.outer_result==SYS_OK && g_busy_result.level_trace==1456U);
        CHECK(manual.base.state==DEVICE_STATE_UNINIT && automatic.base.state==DEVICE_STATE_READY);
        CHECK(g_busy_result.init_calls==1U && g_busy_result.auto_init_calls==1U);
        ListCheck(2U); return;
    }
    CHECK(Device_RegisterAll()==SYS_OK);
    CHECK(Device_Find("manual")==&manual.base && Device_Find("automatic")==&automatic.base);
    ListCheck(2U);
    if (test_id==8U) {
        CHECK(g_busy_result.init_calls==0U && manual.base.state==DEVICE_STATE_UNINIT && automatic.base.state==DEVICE_STATE_UNINIT);
        return;
    }
    if (test_id==9U) {
        g_busy_result.outer_result=Device_InitAll();
        CHECK(g_busy_result.outer_result==SYS_OK);
        CHECK(manual.base.state==DEVICE_STATE_UNINIT && automatic.base.state==DEVICE_STATE_READY);
        CHECK(g_busy_result.init_calls==1U && g_busy_result.auto_init_calls==1U);
        return;
    }
    if (test_id==4U) {
        manual.base.state=DEVICE_STATE_BUSY;
        g_busy_result.nested_init=Device_Init(&manual.base);
        g_busy_result.nested_start=Device_Start(&manual.base);
        g_busy_result.nested_stop=Device_Stop(&manual.base);
        CHECK(g_busy_result.nested_init==SYS_ERR_BUSY && g_busy_result.nested_start==SYS_ERR_BUSY && g_busy_result.nested_stop==SYS_ERR_BUSY);
        CHECK(manual.base.state==DEVICE_STATE_BUSY && g_busy_result.busy_seen==0U);
        return;
    }
    if (test_id>=5U && test_id<=7U) {
        unsigned phase=test_id-4U;
        if (phase>=2U) CHECK(Device_Init(&manual.base)==SYS_OK);
        if (phase>=3U) CHECK(Device_Start(&manual.base)==SYS_OK);
        manual.base.lifecycle=NULL;
        g_busy_result.outer_result=phase==1U ? Device_Init(&manual.base) : (phase==2U ? Device_Start(&manual.base) : Device_Stop(&manual.base));
        CHECK(g_busy_result.outer_result==SYS_ERR_NOT_SUPPORTED);
        CHECK(manual.base.state==(phase==1U ? DEVICE_STATE_UNINIT : (phase==2U ? DEVICE_STATE_READY : DEVICE_STATE_RUNNING)));
        manual.base.lifecycle=phase==1U ? &no_init : (phase==2U ? &no_start : &no_stop);
        g_busy_result.outer_result=phase==1U ? Device_Init(&manual.base) : (phase==2U ? Device_Start(&manual.base) : Device_Stop(&manual.base));
        CHECK(g_busy_result.outer_result==SYS_ERR_NOT_SUPPORTED);
        CHECK(manual.base.state==(phase==1U ? DEVICE_STATE_UNINIT : (phase==2U ? DEVICE_STATE_READY : DEVICE_STATE_RUNNING)));
        CHECK(g_busy_result.init_calls==(phase>=2U ? 1U : 0U) && g_busy_result.start_calls==(phase==3U ? 1U : 0U) && g_busy_result.stop_calls==0U);
        return;
    }
    if (test_id>=11U) failing_phase=test_id-10U;
    if (test_id>=1U && test_id<=3U) reentry_phase=test_id;
    g_busy_result.outer_result=Device_Init(&manual.base);
    if (failing_phase==1U) {
        CHECK(g_busy_result.outer_result==SYS_ERR_IO && manual.base.state==DEVICE_STATE_ERROR); return;
    }
    CHECK(g_busy_result.outer_result==SYS_OK && manual.base.state==DEVICE_STATE_READY);
    if (test_id==1U) {
        CHECK(g_busy_result.nested_init==SYS_ERR_BUSY && g_busy_result.nested_start==SYS_ERR_BUSY && g_busy_result.nested_stop==SYS_ERR_BUSY);
        CHECK(g_busy_result.init_calls==1U && g_busy_result.start_calls==0U && g_busy_result.stop_calls==0U); return;
    }
    g_busy_result.outer_result=Device_Start(&manual.base);
    if (failing_phase==2U) {
        CHECK(g_busy_result.outer_result==SYS_ERR_IO && manual.base.state==DEVICE_STATE_ERROR); return;
    }
    CHECK(g_busy_result.outer_result==SYS_OK && manual.base.state==DEVICE_STATE_RUNNING);
    if (test_id==2U) {
        CHECK(g_busy_result.nested_init==SYS_ERR_BUSY && g_busy_result.nested_start==SYS_ERR_BUSY && g_busy_result.nested_stop==SYS_ERR_BUSY);
        CHECK(g_busy_result.init_calls==1U && g_busy_result.start_calls==1U && g_busy_result.stop_calls==0U); return;
    }
    g_busy_result.outer_result=Device_Stop(&manual.base);
    if (failing_phase==3U) {
        CHECK(g_busy_result.outer_result==SYS_ERR_IO && manual.base.state==DEVICE_STATE_ERROR); return;
    }
    CHECK(g_busy_result.outer_result==SYS_OK && manual.base.state==DEVICE_STATE_READY);
    CHECK(g_busy_result.init_calls==1U && g_busy_result.start_calls==1U && g_busy_result.stop_calls==1U);
    if (test_id==3U)
        CHECK(g_busy_result.nested_init==SYS_ERR_BUSY && g_busy_result.nested_start==SYS_ERR_BUSY && g_busy_result.nested_stop==SYS_ERR_BUSY);
}
uint32_t Busy_TestRun(unsigned test_id)
{
    Run(test_id);
    if (callback_invalid && !g_busy_result.failure_line) g_busy_result.failure_line=__LINE__;
    g_busy_result.final_state=manual.base.state;
    g_busy_result.auto_state=automatic.base.state;
    return g_busy_result.failure_line;
}
