/* Actual per-core Device implementation + simulated lifecycle operations only. */
#include "device.h"
#include <stdbool.h>

/* Minimal standard-library comparison stub; no hardware or C runtime needed. */
int strcmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

#ifdef TEST_CM4
#include "../../CM4/Device/Src/device.c"
#else
#include "../../CM7/Device/Src/device.c"
#endif

static SysResult_t init_result = SYS_OK;
static SysResult_t start_result = SYS_OK;
static SysResult_t stop_result = SYS_OK;
static Device_t *expected_device;
static bool callback_problem;

typedef struct {
    uint32_t failure_line;
    uint32_t init_calls;
    uint32_t start_calls;
    uint32_t stop_calls;
    uint32_t callback_trace;
    uint32_t list_checks;
    uint32_t final_state;
    SysResult_t register_result;
    uint32_t find_same_object;
    SysResult_t init_result;
    SysResult_t start_result;
    SysResult_t stop_result;
} DeviceTestResult;
volatile DeviceTestResult g_device_test;

static SysResult_t SimInit(Device_t *dev)
{
    if (dev != expected_device || dev->state != DEVICE_STATE_UNINIT) callback_problem = true;
    g_device_test.init_calls++;
    g_device_test.callback_trace = g_device_test.callback_trace * 10U + 1U;
    return init_result;
}
static SysResult_t SimStart(Device_t *dev)
{
    if (dev != expected_device || dev->state != DEVICE_STATE_READY) callback_problem = true;
    g_device_test.start_calls++;
    g_device_test.callback_trace = g_device_test.callback_trace * 10U + 2U;
    return start_result;
}
static SysResult_t SimStop(Device_t *dev)
{
    if (dev != expected_device || dev->state != DEVICE_STATE_RUNNING) callback_problem = true;
    g_device_test.stop_calls++;
    g_device_test.callback_trace = g_device_test.callback_trace * 10U + 3U;
    return stop_result;
}
static const DeviceLifecycleOps_t ops = { SimInit, SimStart, SimStop };
static const DeviceLifecycleOps_t no_init = { NULL, SimStart, SimStop };
static const DeviceLifecycleOps_t no_start = { SimInit, NULL, SimStop };
static const DeviceLifecycleOps_t no_stop = { SimInit, SimStart, NULL };
static Device_t camera = { "camera_sim", DEVICE_STATE_UNINIT, {NULL,NULL}, &ops };
static Device_t network = { "network_sim", DEVICE_STATE_UNINIT, {NULL,NULL}, &ops };
static Device_t duplicate = { "camera_sim", DEVICE_STATE_UNINIT, {NULL,NULL}, &ops };

#define CHECK(c) do { if (!(c)) { g_device_test.failure_line = __LINE__; return; } } while (0)

static void CheckList(unsigned expected_count)
{
    FrameworkListNode_t *node = s_device_head.next;
    unsigned count = 0U;
    g_device_test.list_checks++;
    CHECK(s_device_head.next->prev == &s_device_head);
    CHECK(s_device_head.prev->next == &s_device_head);
    while (node != &s_device_head) {
        CHECK(count < expected_count);
        CHECK(node->next->prev == node && node->prev->next == node);
        CHECK(node == &camera.node || node == &network.node);
        count++; node = node->next;
    }
    CHECK(count == expected_count);
}

static void TestPipeline(void)
{
    Device_t *found;
    expected_device = &camera;
    CHECK(camera.state == DEVICE_STATE_UNINIT);
    g_device_test.register_result = Device_Register(&camera);
    CHECK(g_device_test.register_result == SYS_OK);
    CheckList(1U);
    CHECK(camera.state == DEVICE_STATE_UNINIT && g_device_test.callback_trace == 0U);
    found = Device_Find("camera_sim");
    g_device_test.find_same_object = found == &camera;
    CHECK(found == &camera);
    g_device_test.init_result = Device_Init(found);
    CHECK(g_device_test.init_result == SYS_OK && camera.state == DEVICE_STATE_READY);
    g_device_test.start_result = Device_Start(found);
    CHECK(g_device_test.start_result == SYS_OK && camera.state == DEVICE_STATE_RUNNING);
    g_device_test.stop_result = Device_Stop(found);
    CHECK(g_device_test.stop_result == SYS_OK && camera.state == DEVICE_STATE_READY);
    CHECK(g_device_test.init_calls == 1U && g_device_test.start_calls == 1U && g_device_test.stop_calls == 1U);
    CHECK(g_device_test.callback_trace == 123U && !callback_problem);
    CheckList(1U);
}

