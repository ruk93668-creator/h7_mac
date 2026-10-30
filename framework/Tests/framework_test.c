#include "framework_init.h"
#include "framework_runtime.h"
#include "framework_timer.h"
#include "framework_event.h"
#include "framework_list.h"
#include <stddef.h>

static uint32_t s_tick;
static uint32_t s_case;
static uint32_t s_order[8];
static uint32_t s_count;
static uint32_t s_calls;
static uint32_t s_event_value;
volatile uint32_t g_checks;

#define CHECK(x) do { ++g_checks; if (!(x)) return __LINE__; } while (0)

//模拟目标板毫秒时钟
uint32_t Board_TimeGetMs(void)
{
    return s_tick;
}

//记录平台初始化顺序
static SysResult_t Test_PlatformInit(void)
{
    s_order[s_count++] = INIT_LEVEL_PLATFORM;
    return SYS_OK;
}

//模拟总线初始化失败
static SysResult_t Test_BusInit(void)
{
    s_order[s_count++] = INIT_LEVEL_BUS;
    return s_case == 1U ? SYS_ERR_IO : SYS_OK;
}

//记录应用初始化顺序
static SysResult_t Test_AppInit(void)
{
    s_order[s_count++] = INIT_LEVEL_APPLICATION;
    return SYS_OK;
}

#if defined(FRAMEWORK_USE_ARMCC5_INITCALL)
FRAMEWORK_INIT_REGISTER(Test_AppInit, INIT_LEVEL_APPLICATION);
FRAMEWORK_INIT_REGISTER(Test_BusInit, INIT_LEVEL_BUS);
FRAMEWORK_INIT_REGISTER(Test_PlatformInit, INIT_LEVEL_PLATFORM);
#else
const FrameworkInitEntry_t g_framework_init_table[] =
{
    {"app", INIT_LEVEL_APPLICATION, Test_AppInit},
    {"bus", INIT_LEVEL_BUS, Test_BusInit},
    {"platform", INIT_LEVEL_PLATFORM, Test_PlatformInit}
};
const uint32_t g_framework_init_count = 3U;
#endif

//记录事件回调与载荷
static void Test_Event(const FrameworkEvent_t *event)
{
    ++s_calls;
    s_event_value = event->data;
}

//记录定时器回调并确认上下文
static void Test_Timer(void *context)
{
    if (context == &s_tick)
        ++s_calls;
}

//供模拟器调用，每个用例使用全新静态内存
int Framework_TestRun(uint32_t id)
{
    FrameworkTimerHandle_t handle;
    FrameworkTimerConfig_t config = {100U, FRAMEWORK_TIMER_ONE_SHOT, Test_Timer, &s_tick};
    FrameworkEvent_t event = {1U, 42U};
    FrameworkListNode_t head;
    FrameworkListNode_t node;
    uint32_t i;
    s_case = id;

    if (id <= 1U)
    {
        CHECK(Framework_InitRuntime() == (id ? SYS_ERR_IO : SYS_OK));
        CHECK(s_count == (id ? 2U : 3U));
        CHECK(s_order[0] == INIT_LEVEL_PLATFORM);
        CHECK(s_order[1] == INIT_LEVEL_BUS);
        if (!id) CHECK(s_order[2] == INIT_LEVEL_APPLICATION);
    }
    else if (id == 2U)
    {
        CHECK(Framework_EventSubscribe(1U, Test_Event) == SYS_OK);
        CHECK(Framework_EventSubscribe(1U, Test_Event) == SYS_ERR_ALREADY_EXISTS);
        for (i = 0U; i < 5U; ++i) CHECK(Framework_EventPost(&event) == SYS_OK);
        Framework_Run();
        CHECK(s_calls == 4U && s_event_value == 42U);
        Framework_Run();
        CHECK(s_calls == 5U);
    }
    else if (id == 3U || id == 4U)
    {
        if (id == 4U) s_tick = 0xFFFFFFF0U;
        CHECK(Framework_TimerCreate(&config, &handle) == SYS_OK);
        CHECK(Framework_TimerStart(handle) == SYS_OK);
        CHECK(Framework_TimerStart(handle) == SYS_OK);
        s_tick += 99U;
        Framework_Run();
        CHECK(s_calls == 0U);
        ++s_tick;
        Framework_Run();
        CHECK(s_calls == 1U);
        s_tick += 100U;
        Framework_Run();
        CHECK(s_calls == 1U);
    }
    else if (id == 5U)
    {
        FrameworkTimerHandle_t old;
        CHECK(Framework_TimerCreate(&config, &old) == SYS_OK);
        CHECK(Framework_TimerDelete(old) == SYS_OK);
        CHECK(Framework_TimerCreate(&config, &handle) == SYS_OK);
        CHECK(old != handle);
        CHECK(Framework_TimerStart(old) != SYS_OK);
        CHECK(Framework_TimerStart(handle) == SYS_OK);
        CHECK(Framework_TimerStop(handle) == SYS_OK);
        s_tick = 100U;
        Framework_Run();
        CHECK(s_calls == 0U);
    }
    else if (id == 6U)
    {
        config.mode = FRAMEWORK_TIMER_PERIODIC;
        CHECK(Framework_TimerCreate(&config, &handle) == SYS_OK);
        CHECK(Framework_TimerStart(handle) == SYS_OK);
        s_tick = 100U;
        Framework_Run();
        CHECK(s_calls == 1U);
        s_tick = 200U;
        Framework_Run();
        CHECK(s_calls == 2U);
    }
    else if (id == 7U)
    {
        Framework_ListInit(&head);
        Framework_ListInit(&node);
        CHECK(Framework_ListIsEmpty(&head));
        Framework_ListAddTail(&head, &node);
        CHECK(!Framework_ListIsEmpty(&head));
        CHECK(node.prev == &head && node.next == &head);
        Framework_ListRemove(&node);
        CHECK(Framework_ListIsEmpty(&head));
        CHECK(node.prev == &node && node.next == &node);
    }
    return 0;
}
