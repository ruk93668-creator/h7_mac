/* Isolated ARM test translation unit: production source is included unchanged.
 * No test hooks, exports, or initialization changes are added to the framework.
 * Each case runs in a fresh emulator, including zero-initialized static storage.
 */
#include "framework_timer.h"
#include <stdbool.h>

static uint32_t mock_now;
uint32_t Platform_TimeGetMs(void) { return mock_now; }

/* ARM compiler ABI stub for structure/constant-array copies, no C runtime. */
void __aeabi_memcpy4(void *destination, const void *source, unsigned size)
{
    volatile unsigned char *out = (volatile unsigned char *)destination;
    const volatile unsigned char *in = (const volatile unsigned char *)source;
    while (size != 0U) { *out++ = *in++; size--; }
}

#include "../../CM7/Framework/framework_timer.c"

typedef struct {
    uint32_t failure_line;
    uint32_t invariant_checks;
    uint32_t callback_count;
    uint32_t callback_order;
    uint32_t pool_count;
    uint32_t heap_size;
    uint32_t old_handle;
    uint32_t new_handle;
    SysResult_t action_result;
    uint32_t first_callback_tick;
} TestResult;

volatile TestResult g_test_result;
static uint32_t callback_ids[64];
static uint32_t callback_ticks[64];
static FrameworkTimerHandle_t action_handle;
static uint32_t callback_action; /* 0=none, 1=Stop, 2=Start, 3=Delete. */
static uint32_t action_calls;

static bool Check(bool condition, uint32_t line)
{
    if (!condition && g_test_result.failure_line == 0U)
        g_test_result.failure_line = line;
    return condition;
}
#define CHECK(condition) do { if (!Check((condition), __LINE__)) return; } while (0)

/* Independent ordering oracle for intervals less than half the tick range. */
static bool OracleEarlier(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) < 0;
}

static void Invariants(void)
{
    uint32_t allocated = 0U, active = 0U, seen = 0U, i, index;
    g_test_result.invariant_checks++;
    CHECK(s_heap_size <= TIMER_POOL_MAX);
    CHECK(s_timer_pool_count <= TIMER_POOL_MAX);
    for (i = 0U; i < s_heap_size; i++) {
        index = s_timer_heap[i];
        CHECK(index < TIMER_POOL_MAX);
        CHECK((seen & (1UL << index)) == 0U);
        seen |= 1UL << index;
        CHECK(s_timer_pool[index].allocated);
        CHECK(s_timer_pool[index].active);
        CHECK(s_timer_pool[index].heap_index == i);
        if (i != 0U) {
            CHECK(!OracleEarlier(s_timer_pool[index].deadline_ms,
                s_timer_pool[s_timer_heap[(i - 1U) / 2U]].deadline_ms));
        }
    }
    for (i = 0U; i < TIMER_POOL_MAX; i++) {
        FrameworkTimer_t *timer = &s_timer_pool[i];
        if (timer->allocated) {
            allocated++;
            CHECK(timer->generation > 0U);
            CHECK(timer->generation <= TIMER_HANDLE_GEN_MASK);
        }
        if (timer->active) {
            active++;
            CHECK(timer->allocated);
            CHECK(timer->heap_index < s_heap_size);
            CHECK(s_timer_heap[timer->heap_index] == i);
            CHECK((seen & (1UL << i)) != 0U);
        } else {
            CHECK((seen & (1UL << i)) == 0U);
            /* Never allocated slots start with heap_index=0 in BSS. */
            if (timer->allocated || timer->generation != 0U)
                CHECK(timer->heap_index == TIMER_HEAP_INDEX_INVALID);
        }
    }
    CHECK(allocated == s_timer_pool_count);
    CHECK(active == s_heap_size);
}

