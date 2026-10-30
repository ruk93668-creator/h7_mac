# Framework 独立移植副本

复制自当前 CM7 Framework；原工程源码、Keil 配置和运行流程不变。本目录不加入原工程编译。

## 目录

| 目录 | 作用 |
|---|---|
| Inc | Event、Timer、Runtime、Init 接口，以及 List、SysResult |
| Src | 初始化、事件 FIFO、Timer 最小堆和主循环调度 |
| Port | 目标板毫秒时钟适配、显式初始化表 |
| Ports/ARMCC5 | 原工程自动注册实现及 Scatter 片段，可选 |
| Examples | main 接入示例 |
| Tests | 独立编译与模拟执行测试，不加入固件 |

Event、Timer、Runtime 和基础头文件原样复制，来源与 SHA256 见 source_manifest.json。没有复制 IPC、USB、相机、HAL 或设备业务。

## 默认移植：显式初始化表，C99

1. 添加 `Inc`、`Port` 到 Include Path。
2. 编译 `Src` 下四个 `.c` 和 `Port` 下两个 `.c`。不要递归添加 Tests、Examples 或 Ports 下的文件。
3. 目标板实现 `uint32_t Board_TimeGetMs(void)`，由 Port/platform_time.c 转交给 Timer。返回真实单调毫秒计数，允许 uint32_t 自然回绕，不能返回恒定值。8/16 位芯片读取 32 位计数时，需要在板级实现中保证读取一致性。
4. 修改 `Port/framework_init_table.c`，在最后占位项之前添加初始化函数及必要声明，例如：

```c
    {"Board_Init", INIT_LEVEL_PLATFORM, Board_Init},
    {"App_Init", INIT_LEVEL_APPLICATION, App_Init},
    {NULL, INIT_LEVEL_COUNT, NULL}
```

5. main 完成必要的启动配置后调用 `Framework_InitRuntime()`，检查结果，再持续调用 `Framework_Run()`。参考 Examples/main_example.c。

默认初始化表为空，不会初始化硬件。此方式不用 `FRAMEWORK_INIT_REGISTER`，不依赖 Scatter、GNU 链接脚本或 ARMCC 特有符号。该宏仅在下面的可选模式启用，避免移植后注册项被静默忽略。

初始化等级：PLATFORM → BUS → DEVICE → MEDIA → SERVICE → APPLICATION。同等级按表顺序执行，首次失败立即返回原错误码，后续任务不再执行。

## 可选：ARMCC5 自动注册

与默认方式二选一，不能混编两份 Framework_InitRuntime：

- 不编译 `Src/framework_init.c`、`Port/framework_init_table.c`。
- 改为编译 `Ports/ARMCC5/framework_init.c`，其余三个 Src 文件和 Port/platform_time.c 保留。
- 所有注册模块定义 `FRAMEWORK_USE_ARMCC5_INITCALL`，继续使用原有 `FRAMEWORK_INIT_REGISTER`。
- 把 `Ports/ARMCC5/framework_init.sct.inc` 合入目标板的 Flash 加载区，不照搬 H747 地址。
- 链接选项加入 `--keep=*(.framework_init)`，Map 中核对 ER_FW_INIT 的 Base/Limit 与注册表项。

GCC/IAR 的自动注册链接段暂未适配，使用默认显式表即可；相关编译器和目标板尚未实测。

## 使用约束

- 协作式、单实例、静态资源，无 RTOS、无 malloc。不是抢占式任务调度器。
- 同一主循环上下文调用 Event/Timer 公共接口，不支持 ISR 或多个任务并发访问，没有增加锁。
- 每轮先分发最多 4 个事件，再处理最多 1 个到期 Timer；回调同步执行，应短且非阻塞。
- 事件队列 16 槽、订阅表 16 项、Timer 池 16 槽，容量沿用原实现。
- 周期 Timer 从处理时刻重新计时，不保证无漂移。最长周期 86400000ms，必须及时轮询，不能停顿超过半个 32 位 Tick 周期后才检查。
- 事件 data 是 uint32_t 值，不能自动管理对象生命周期；64 位平台不能直接用它保存指针。
- Timer Handle 仅在当前实例有效，不跨核、不持久化重启后复用。
- 启动代码必须正常清零静态变量。Framework_InitRuntime 不是 Event/Timer 的复位接口。
- C/H 保留 GBK 编码，编译器需支持 C99、stdint、stdbool。不同单片机仍需确认 RAM 容量、时钟精度和长回调延迟。

## 验证

执行 `python framework/Tests/run_tests.py`，使用本机 ARMCC5 编译该副本，在 Unicorn 执行真实 ARM 指令。测试覆盖通用表及 ARMCC5 自动注册两种初始化方式、失败短路、事件、Timer、回绕、旧 Handle 和链表。

运行工具依赖当前工程已有的本机 Python 测试库，不属于嵌入式移植依赖。结果及实际命令见 Tests/results.json、Tests/commands.json。模拟通过不代表其他单片机实板通过。
