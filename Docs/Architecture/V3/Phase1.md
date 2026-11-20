# STM32H747 ApplicationV3 第一阶段

## 基线

V3 独立分支为 `codex/application-v3-flipper`，基线为 `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`。创建前直接查询 GitHub，远端 main 与本地 main 一致，远端尚无同名分支。原清理工作树已有 `v2_types.h`、`.uvprojx`、`.uvoptx` 修改及历史 output，V2 审计工作树为 `11b025327c57c35ccf03eb477251d23bfa256be5`。本阶段在另一个 worktree 内完成。

Flipper 官方固定参考为 `a8a24de3ff394f77d599ea8ba9c073398670c880`。通过官方 Git 端点获取并逐文件审读。Flipper 是唯一应用架构参考。

## 源码来源与取舍

| V3 模块 | 已阅读的 Flipper 对应实现 | 采用方式及 H7 权威 |
| --- | --- | --- |
| App Context / Initcall | `SubGhz` 集中保存 GUI、Notification 等服务引用，`subghz_alloc` 取得这些引用。 [subghz.c](https://github.com/flipperdevices/flipperzero-firmware/blob/a8a24de3ff394f77d599ea8ba9c073398670c880/applications/main/subghz/subghz.c) · [subghz_i.h](https://github.com/flipperdevices/flipperzero-firmware/blob/a8a24de3ff394f77d599ea8ba9c073398670c880/applications/main/subghz/subghz_i.h) | 借鉴单应用 Context，使用编译期服务表。Context 保存两份只读配置引用，服务入口通过表获得；任务私有状态无需登记到 Context。H7 复用现有 Framework Initcall 和 VideoConfig。 |
| Camera / Storage Service | `storage_app_alloc`、`storage_srv`、`storage_process_message` 和 `application.fam` 展示私有应用实例、持久服务线程、消息处理与服务注册。 [storage.c](https://github.com/flipperdevices/flipperzero-firmware/blob/a8a24de3ff394f77d599ea8ba9c073398670c880/applications/services/storage/storage.c) · [storage manifest](https://github.com/flipperdevices/flipperzero-firmware/blob/a8a24de3ff394f77d599ea8ba9c073398670c880/applications/services/storage/application.fam) | 借鉴所属任务持有私有配置、BOOT 后初始化的组织方式。Camera 配置复用真实 CameraCapture 验证器，Storage 借用原录像策略；文件及介质业务留给后续接入原后端。 |
| USB Service / 初始化结果 | `notification_app_alloc`、`notification_srv` 维护私有队列，队列 put 与 `furi_event_flag_wait` 的消费完成各有语义。 [notification_app.c](https://github.com/flipperdevices/flipperzero-firmware/blob/a8a24de3ff394f77d599ea8ba9c073398670c880/applications/services/notification/notification_app.c) · [notification manifest](https://github.com/flipperdevices/flipperzero-firmware/blob/a8a24de3ff394f77d599ea8ba9c073398670c880/applications/services/notification/application.fam) | 借鉴服务轮询、真实消费的区分。UsbTask 只接受 V3 当前 UVC 业务选择。初始化结果走原队列，Control 消费后写原 IPC 投影。阶段一没有 UVC 启动或传输。 |
| Control 任务 / BOOT | `loader_alloc`、`loader_srv` 维护自身消息循环并注册服务。 [loader.c](https://github.com/flipperdevices/flipperzero-firmware/blob/a8a24de3ff394f77d599ea8ba9c073398670c880/applications/services/loader/loader.c) · [loader manifest](https://github.com/flipperdevices/flipperzero-firmware/blob/a8a24de3ff394f77d599ea8ba9c073398670c880/applications/services/loader/application.fam) | 复用原 ControlTask 的物理任务位置和 AppTasks BOOT 屏障，采用单消费者循环。动态加载、GUI 依赖与跨应用在线切换在此阶段无用途。 |
| Event / Queue | `furi_event_loop_run` 用任务所属线程和 `xTaskNotifyWaitIndexed` 分派；`furi_event_loop_subscribe_message_queue` 接入消息；`furi_message_queue_put/get` 调用 FreeRTOS 真实队列；`furi_event_flag_set/wait` 包装 EventGroup。 [event_loop.c](https://github.com/flipperdevices/flipperzero-firmware/blob/a8a24de3ff394f77d599ea8ba9c073398670c880/furi/core/event_loop.c) · [message_queue.c](https://github.com/flipperdevices/flipperzero-firmware/blob/a8a24de3ff394f77d599ea8ba9c073398670c880/furi/core/message_queue.c) · [event_flag.c](https://github.com/flipperdevices/flipperzero-firmware/blob/a8a24de3ff394f77d599ea8ba9c073398670c880/furi/core/event_flag.c) | 采用任务内等待、队列为权威、通知作提示。直接复用 H7 的 AppEvents、AppMessages、RuntimeEvent 和静态 FreeRTOS 队列。Furi 完整循环依赖动态容器、订阅对象和自己的通知索引，移植会重复现有能力。 |
| Record | `furi_record_create/open/close` 提供动态字典、互斥、ready flag、引用持有次数。 [record.c](https://github.com/flipperdevices/flipperzero-firmware/blob/a8a24de3ff394f77d599ea8ba9c073398670c880/furi/core/record.c) · [record.h](https://github.com/flipperdevices/flipperzero-firmware/blob/a8a24de3ff394f77d599ea8ba9c073398670c880/furi/core/record.h) | 主线已具备任务目标身份与静态队列，缺少应用服务名称查找。补充三个只读、以原任务目标为键的记录，固定表直接引用初始化入口。准备与所有权仍由原 BOOT 和任务校验保证。 |

未复制 Flipper 源码、注释、宏、数据结构实现或外部库。直接复用来自本项目的 7 个 Application 编译单元及原有 FreeRTOS、Framework、BSP、Device、Media、IPC。此次 V3 自行实现 6 C、2 H、1 个 Keil 生成脚本，以及 1 C 与 1 Python 的测试。移植后必须遵循的 GPL 分发义务在未来出现实际复制时另行评估。本提交采用架构和调用模式借鉴。 [Flipper LICENSE](https://github.com/flipperdevices/flipperzero-firmware/blob/a8a24de3ff394f77d599ea8ba9c073398670c880/LICENSE)

## 文件与启动路径

```text
CM7/ApplicationV3/
  Core/app_context.h
  Core/app_main.c
  Core/app_services.c/.h
  Services/Camera/camera_service.c
  Services/USB/usb_service.c
  Services/Storage/storage_service.c
  Tasks/service_tasks.c
  generate_keil.py
Tests/ApplicationV3/
  startup_test.c
  run_tests.py
```

原 CM7 main 调用 AppTasks 创建原五个任务位置，沿用 AppEvents、AppMessages 和 IPC。Framework Initcall 调用 V3 App_Init 后记录准备结果。Control 放行既有 BOOT；三个所属任务分别验证固定配置，保存本任务的初始化结果并提交到原 APP_CHANNEL_CONTROL。Control 实际消费后发布状态，保留首次收到的错误。队列满时保留该任务的原报告，已入队的报告即使唤醒失败也会由原 Control 100 ms 周期取出。状态上报使用原 IPC 结构，服务准备完成时不宣称 Camera、UVC 或录像已经运行。

| 状态 | 唯一写入者与含义 |
| --- | --- |
| AppV3Context | Framework Initcall 在 BOOT 前保存只读 VideoConfig 和录像策略引用 |
| 固定服务表 | ROM，三个固定目标与初始化入口 |
| 三份已接受配置指针 | Camera / Usb / Storage 各任务仅写自己一份；只表示软件准备完成 |
| 四个任务的栈、控制块、创建尝试 | 复用原静态规格及 AppTasks 启动次序 |
| BOOT 已见、待发 Result | 各服务任务局部变量，等待实际队列入队 |
| 启动首错 | Control 实际消费后记录，借原 App_StatusPublishControl 发布诊断 |

原 BOOT 事件、52B/56B 消息布局、旧通道容量、IRQ 和任务优先级无变更。Camera、USB、Storage、Control 的栈字数和优先级分别为 1536/3、2048/4、2048/1、768/2。IPC 原实现仍为 512/1。无额外任务、堆分配、通用 Lifecycle、Graph、Resource 或物理 Owner 记录。

## 构建与隔离

在基线副本运行 `python CM7/ApplicationV3/generate_keil.py`。脚本只生成名为 `h7_mac_application_v3.uvprojx` 的独立工程，拒绝覆盖现存文件。选择 `h7_mac_CM7_V3`。生成工程从原 CM7 复制编译器、C99、链接与驱动配置，启用 APP_V3_BUILD，只加入六个 V3 编译单元。原 Legacy 应用业务与四个旧任务的实现从生成的编译集合排除，原工程保持不变。共享源为 app_events、app_messages、app_tasks、app_status、app_camera_lifecycle、video_config 和 ipc_task。其中 app_camera_lifecycle 供保留的日志/原传输接口验证消息；V3 无 Qualification 或 Replay 调度。CM4 留在原工程，生成工程只有 CM7。生成项目、AXF、Map 和测试证据在独立目录保存。

软件专项使用 ARMCC5 编译和链接真实 V3 函数，以 Cortex-M 模拟器执行。使用真实 FreeRTOS Queue/List 和 AppMessages，模拟调度/任务身份、Camera 校验结果与 IPC 出口。覆盖缺配置阻断、Registry/owner、三个初始化成功、故障首错、通知失败、Storage 真实任务入口的队列满与重复 BOOT、Control 真实入口超时取队列。产品 V3 Keil 目标链接真实 Camera 验证函数和 FreeRTOS。模拟启动结果不能代替 MCU 执行。

| 实际 Target | 退出码 | Error / Warning | Code / RO / RW / ZI（B） |
| --- | ---: | ---: | ---: |
| h7_mac_CM7_V3 | 0 | 0 / 0 | 67542 / 2870 / 836 / 95620 |
| h7_mac_CM7（默认 Legacy） | 0 | 0 / 0 | 137690 / 3114 / 1340 / 100932 |
| h7_mac_CM4（原工程） | 0 | 0 / 0 | 26302 / 1438 / 936 / 3576 |

ARMCC5 5.06 update 7。最终专项 O0 和 O2 各 7/7，退出 0，52B / 56B 布局断言同时通过。V3 Map 包含 V3 Initcall、四个任务绑定的服务入口与真实采集验证器、FreeRTOS 队列，未发现旧 Camera 或 UVC Bridge 初始化和 V2 Runtime 符号。默认双核 Map 无 AppV3 符号。V3 体积较小是因为阶段一尚无完整业务，不能当作等功能优化。初次 V3 Rebuild 的枚举初始化警告已在最终源码修正。

[构建结果](D:/32/STM32H7/application-v3-phase1-evidence/build_results.json) · [V3 Map](D:/32/STM32H7/application-v3-phase1-evidence/h7_mac_CM7_V3/h7_mac_CM7_V3.map) · [逐项专项结果](D:/32/STM32H7/application-v3-phase1-evidence/startup_results.json) · [SHA256](D:/32/STM32H7/application-v3-phase1-evidence/SHA256.json)

本阶段 Camera 采集、真实 UVC 传输、SD/AVI 写入、硬件异常恢复和实板启动均为 NOT_RUN。未开发数据管线，未复用 V2 迁移或运行期切换逻辑。

## 后续最小 Camera 工作

下一阶段先在同一个 CameraTask 上承接纯格式验证后的实际 OV5640/CameraCapture Start 和正常 Stop 与完成回报。用一个真实 Camera owner 验证启动失败和停止未完成的处理。USB 与 Storage 的 Frame 交接仍需分别验证实际读取结束和物理归还，后续才可接入。