static void Callback(void *context)
{
    uint32_t id = (uint32_t)(uintptr_t)context;
    uint32_t pos = g_test_result.callback_count;
    CHECK(pos < 64U);
    callback_ids[pos] = id;
    callback_ticks[pos] = mock_now;
    if (pos == 0U) g_test_result.first_callback_tick = mock_now;
    g_test_result.callback_count++;
    g_test_result.callback_order = g_test_result.callback_order * 10U + id;
    Invariants();
    if (id == 1U && callback_action != 0U && action_calls == 0U) {
        action_calls++;
        if (callback_action == 1U)
            g_test_result.action_result = Framework_TimerStop(action_handle);
        else if (callback_action == 2U)
            g_test_result.action_result = Framework_TimerStart(action_handle);
        else
            g_test_result.action_result = Framework_TimerDelete(action_handle);
        CHECK(g_test_result.action_result == SYS_OK);
        Invariants();
    }
}

static FrameworkTimerHandle_t Create(uint32_t period,
                                     FrameworkTimerMode_t mode, uint32_t id)
{
    FrameworkTimerConfig_t config;
    FrameworkTimerHandle_t handle = FRAMEWORK_TIMER_INVALID_HANDLE;
    config.period_ms = period;
    config.mode = mode;
    config.callback = Callback;
    config.context = (void *)(uintptr_t)id;
    Check(Framework_TimerCreate(&config, &handle) == SYS_OK, __LINE__);
    Invariants();
    return handle;
}

static void Start(FrameworkTimerHandle_t handle)
{
    CHECK(Framework_TimerStart(handle) == SYS_OK);
    Invariants();
}

static void Process(uint32_t tick)
{
    mock_now = tick;
    Framework_TimerProcess();
    Invariants();
}

static void TestOrder(void)
{
    FrameworkTimerHandle_t a, b, c;
    a = Create(300U, FRAMEWORK_TIMER_ONE_SHOT, 1U);
    b = Create(100U, FRAMEWORK_TIMER_ONE_SHOT, 2U);
    c = Create(200U, FRAMEWORK_TIMER_ONE_SHOT, 3U);
    Start(a); Start(b); Start(c);
    Process(99U); CHECK(g_test_result.callback_count == 0U);
    Process(100U); CHECK(g_test_result.callback_count == 1U);
    Process(199U); CHECK(g_test_result.callback_count == 1U);
    Process(200U); CHECK(g_test_result.callback_count == 2U);
    Process(299U); CHECK(g_test_result.callback_count == 2U);
    Process(300U); CHECK(g_test_result.callback_count == 3U);
    CHECK(g_test_result.callback_order == 231U);
    CHECK(callback_ticks[0] == 100U && callback_ticks[1] == 200U && callback_ticks[2] == 300U);
    Process(1000U); CHECK(g_test_result.callback_count == 3U);
    CHECK(s_timer_pool_count == 3U && s_heap_size == 0U);
}

static void TestDeletePosition(uint32_t position)
{
    const uint32_t periods[7] = {700U,100U,600U,200U,500U,300U,400U};
    FrameworkTimerHandle_t handles[7];
    uint32_t i, heap_pos, deleted, expected_count = 0U;
    for (i = 0U; i < 7U; i++) { handles[i] = Create(periods[i], FRAMEWORK_TIMER_ONE_SHOT, i + 1U); Start(handles[i]); }
    heap_pos = position == 0U ? 0U : (position == 1U ? 2U : s_heap_size - 1U);
    deleted = s_timer_heap[heap_pos];
    mock_now = 50U;
    CHECK(Framework_TimerDelete(handles[deleted]) == SYS_OK);
    Invariants();
    CHECK(s_timer_pool_count == 6U && s_heap_size == 6U);
    CHECK(!s_timer_pool[deleted].allocated && !s_timer_pool[deleted].active);
    CHECK(s_timer_pool[deleted].heap_index == TIMER_HEAP_INDEX_INVALID);
    for (i = 100U; i <= 700U; i += 100U) {
        uint32_t j;
        Process(i);
        for (j = 0U; j < 7U; j++) {
            if (periods[j] == i && j != deleted) {
                CHECK(callback_ids[expected_count] == j + 1U);
                CHECK(callback_ticks[expected_count] == i);
                expected_count++;
            }
        }
        CHECK(g_test_result.callback_count == expected_count);
    }
    CHECK(expected_count == 6U && s_heap_size == 0U);
}

