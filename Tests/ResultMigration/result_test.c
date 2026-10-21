/* Isolated tests use actual production sources, no hardware or source hooks. */
#include "sys_result.h"
#include "ipc_fault.h"
#include "ipc_shared.h"
#include "framework_init.h"
#include "framework_event.h"
#include "framework_timer.h"
#include <stddef.h>

static uint32_t tick;
uint32_t Platform_TimeGetMs(void) { return tick; }
void IPC_Platform_MemoryBarrier(void) { }
void __aeabi_memcpy4(void *destination, const void *source, unsigned size)
{
    volatile unsigned char *out = (volatile unsigned char *)destination;
    const volatile unsigned char *in = (const volatile unsigned char *)source;
    while (size != 0U) { *out++ = *in++; size--; }
}
void __aeabi_memclr4(void *destination, unsigned size)
{
    volatile unsigned char *out = (volatile unsigned char *)destination;
    while (size != 0U) { *out++ = 0U; size--; }
}

#include "../../Common/Src/ipc_shared.c"
#ifdef TEST_CM4
#include "../../CM4/Framework/framework_init.c"
#include "../../CM4/Framework/framework_event.c"
#include "../../CM4/Framework/framework_timer.c"
#include "../../CM4/Service/ipc_service.c"
#else
#include "../../CM7/Framework/framework_init.c"
#include "../../CM7/Framework/framework_event.c"
#include "../../CM7/Framework/framework_timer.c"
#include "../../CM7/Service/ipc_service.c"
#include "../../CM7/Device/Src/device.c"
#endif

static unsigned fail_level = INIT_LEVEL_COUNT;
static unsigned trace;
static unsigned device_calls;
static SysResult_t InitStep(unsigned level)
{
    trace = trace * 10U + level + 1U;
    if (level == INIT_LEVEL_DEVICE) device_calls++;
    return level == fail_level ? SYS_ERR_IO : SYS_OK;
}
static SysResult_t TestPlatform(void) { return InitStep(INIT_LEVEL_PLATFORM); }
static SysResult_t TestBus(void) { return InitStep(INIT_LEVEL_BUS); }
static SysResult_t TestDevice1(void) { return InitStep(INIT_LEVEL_DEVICE); }
static SysResult_t TestDevice2(void) { return InitStep(INIT_LEVEL_DEVICE); }
static SysResult_t TestMedia(void) { return InitStep(INIT_LEVEL_MEDIA); }
static SysResult_t TestService(void) { return InitStep(INIT_LEVEL_SERVICE); }
static SysResult_t TestApplication(void) { return InitStep(INIT_LEVEL_APPLICATION); }
/* Intentionally register levels out of order to test level ordering. */
FRAMEWORK_INIT_REGISTER(TestApplication, INIT_LEVEL_APPLICATION);
FRAMEWORK_INIT_REGISTER(TestDevice1, INIT_LEVEL_DEVICE);
FRAMEWORK_INIT_REGISTER(TestPlatform, INIT_LEVEL_PLATFORM);
FRAMEWORK_INIT_REGISTER(TestMedia, INIT_LEVEL_MEDIA);
FRAMEWORK_INIT_REGISTER(TestDevice2, INIT_LEVEL_DEVICE);
FRAMEWORK_INIT_REGISTER(TestBus, INIT_LEVEL_BUS);
FRAMEWORK_INIT_REGISTER(TestService, INIT_LEVEL_SERVICE);

#define REQUIRE(c) do { if (!(c)) return __LINE__; } while (0)
static void EventHandler(const FrameworkEvent_t *event) { (void)event; }
static void TimerCallback(void *context) { (void)context; }

