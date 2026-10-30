# Framework 使用说明
基于https://github.com/ruk93668-creator/h7_mac
适用于当前裁减后的独立副本。无 HAL、无 RTOS、无动态内存，初始化使用手动表，不使用自动注册或特殊链接段。

## 1. 当前目录

```text
framework/
├── Inc/
│   ├── framework_init.h       初始化等级、表项和入口
│   ├── framework_runtime.h    主循环调度入口
│   ├── framework_event.h      事件发布、订阅接口
│   ├── framework_timer.h      软件定时器接口
│   └── sys_result.h           通用返回码
├── Src/
│   ├── framework_init.c       按等级执行初始化表
│   ├── framework_runtime.c    先分发事件，再处理定时器
│   ├── framework_event.c      订阅表和事件 FIFO
│   └── framework_timer.c      Timer 对象池和最小堆
└── Port/
    ├── framework_init_table.c  填写目标工程初始化函数
    ├── platform_time.h         毫秒时钟接口
    └── platform_time.c         调用目标板 Board_TimeGetMs
```

## 2. 加入其他单片机工程

添加头文件搜索路径：

```text
framework/Inc
framework/Port
```

加入编译的源文件：

```text
framework/Src/framework_init.c
framework/Src/framework_runtime.c
framework/Src/framework_event.c
framework/Src/framework_timer.c
framework/Port/framework_init_table.c
framework/Port/platform_time.c
```

使用 C99，支持 `stdint.h`、`stdbool.h`。现有 C/H 注释为 GBK 编码，编辑时选择对应编码。

每个固件只加入一份实现；不要与原工程 CM7/CM4 Framework 或同名 `platform_time.c` 重复链接。移植副本不需要额外 Scatter 区域或 `--keep` 参数。

## 3. 接入毫秒时钟

当前调用关系：

```text
Timer → Platform_TimeGetMs() → Board_TimeGetMs()
```

目标板必须提供下面的函数，返回真实的、持续递增的毫秒计数：

```c
#include <stdint.h>

//在板级源文件中提供真实时钟
extern uint32_t Board_ReadMillisecondCounter(void);

//返回当前毫秒计数
uint32_t Board_TimeGetMs(void)
{
    return Board_ReadMillisecondCounter();
}
```

`Board_ReadMillisecondCounter()` 是这里的适配示意，必须替换为目标板已有计数读取函数，或自行实现。STM32 HAL 工程可以包含对应 HAL 头文件后，直接在 `Board_TimeGetMs()` 中返回 `HAL_GetTick()`。

- 时钟单位必须是毫秒，允许 32 位自然回绕。
- 在初始化 Timer 前启动时基；不能返回固定值。
- 8/16 位芯片读取 32 位计数时，需要保证快照一致性。
- 不要为了移植副本重复定义已有的 SysTick 中断入口。

## 4. 填写初始化表

修改 `Port/framework_init_table.c`。下面示例把下一节的 `App_DemoInit()` 放入 APPLICATION 等级：

```c
#include "framework_init.h"
#include <stddef.h>

//由应用模块提供初始化函数
extern SysResult_t App_DemoInit(void);

const FrameworkInitEntry_t g_framework_init_table[] =
{
    {"App_DemoInit", INIT_LEVEL_APPLICATION, App_DemoInit},
    {NULL, INIT_LEVEL_COUNT, NULL}
};

const uint32_t g_framework_init_count =
    sizeof(g_framework_init_table) / sizeof(g_framework_init_table[0]) - 1U;
```

最后的占位项必须保留，计数会排除它。当前默认表只有占位项，因此初始化入口会返回成功，但不会初始化任何模块。

执行等级固定为：

```text
PLATFORM → BUS → DEVICE → MEDIA → SERVICE → APPLICATION
```

每个等级扫描整个表，只执行等级匹配且函数非空的项；同等级按表中顺序执行。某项失败立即返回原错误码，后续项与后续等级不再执行；已经初始化的模块不会自动回滚。非法等级或空函数项会被跳过，填写时自行核对。

## 5. 例子：每 100ms 发布一个事件

以下内容放入目标工程自己的应用源文件。Timer 回调发布事件，事件 Handler 在后续事件分发时处理业务。

```c
#include "framework_timer.h"
#include "framework_event.h"
#include <stddef.h>

#define APP_EVENT_TICK 1U

static FrameworkTimerHandle_t s_timer = FRAMEWORK_TIMER_INVALID_HANDLE;
static uint32_t s_tick_count;
static volatile uint32_t s_handled_count;
static volatile SysResult_t s_post_result;

//处理应用事件
static void App_OnEvent(const FrameworkEvent_t *event);

//定时发布应用事件
static void App_OnTimer(void *context);

//订阅事件并创建周期定时器
SysResult_t App_DemoInit(void)
{
    SysResult_t result;
    FrameworkTimerConfig_t config =
    {
        .period_ms = 100U,
        .mode = FRAMEWORK_TIMER_PERIODIC,
        .callback = App_OnTimer,
        .context = &s_tick_count
    };

    result = Framework_EventSubscribe(APP_EVENT_TICK, App_OnEvent);
    if (result != SYS_OK)
        return result;

    result = Framework_TimerCreate(&config, &s_timer);
    if (result != SYS_OK)
        return result;

    return Framework_TimerStart(s_timer);
}

//保存最近一次处理的计数
static void App_OnEvent(const FrameworkEvent_t *event)
{
    s_handled_count = event->data;
}

//复制事件到队列，队满时记录失败
static void App_OnTimer(void *context)
{
    uint32_t *count = context;
    FrameworkEvent_t event;

    ++(*count);
    event.id = APP_EVENT_TICK;
    event.data = *count;
    s_post_result = Framework_EventPost(&event);
}
```

