# Framework 独立移植副本

复制自当前 CM7 Framework；原工程源码、Keil 配置和运行流程不变。本目录不加入原工程编译。

## 目录

| 目录 | 作用 |
|---|---|
| Inc | Event、Timer、Runtime、Init 接口，以及 List、SysResult |
| Src | 初始化、事件 FIFO、Timer 最小堆和主循环调度 |
| Port | 目标板毫秒时钟适配、显式初始化表 |
| Examples | main 接入示例 |

Event、Timer、Runtime 和基础头文件原样复制，来源与 SHA256 见 source_manifest.json。没有复制 IPC、USB、相机、HAL 或设备业务。

## 默认移植：显式初始化表，C99

1. 添加 `Inc`、`Port` 到 Include Path。
2. 编译 `Src` 下四个 `.c` 和 `Port` 下两个 `.c`。不要把 Examples/main_example.c 与目标工程已有的 main 同时编译。
3. 目标板实现 `uint32_t Board_TimeGetMs(void)`，由 Port/platform_time.c 转交给 Timer。返回真实单调毫秒计数，允许 uint32_t 自然回绕，不能返回恒定值。8/16 位芯片读取 32 位计数时，需要在板级实现中保证读取一致性。
4. 修改 `Port/framework_init_table.c`，在最后占位项之前添加初始化函数及必要声明，例如：

```c
    {"Board_Init", INIT_LEVEL_PLATFORM, Board_Init},
    {"App_Init", INIT_LEVEL_APPLICATION, App_Init},
    {NULL, INIT_LEVEL_COUNT, NULL}
```

5. main 完成必要的启动配置后调用 `Framework_InitRuntime()`，检查结果，再持续调用 `Framework_Run()`。参考 Examples/main_example.c。

默认初始化表为空，不会初始化硬件。所有模块通过显式表登记，不依赖链接段、Scatter 或编译器自动注册宏。

初始化等级：PLATFORM → BUS → DEVICE → MEDIA → SERVICE → APPLICATION。同等级按表顺序执行，首次失败立即返回原错误码，后续任务不再执行。

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

本副本不附带测试目录；移植到其他单片机后，需要验证目标板时钟、初始化顺序及实际调度行为。