static void TestRegistry(void)
{
    char query[] = "camera_sim";
    CHECK(Device_Register(&camera) == SYS_OK);
    CHECK(Device_Register(&network) == SYS_OK);
    CheckList(2U);
    CHECK(Device_Find(query) == &camera && Device_Find("network_sim") == &network);
    CHECK(Device_Find("missing") == NULL);
    CHECK(Device_Register(&camera) == SYS_ERR_ALREADY_EXISTS);
    CHECK(Device_Register(&duplicate) == SYS_ERR_ALREADY_EXISTS);
    CheckList(2U);
    CHECK(g_device_test.callback_trace == 0U);
}

static void TestArguments(void)
{
    CHECK(Device_Register(NULL) == SYS_ERR_INVALID_ARG);
    CHECK(Device_Init(NULL) == SYS_ERR_INVALID_ARG);
    CHECK(Device_Start(NULL) == SYS_ERR_INVALID_ARG);
    CHECK(Device_Stop(NULL) == SYS_ERR_INVALID_ARG);
    CHECK(Device_Find(NULL) == NULL && Device_Find("") == NULL);
    camera.name = NULL;
    CHECK(Device_Register(&camera) == SYS_ERR_INVALID_ARG);
    CHECK(Device_Init(&camera) == SYS_ERR_INVALID_ARG);
    CHECK(Device_Start(&camera) == SYS_ERR_INVALID_ARG);
    CHECK(Device_Stop(&camera) == SYS_ERR_INVALID_ARG);
    camera.name = "";
    CHECK(Device_Register(&camera) == SYS_ERR_INVALID_ARG);
    CHECK(Device_Init(&camera) == SYS_ERR_INVALID_ARG);
    CHECK(Device_Start(&camera) == SYS_ERR_INVALID_ARG);
    CHECK(Device_Stop(&camera) == SYS_ERR_INVALID_ARG);
    CHECK(g_device_test.callback_trace == 0U);
    CheckList(0U);
}

static void TestIdentity(void)
{
    CHECK(Device_Init(&camera) == SYS_ERR_NOT_FOUND);
    CHECK(Device_Start(&camera) == SYS_ERR_NOT_FOUND);
    CHECK(Device_Stop(&camera) == SYS_ERR_NOT_FOUND);
    CHECK(Device_Register(&camera) == SYS_OK);
    CHECK(Device_Init(&duplicate) == SYS_ERR_NOT_FOUND);
    CHECK(Device_Start(&duplicate) == SYS_ERR_NOT_FOUND);
    CHECK(Device_Stop(&duplicate) == SYS_ERR_NOT_FOUND);
    CHECK(g_device_test.callback_trace == 0U);
    CheckList(1U);
}

static void TestStateOrder(void)
{
    expected_device = &camera;
    CHECK(Device_Register(&camera) == SYS_OK);
    CHECK(Device_Start(&camera) == SYS_ERR_INVALID_STATE);
    CHECK(Device_Stop(&camera) == SYS_ERR_INVALID_STATE);
    CHECK(Device_Init(&camera) == SYS_OK);
    CHECK(Device_Init(&camera) == SYS_ERR_INVALID_STATE);
    CHECK(Device_Stop(&camera) == SYS_ERR_INVALID_STATE);
    CHECK(Device_Start(&camera) == SYS_OK);
    CHECK(Device_Start(&camera) == SYS_ERR_INVALID_STATE);
    CHECK(Device_Init(&camera) == SYS_ERR_INVALID_STATE);
    CHECK(Device_Stop(&camera) == SYS_OK);
    CHECK(Device_Stop(&camera) == SYS_ERR_INVALID_STATE);
    CHECK(Device_Start(&camera) == SYS_OK);
    CHECK(Device_Stop(&camera) == SYS_OK);
    CHECK(g_device_test.callback_trace == 12323U && !callback_problem);
    CheckList(1U);
}