static void TestRestartActive(void)
{
    FrameworkTimerHandle_t a = Create(100U, FRAMEWORK_TIMER_ONE_SHOT, 1U);
    FrameworkTimerHandle_t b = Create(150U, FRAMEWORK_TIMER_ONE_SHOT, 2U);
    uint32_t tick;
    Start(a); Start(b);
    for (tick = 1U; tick <= 50U; tick++) {
        mock_now = tick; Start(a);
        CHECK(s_heap_size == 2U && s_timer_pool_count == 2U);
    }
    Process(100U); CHECK(g_test_result.callback_count == 0U);
    Process(149U); CHECK(g_test_result.callback_count == 0U);
    Process(150U); Process(150U);
    CHECK(g_test_result.callback_count == 2U);
    CHECK(callback_ticks[0] == 150U && callback_ticks[1] == 150U);
    CHECK(s_heap_size == 0U);
}

static void TestCallbackAction(uint32_t action, FrameworkTimerMode_t mode)
{
    FrameworkTimerHandle_t peer;
    callback_action = action;
    action_handle = Create(100U, mode, 1U);
    peer = Create(150U, FRAMEWORK_TIMER_ONE_SHOT, 2U);
    Start(action_handle); Start(peer);
    Process(100U);
    CHECK(action_calls == 1U && g_test_result.callback_count == 1U);
    CHECK(callback_ticks[0] == 100U);
    CHECK(s_heap_size == (action == 2U ? 2U : 1U));
    CHECK(s_timer_pool_count == (action == 3U ? 1U : 2U));
    Process(150U); CHECK(g_test_result.callback_count == 2U && callback_ids[1] == 2U);
    Process(199U); CHECK(g_test_result.callback_count == 2U);
    Process(200U);
    CHECK(g_test_result.callback_count == (action == 2U ? 3U : 2U));
    if (action == 2U) CHECK(callback_ids[2] == 1U && callback_ticks[2] == 200U);
    CHECK(s_heap_size == ((action == 2U && mode == FRAMEWORK_TIMER_PERIODIC) ? 1U : 0U));
    if (action == 3U) CHECK(Framework_TimerStart(action_handle) == SYS_ERR_NOT_FOUND);
    Invariants();
}

static void TestGeneration(void)
{
    FrameworkTimerHandle_t old_handle = Create(100U, FRAMEWORK_TIMER_ONE_SHOT, 1U);
    FrameworkTimerHandle_t new_handle;
    Start(old_handle);
    CHECK(Framework_TimerDelete(old_handle) == SYS_OK); Invariants();
    new_handle = Create(200U, FRAMEWORK_TIMER_ONE_SHOT, 2U);
    g_test_result.old_handle = old_handle; g_test_result.new_handle = new_handle;
    CHECK(Timer_HandleGetIndex(old_handle) == Timer_HandleGetIndex(new_handle));
    CHECK(Timer_HandleGetGeneration(new_handle) == Timer_HandleGetGeneration(old_handle) + 1U);
    CHECK(Framework_TimerStart(old_handle) == SYS_ERR_NOT_FOUND); Invariants();
    CHECK(Framework_TimerStop(old_handle) == SYS_ERR_NOT_FOUND); Invariants();
    CHECK(Framework_TimerDelete(old_handle) == SYS_ERR_NOT_FOUND); Invariants();
    CHECK(s_timer_pool_count == 1U && s_heap_size == 0U);
    Start(new_handle); Process(199U); CHECK(g_test_result.callback_count == 0U);
    Process(200U); CHECK(g_test_result.callback_count == 1U && callback_ids[0] == 2U);
}

static void TestRollover(void)
{
    FrameworkTimerHandle_t a, b, c;
    mock_now = 0xFFFFFF80U;
    a = Create(300U, FRAMEWORK_TIMER_ONE_SHOT, 1U);
    b = Create(100U, FRAMEWORK_TIMER_ONE_SHOT, 2U);
    c = Create(200U, FRAMEWORK_TIMER_ONE_SHOT, 3U);
    Start(a); Start(b); Start(c);
    Process(0xFFFFFFE3U); CHECK(g_test_result.callback_count == 0U);
    Process(0xFFFFFFE4U); CHECK(g_test_result.callback_count == 1U && callback_ids[0] == 2U);
    Process(0xFFFFFFFFU); Process(0U); CHECK(g_test_result.callback_count == 1U);
    Process(0x47U); CHECK(g_test_result.callback_count == 1U);
    Process(0x48U); CHECK(g_test_result.callback_count == 2U && callback_ids[1] == 3U);
    Process(0xABU); CHECK(g_test_result.callback_count == 2U);
    Process(0xACU); CHECK(g_test_result.callback_count == 3U && callback_ids[2] == 1U);
    CHECK(g_test_result.callback_order == 231U && s_heap_size == 0U);
}