示例中的 `context` 指向静态变量，Timer 回调期间始终有效。两个 volatile 变量用于调试观察，不代表线程安全。事件入队失败不会自动重试或暂停 Timer，实际应用应根据 `s_post_result` 决定丢弃、重试或上报。

## 6. 主循环接入

在目标板完成必要的系统时钟、时基和硬件基础启动后，执行：

```c
#include "framework_init.h"
#include "framework_runtime.h"

//由目标工程的main调用一次
void App_FrameworkMain(void)
{
    SysResult_t result;

    result = Framework_InitRuntime();
    if (result != SYS_OK)
    {
        //在这里记录并处理初始化失败
        for (;;)
        {
        }
    }

    for (;;)
    {
        Framework_Run();

        //在这里加入其他非阻塞业务轮询
    }
}
```

若原工程已有 `while (1)`，把 `Framework_Run()` 放入现有循环即可，不必再创建第二个主循环。

每轮的实际顺序：

```text
Framework_Run()
    ├── Framework_EventDispatch()  最多取出4个事件
    └── Framework_TimerProcess()   最多处理1个到期Timer
```

因此 Timer 回调发布的事件通常在下一轮分发；事件 Handler 发布的新事件可以在本轮剩余的事件预算内继续处理。一个事件会调用所有匹配的订阅者，所以“4个事件”不等于“4次回调”。

## 7. 常用接口

| 接口 | 用途 |
|---|---|
| Framework_InitRuntime | 按等级执行初始化表，首次失败返回 |
| Framework_Run | 执行一轮事件和 Timer 调度 |
| Framework_EventSubscribe | 注册 ID 与 Handler，相同组合不能重复注册 |
| Framework_EventPost | 复制事件到 FIFO 队尾，队满返回失败 |
| Framework_TimerCreate | 创建 Timer 并返回 Handle，不自动启动 |
| Framework_TimerStart | 启动或重新计时，Active Timer 不重复入堆 |
| Framework_TimerStop | 停止但保留对象及 Handle |
| Framework_TimerDelete | 释放对象，使旧 Handle 失效 |

成功为 `SYS_OK`，失败为负数；结果码不能按位或合并。Delete 成功后，由调用方把保存的 Handle 改为 `FRAMEWORK_TIMER_INVALID_HANDLE`，接口不会自动修改调用方变量。

当前没有事件取消订阅接口。单次 Timer 到期后只是停止，仍占用对象池，需要调用 Delete 才释放。

## 8. 容量与运行限制

| 项目 | 当前值 |
|---|---|
| 订阅表 | 16项 |
| 事件队列 | 16槽 |
| Timer 对象池 | 16槽 |
| Timer 周期范围 | 1～86400000ms |
| 事件每轮预算 | 4个事件 |
| Timer 每轮预算 | 1个到期对象 |

- 所有 Event/Timer 公共接口在同一个主循环上下文调用，不支持 ISR、多任务并发调用或递归执行 Framework_Run。中断可先保存板级标志，再由主循环发布事件。
- 回调同步执行，不得长时间阻塞；实际响应时间受主循环负载影响。
- 周期 Timer 在处理到期时先从当前时间重新计时，再执行回调，不补发错过的周期，也不保证无漂移。
- 多个 Timer 同时到期，需要多轮处理。Tick 回绕比较要求及时轮询，不能停顿超过半个 32 位计数周期后才检查。
- 事件结构按值复制，但 `data` 只有 uint32_t；不自动复制它所代表的对象或缓冲区。64位平台不要把指针直接塞入 data。
- Handler 的 event 指针只在当前回调期间有效，需要长期保存时复制内容。
- 静态变量依赖启动代码正常清零；初始化入口不清空事件队列或 Timer 池，不用于重复软复位框架。
- 双核使用时各自编译自己的实例，Timer Handle 和内部队列不跨核共享。

## 9. 移植后检查

1. 确认 Board_TimeGetMs 有实际实现且每毫秒递增。
2. 确认初始化表最后占位项与 count 正确，失败时停止后续初始化。
3. 创建单次1000ms Timer，确认到期只回调一次；再验证周期 Timer 的启动、停止、删除。
4. 发布事件，确认数据及订阅 Handler 对应，检查队满返回值。
5. 查看目标芯片 RAM/Flash 占用和主循环延迟，确认资源预算满足要求。

本目录没有附带测试工程。上述步骤需要在目标单片机上验收，不能把原 H747 的验证结果直接视为新平台通过。