uint32_t Result_TestRun(unsigned test_id)
{
    SysResult_t result;
    g_ipc.magic = IPC_MAGIC;
    g_ipc.version = IPC_VERSION;
    if (test_id <= 1U) {
        fail_level = test_id == 0U ? INIT_LEVEL_COUNT : INIT_LEVEL_DEVICE;
        result = Framework_InitRuntime();
        if (test_id == 0U) {
            REQUIRE(result == SYS_OK);
            REQUIRE(trace == 1233456U && device_calls == 2U);
        } else {
            REQUIRE(result == SYS_ERR_IO);
            REQUIRE(trace == 123U && device_calls == 1U);
        }
        REQUIRE(Framework_InitLevel(INIT_LEVEL_COUNT) == SYS_ERR_INVALID_ARG);
        return 0U;
    }
    if (test_id == 2U) {
        IpcFaultFlags_t flags = IPC_FAULT_MAGIC_TIMEOUT | IPC_FAULT_VERSION_MISMATCH |
            IPC_FAULT_PEER_NOT_READY | IPC_FAULT_HEARTBEAT_TIMEOUT |
            IPC_FAULT_HSEM | IPC_FAULT_SHARED_INVALID;
        REQUIRE(sizeof(SysResult_t) == 4U && sizeof(IpcFaultFlags_t) == 4U);
        REQUIRE(SYS_OK == 0 && SYS_ERR_INVALID_ARG == -1 && SYS_ERR_NOT_FOUND == -2);
        REQUIRE(SYS_ERR_ALREADY_EXISTS == -3 && SYS_ERR_INVALID_STATE == -4 && SYS_ERR_BUSY == -5);
        REQUIRE(SYS_ERR_NO_RESOURCE == -6 && SYS_ERR_NOT_SUPPORTED == -7 && SYS_ERR_TIMEOUT == -8);
        REQUIRE(SYS_ERR_IO == -9 && SYS_ERR_PROTOCOL == -10);
        REQUIRE(IPC_FAULT_MAGIC_TIMEOUT == 1U && IPC_FAULT_VERSION_MISMATCH == 2U);
        REQUIRE(IPC_FAULT_PEER_NOT_READY == 4U && IPC_FAULT_HEARTBEAT_TIMEOUT == 8U);
        REQUIRE(IPC_FAULT_HSEM == 16U && IPC_FAULT_SHARED_INVALID == 32U && flags == 63U);
        REQUIRE(sizeof(IpcShared_t) == 32U && IPC_VERSION == 1U);
        REQUIRE(offsetof(IpcShared_t,magic) == 0U && offsetof(IpcShared_t,version) == 4U);
        REQUIRE(offsetof(IpcShared_t,m7_ready) == 8U && offsetof(IpcShared_t,m4_ready) == 12U);
        REQUIRE(offsetof(IpcShared_t,m7_heartbeat) == 16U && offsetof(IpcShared_t,m4_heartbeat) == 20U);
        REQUIRE(offsetof(IpcShared_t,m7_err) == 24U && offsetof(IpcShared_t,m4_err) == 28U);
        g_ipc.m7_err |= IPC_FAULT_HSEM;
        g_ipc.m7_err |= IPC_FAULT_HEARTBEAT_TIMEOUT;
        REQUIRE(g_ipc.m7_err == 24U);
#ifdef TEST_CM4
        g_ipc.magic = 0U;
        REQUIRE(IPC_Service_Init() == SYS_ERR_PROTOCOL);
        REQUIRE(g_ipc.m4_err == IPC_FAULT_SHARED_INVALID);
        g_ipc.magic = IPC_MAGIC; g_ipc.version = 2U;
        REQUIRE(IPC_Service_Init() == SYS_ERR_PROTOCOL);
        REQUIRE(g_ipc.m4_err == (IPC_FAULT_SHARED_INVALID | IPC_FAULT_VERSION_MISMATCH));
        g_ipc.version = IPC_VERSION;
        REQUIRE(IPC_Service_Init() == SYS_OK && g_ipc.m4_ready == IPC_CORE_READY);
        REQUIRE(g_ipc.m4_err == 34U);
#else
        IPC_Service_EarlyInit();
        REQUIRE(g_ipc.magic == IPC_MAGIC && g_ipc.version == IPC_VERSION && g_ipc.m7_err == 0U);
        REQUIRE(IPC_Service_Init() == SYS_OK && g_ipc.m7_ready == IPC_CORE_READY);
#endif
        return 0U;
    }
    if (test_id == 3U) {
        FrameworkEvent_t event;
        unsigned i;
        event.data = 0U;
        REQUIRE(Framework_EventSubscribe(0U,NULL) == SYS_ERR_INVALID_ARG);
        REQUIRE(Framework_EventPost(NULL) == SYS_ERR_INVALID_ARG);
        REQUIRE(Framework_EventSubscribe(0U,EventHandler) == SYS_OK);
        REQUIRE(Framework_EventSubscribe(0U,EventHandler) == SYS_ERR_ALREADY_EXISTS);
        for (i=1U; i<16U; i++) REQUIRE(Framework_EventSubscribe(i,EventHandler) == SYS_OK);
        REQUIRE(Framework_EventSubscribe(16U,EventHandler) == SYS_ERR_NO_RESOURCE);
        for (i=0U; i<16U; i++) { event.id=i; REQUIRE(Framework_EventPost(&event) == SYS_OK); }
        REQUIRE(Framework_EventPost(&event) == SYS_ERR_NO_RESOURCE);
        Framework_EventDispatch();
        REQUIRE(Framework_EventPost(&event) == SYS_OK);
        return 0U;
    }
    if (test_id == 4U) {
        FrameworkTimerConfig_t config;
        FrameworkTimerHandle_t handle, old_handle;
        unsigned i;
        config.period_ms=100U; config.mode=FRAMEWORK_TIMER_ONE_SHOT;
        config.callback=TimerCallback; config.context=NULL;
        REQUIRE(Framework_TimerCreate(NULL,&handle) == SYS_ERR_INVALID_ARG);
        REQUIRE(Framework_TimerCreate(&config,NULL) == SYS_ERR_INVALID_ARG);
        config.callback=NULL;
        REQUIRE(Framework_TimerCreate(&config,&handle) == SYS_ERR_INVALID_ARG);
        config.callback=TimerCallback; config.period_ms=0U;
        REQUIRE(Framework_TimerCreate(&config,&handle) == SYS_ERR_INVALID_ARG);
        config.period_ms=TIMER_MAX_PERIOD_MS+1U;
        REQUIRE(Framework_TimerCreate(&config,&handle) == SYS_ERR_INVALID_ARG);
        /* Deliberately construct an invalid enum input in the test only. */
        config.period_ms=100U; config.mode=(FrameworkTimerMode_t)2;
        REQUIRE(Framework_TimerCreate(&config,&handle) == SYS_ERR_INVALID_ARG);
        config.mode=FRAMEWORK_TIMER_ONE_SHOT;
        REQUIRE(Framework_TimerCreate(&config,&handle) == SYS_OK);
        old_handle=handle;
        REQUIRE(Framework_TimerDelete(handle) == SYS_OK);
        REQUIRE(Framework_TimerCreate(&config,&handle) == SYS_OK);
        REQUIRE(Framework_TimerStart(old_handle) == SYS_ERR_NOT_FOUND);
        REQUIRE(Framework_TimerStop(old_handle) == SYS_ERR_NOT_FOUND);
        REQUIRE(Framework_TimerDelete(old_handle) == SYS_ERR_NOT_FOUND);
        for (i=1U; i<TIMER_POOL_MAX; i++) REQUIRE(Framework_TimerCreate(&config,&handle) == SYS_OK);
        REQUIRE(Framework_TimerCreate(&config,&handle) == SYS_ERR_NO_RESOURCE);
        return 0U;
    }
#ifndef TEST_CM4
    if (test_id == 5U) {
        Device_t dev = {0};
        REQUIRE(Device_Register(NULL) == SYS_ERR_INVALID_ARG);
        REQUIRE(Device_Register(&dev) == SYS_ERR_INVALID_ARG);
        dev.name="";
        REQUIRE(Device_Register(&dev) == SYS_ERR_INVALID_ARG);
        dev.name="test";
        REQUIRE(Device_Register(&dev) == SYS_ERR_NOT_SUPPORTED);
        return 0U;
    }
#endif
    return __LINE__;
}