static void TestCapacityLifecycle(void)
{
    FrameworkTimerHandle_t handles[TIMER_POOL_MAX], extra;
    FrameworkTimerConfig_t config;
    uint32_t i;
    for (i = 0U; i < TIMER_POOL_MAX; i++) handles[i] = Create(100U + i, FRAMEWORK_TIMER_ONE_SHOT, i + 1U);
    CHECK(s_timer_pool_count == TIMER_POOL_MAX && s_heap_size == 0U);
    config.period_ms = 100U; config.mode = FRAMEWORK_TIMER_ONE_SHOT;
    config.callback = Callback; config.context = 0;
    CHECK(Framework_TimerCreate(&config, &extra) == SYS_ERR_NO_RESOURCE);
    CHECK(extra == FRAMEWORK_TIMER_INVALID_HANDLE); Invariants();
    for (i = 0U; i < TIMER_POOL_MAX; i++) Start(handles[i]);
    CHECK(s_heap_size == TIMER_POOL_MAX);
    for (i = 0U; i < TIMER_POOL_MAX; i += 2U) {
        CHECK(Framework_TimerStop(handles[i]) == SYS_OK); Invariants();
    }
    CHECK(s_heap_size == 8U && s_timer_pool_count == 16U);
    for (i = 1U; i < TIMER_POOL_MAX; i += 2U) {
        CHECK(Framework_TimerDelete(handles[i]) == SYS_OK); Invariants();
    }
    CHECK(s_heap_size == 0U && s_timer_pool_count == 8U);
    for (i = 1U; i < TIMER_POOL_MAX; i += 2U) handles[i] = Create(100U + i, FRAMEWORK_TIMER_ONE_SHOT, i + 1U);
    for (i = 0U; i < TIMER_POOL_MAX; i++) Start(handles[i]);
    for (i = 100U; i < 116U; i++) Process(i);
    CHECK(g_test_result.callback_count == 16U && s_heap_size == 0U && s_timer_pool_count == 16U);
    for (i = 0U; i < TIMER_POOL_MAX; i++) {
        CHECK(callback_ids[i] == i + 1U);
        CHECK(Framework_TimerDelete(handles[i]) == SYS_OK); Invariants();
    }
    CHECK(s_heap_size == 0U && s_timer_pool_count == 0U);
}

uint32_t Test_Run(uint32_t case_id)
{
    Invariants();
    switch (case_id) {
        case 0U: TestOrder(); break;
        case 1U: TestDeletePosition(0U); break;
        case 2U: TestDeletePosition(1U); break;
        case 3U: TestDeletePosition(2U); break;
        case 4U: TestRestartActive(); break;
        case 5U: TestCallbackAction(1U, FRAMEWORK_TIMER_ONE_SHOT); break;
        case 6U: TestCallbackAction(2U, FRAMEWORK_TIMER_ONE_SHOT); break;
        case 7U: TestCallbackAction(3U, FRAMEWORK_TIMER_ONE_SHOT); break;
        case 8U: TestCallbackAction(1U, FRAMEWORK_TIMER_PERIODIC); break;
        case 9U: TestCallbackAction(2U, FRAMEWORK_TIMER_PERIODIC); break;
        case 10U: TestCallbackAction(3U, FRAMEWORK_TIMER_PERIODIC); break;
        case 11U: TestGeneration(); break;
        case 12U: TestRollover(); break;
        case 13U: TestCapacityLifecycle(); break;
        default: Check(false, __LINE__); break;
    }
    Invariants();
    g_test_result.pool_count = s_timer_pool_count;
    g_test_result.heap_size = s_heap_size;
    return g_test_result.failure_line;
}