static void TestUnsupported(void)
{
    expected_device = &camera;
    CHECK(Device_Register(&camera) == SYS_OK);
    camera.lifecycle = NULL;
    CHECK(Device_Init(&camera) == SYS_ERR_NOT_SUPPORTED);
    CHECK(camera.state == DEVICE_STATE_UNINIT);
    camera.lifecycle = &no_init;
    CHECK(Device_Init(&camera) == SYS_ERR_NOT_SUPPORTED);
    camera.lifecycle = &no_start;
    CHECK(Device_Init(&camera) == SYS_OK);
    CHECK(Device_Start(&camera) == SYS_ERR_NOT_SUPPORTED && camera.state == DEVICE_STATE_READY);
    camera.lifecycle = NULL;
    CHECK(Device_Start(&camera) == SYS_ERR_NOT_SUPPORTED);
    camera.lifecycle = &no_stop;
    CHECK(Device_Start(&camera) == SYS_OK);
    CHECK(Device_Stop(&camera) == SYS_ERR_NOT_SUPPORTED && camera.state == DEVICE_STATE_RUNNING);
    camera.lifecycle = NULL;
    CHECK(Device_Stop(&camera) == SYS_ERR_NOT_SUPPORTED);
    CHECK(g_device_test.callback_trace == 12U && !callback_problem);
    CheckList(1U);
}

static void TestCallbackFailure(unsigned phase)
{
    expected_device = &camera;
    CHECK(Device_Register(&camera) == SYS_OK);
    if (phase == 0U) {
        init_result = SYS_ERR_IO;
        CHECK(Device_Init(&camera) == SYS_ERR_IO);
    } else {
        CHECK(Device_Init(&camera) == SYS_OK);
        if (phase == 1U) {
            start_result = SYS_ERR_BUSY;
            CHECK(Device_Start(&camera) == SYS_ERR_BUSY);
        } else {
            CHECK(Device_Start(&camera) == SYS_OK);
            stop_result = SYS_ERR_TIMEOUT;
            CHECK(Device_Stop(&camera) == SYS_ERR_TIMEOUT);
        }
    }
    CHECK(camera.state == DEVICE_STATE_ERROR);
    CHECK(Device_Init(&camera) == SYS_ERR_INVALID_STATE);
    CHECK(Device_Start(&camera) == SYS_ERR_INVALID_STATE);
    CHECK(Device_Stop(&camera) == SYS_ERR_INVALID_STATE);
    CHECK(g_device_test.init_calls == 1U);
    CHECK(g_device_test.start_calls == (phase == 0U ? 0U : 1U));
    CHECK(g_device_test.stop_calls == (phase == 2U ? 1U : 0U));
    CHECK(!callback_problem);
    CheckList(1U);
}

static void TestRegisterState(void)
{
    camera.state = DEVICE_STATE_READY;
    CHECK(Device_Register(&camera) == SYS_ERR_INVALID_STATE);
    camera.state = DEVICE_STATE_RUNNING;
    CHECK(Device_Register(&camera) == SYS_ERR_INVALID_STATE);
    camera.state = DEVICE_STATE_ERROR;
    CHECK(Device_Register(&camera) == SYS_ERR_INVALID_STATE);
    CHECK(Device_Find("camera_sim") == NULL);
    CHECK(g_device_test.callback_trace == 0U);
    CheckList(0U);
}

uint32_t Device_TestRun(unsigned test_id)
{
    switch (test_id) {
        case 0U: TestPipeline(); break;
        case 1U: TestRegistry(); break;
        case 2U: TestArguments(); break;
        case 3U: TestIdentity(); break;
        case 4U: TestStateOrder(); break;
        case 5U: TestUnsupported(); break;
        case 6U: TestCallbackFailure(0U); break;
        case 7U: TestCallbackFailure(1U); break;
        case 8U: TestCallbackFailure(2U); break;
        case 9U: TestRegisterState(); break;
        default: g_device_test.failure_line = __LINE__; break;
    }
    g_device_test.final_state = camera.state;
    return g_device_test.failure_line;
}
