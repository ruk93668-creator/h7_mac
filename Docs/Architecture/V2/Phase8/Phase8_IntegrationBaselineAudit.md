# Phase 8.0 工程集成基线审计

## 基线及证据

| 项目 | 核对结果 |
| --- | --- |
| 仓库、分支 | `ruk93668-creator/h7_mac`，`codex/architecture-media-graph-lifecycle` |
| Phase 7 远端输入 | `a4f2c8a125c4b8d6595ae2d3ee56cd88ce96b111` |
| 本地审计 HEAD | `4fe50277ae31acc864e44ff90ae4a618638cbf85`，直接后继为本轮前序的注释、命名和负对照脚本定位整理。V2 54 个 C/H 的 token 和变量绑定关系已核对，27 个 C 单元两种优化共 54 对 ARMCC5 目标的可装载段、重定位和外部符号一致 |
| main 本地及远端 | `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb` |
| 保留中的用户内容 | `MDK-ARM/h7_mac.uvprojx`、`MDK-ARM/h7_mac.uvoptx` 有外部改动，`output/` 未跟踪。本报告核对实际工作区及已提交工程设置 |
| 前序构建 | 隔离快照中，CM7/CM4 Legacy Target 各为 0 Error、0 Warning。V2 独立 ARMCC5 编译和模拟执行另有结果。以上均早于 Phase 8.0 文档提交 |

源码确认、建议集成设计和未来验收分别记录。真实 Grant → Frame Lease 与 Stable Admission Closure 均为 **NOT_PROVEN**。原 R3 72 项与 Phase 2 新增 32 项沿用既有映射和真实 NOT_RUN 记录，不能将两个集合直接相加。Phase 8.0 验收矩阵是集成门禁，未执行的项目保持 NOT_RUN。

## 启动路径

1. `MDK-ARM/startup_stm32h747xx_CM7.s` 的 `Reset_Handler` 调用 `SystemInit`，进入 ARM C 运行时 `__main`，再到 `CM7/Core/Src/main.c::main`。
2. CM7 配 MPU，启动并等待 CM4 进入 D2 STOP。随后 `HAL_Init`、时钟、HSEM、`BSP_DCMI_EarlyInit`、`BSP_Camera_EarlyInit`、`IPC_Service_EarlyInit`，CM7 通过 HSEM0 放行 CM4，确认 D2 唤醒后配置 SDRAM MPU。在调度器前后分别保持各自的初始化调用约束。
3. `App_TasksCreate` 在调度器前一次执行，依次准备事件、旧消息队列及 Camera、USB、Control、IPC、Storage 的静态 TCB、栈、通知注册。失败会阻止继续创建。
4. `Framework_InitRuntime` 从 scatter 的 `ER_FW_INIT` 逐级执行 Platform → Bus → Device → Media → Service → Application，遇错停止。`App_Init` 仅检查 USB 模式，`App_LifecycleMessagesInit` 同级准备三条资格通道。`StorageFS_Init` 属于 Service。初始化阶段的任务创建、队列注册和真实模块启动是三个步骤。
5. `App_TasksRecordInitialization` 检查五个任务齐备，将结果交给事件层。成功才到 `Platform_RtosStart`，后者检查任务数、上下文、NVIC 优先级分组，调用 `vTaskStartScheduler`。意外返回走错误处理。
6. `ControlTask_Run` 经 `App_TasksReleaseBoot → App_EventsReleaseBoot` 放行 BOOT。事件层只允许实际 Control 任务在调度器运行时放行，并合并预先登记的通知。其它任务先等待 BOOT。默认 UVC 下旧 Control 发送 Camera START，匹配成功 ACK 后发送 USB START。USB TEST 入口直接发 USB START。Storage 在 BOOT 只调用 `App_StoragePrepare`，录像、抓拍另由命令触发。`IpcTask` BOOT 后开始现有状态和心跳。

源码定位：`CM7/Application/app_tasks.c`、`app_events.c`、`app_main.c`，`CM7/Framework/framework_init.c`，`CM7/Platform/platform_rtos.c`，各任务在 `CM7/Application/Tasks/`。

## 任务、状态权威及消息

| 任务 | 唯一职责及执行顺序 | 输入、输出与节奏 |
| --- | --- | --- |
| CameraTask | `app_camera.c` 的运行与硬件协调由 CameraTask 推进。 `BeginRound → Reply → 至多一条旧命令 → App_CameraProcess → App_UvcBridgeCameraProcess → FinishRound → Reply`。Bridge 首先处理 Storage Return。普通 STOP 在 FinishRound 推进，START 失败回退与 Media ERROR 保护按原事实记账 | `APP_CHANNEL_CAMERA_COMMAND`、`TO_CAMERA`，输出匹配 ACK/FAULT 及 USB Offer/Return ACK。DCMI 通知与超时均可推进，BOOT 前阻塞，之后 20 ms。1536 words，优先级 3 |
| UsbTask | `App_USBHandleCommand/App_USBProcess/App_USBFinishRound`、UVC、Bridge USB 侧；底层 IRQ 报告匹配事件，任务推进传输及停止 | `USB_COMMAND`、`TO_USB`，输出 ACK/FAULT、Frame Request/Cancel/Return；SOF/传输通知，BOOT 后 20 ms 周期兜底。2048 words，优先级 4 |
| StorageTask | 命令和文件/卷收尾由 StorageTask 独占；Camera 侧按槽代次操作真正的 SD Lease Return，可读写规定的共享槽位阶段 | 现有 Storage 事件和有界命令/槽位队列，向 Camera 通知待归还，BOOT 后 100 ms 兜底。2048 words，优先级 1 |
| ControlTask | 旧 BOOT、START/STOP 串行编排与 ACK/FAULT 协调；控制事务状态不拥有媒体 | `APP_CHANNEL_CONTROL` 每轮最多四条，输出 Camera/USB Command；100 ms 等待，活动命令超时 5000 ms，首错单独保留。768 words，优先级 2 |
| IpcTask | `IPC_Service_SetRuntimeOwner` 注册唯一 IPC 推进任务，汇总 `App_Status` 稳定快照、公布状态和心跳 | BOOT 前阻塞，之后每 100 ms，512 words，优先级 1。与 CM4 共享布局不提供控制事务 |
| V2 Control / Execution | Control owner 唯一持有 `CSService` 与 Composition scratch。Execution owner 维护 EM 计划及端口；模块 owner 单独推进 LC 和事实。两个角色可以在同一已有任务中串行承担，但需明确调度契约 | `CSExecClient/Server` 与模块 Endpoint 的 Ops 是对接点；当前五个真正任务没有装载 V2 Context 或循环 |

旧 52B AppMessage 是五条各四项静态队列，56B AppLifecycleMessage 是三条各四项静态队列（Camera/USB/Storage）。`App_MessagesSend/Receive` 和 Lifecycle 版本检查方向、所有者和运行上下文。消息本体才是交付证据，`xTaskNotify eSetBits` 是可合并的唤醒提示。资格初始化允许启动前 BASEPRI 非零，运行时访问规则独立。资格 Tx/Rx 按完整身份、单未完成事务、单调 issued/high-water、候选接替工作。保留 `App_LifecycleReplayRetire` 的禁止成功路径；DECIDED 只冻结结果，完整身份消费确认与 RxFlushReply 的运输背压分别处理。

关键当前事实：`App_CameraQualificationPrepare/Receive/Process/Flush` 和 Tx/Rx 都已存在，但 Legacy CameraTask 没有调用这些资格阶段，也没有消费 56B Camera 通道。 `app_camera.c::s_qualification_ops` 的 released、idle 仍为 NULL。保持安全阻塞，直到资源证据实际实现且审核通过。USB/Storage 的 Legacy START 仍不经 V2 Consumer。旧 Watch 候选包括 `app_storage.c` 的 Watch 命令字段以及 `ControlTask_RequestStart/Stop`，现有代码有上下文限制。具体用户调试脚本不在当前源码证据中。切换后 Watch 只能转换为受控请求，不能直接写业务私有状态。

## Keil 登记

检查 `MDK-ARM/h7_mac.uvprojx` 的 Group、File、IncludeInBuild 和 Target 字段，并对照提交树。CM7 有 19 个活动 Legacy Application C，V2 的 **27 个 C 和 27 个 H 均无登记**，没有 V2 include path。CM4 工作区可见 19 个 CM7 Legacy C 条目，全部禁用，提交版可见 15 个同类禁用条目。两版 CM4 均无 V2。能否编译以实际 IncludeInBuild 和完整链接 Map 判定。

两 Target 都是 ARM-ADS、ARMCC 5.06 update 7 build 960、`uAC6=0`、`uC99=1`、`Optim=1`、`wLevel=2`。CM7 include path 包含 Application/Service/Device/Media/Platform/BSP/Framework、OV5640、USB Core、FreeRTOS RVDS CM7、FatFs。CM7 预包含 SDRAM、DCMI、USB、SDMMC HAL 配置，当前 Target 定义 `APP_VIDEO_MJPEG_MODE=1`，`video_config.c` 默认 320×240 JPEG/UVC，Raw 对照为 160×120 YUV422。CM4 使用自身 Core/Application/Device/Platform/BSP/Framework。保留现有 `h7_mac_CM7_custom.sct`、`h7_mac_CM4_custom.sct`、MPU、宏与生成物策略。CM7 scatter 包含 Flash `0x08000000`、DTCM RW/ZI `0x20000000` 128 KiB、共享 UNINIT `0x38000000`、SDMMC AXI UNINIT `0x24000000` 32 KiB，以及 initcall/device 段。容量最终以集成 Map 为准。

前序兼容构建 CM7 为 Code 137690、RO-data 3114、RW-data 1340、ZI-data 100932，CM4 分别为 26350、1438、936、3576。它们是 Legacy 快照数据，不是 V2 接管预算。原队列净载荷 1040 + 672 B，未含控制结构及对齐。五业务任务静态栈合计 6912 words。集成时还要单独核算命令/完成/消费队列、Context、每个物理源关联表、栈高水位及使用中的 SDRAM；缺少真实 Map 和板级峰值时不得通过容量门禁。

## DMA 和读者结束条件

| 读者或写入者 | 源码事实 | 未来适配必须提供的证据 |
| --- | --- | --- |
| DCMI DMA M0/M1 | `bsp_dcmi.c::BSP_DCMI_ReplaceTarget/BSP_DCMI_Stop` 操作 DMA1 Stream0。 `camera_capture.c::CameraStream_OnFrameComplete/Rotate` 仅在替换成功后重新发布完成缓冲。停止需看真实停止事实，允许 DRAINING 中归还 | 关联本次采集代次、已完成槽及替换结果。硬件停稳需查实际 DMA/采集状态。错误或超时保留在途持有，不用一个 callback 替代整个停机证明 |
| Raw Frame | `CameraStream_t` 在 `camera_capture.c` 内有七槽固定流池、双 DMA target、ready 和单 in_use。Get 取得活动读者，Release 退回槽 | 真实槽身份、代次（不得从地址推导），只允许受支持的单 owner。重复和错误 owner 拒绝，槽在读者退出并经 Media 归还后方可再次成为 DMA 目标 |
| JPEG FIFO Block | `CameraMjpegFifo_GetFrame/RetainFrame/ReleaseFrame/ReleaseLease`、`Mjpeg_ReleaseLoan` 维护同一物理块的 usb_owned、sd_owned 与块依赖。内部 Get 在 SD-only 也建立原始持有，再给 SD retain | 两个 owner 独立；内部 Get 标记为内部借用，避免冒充 USB Grant。两 span 均受保护。哪个 owner 归还由实际成功和只读查询证明；最终块复用还需满足原解析器和 DMA 条件 |
| USB UVC | `usb_uvc_payload.c::USB_UVC_PayloadStartView/BuildPacket/CompletePacket` 保留一或两段原帧供后续分包。`usb_uvc_class.c::UVC_DataIn` 报告 token，任务推进完成或取消。 `bsp_usb.c` 当前 `dma_enable=DISABLE`，PIO 分包到 endpoint staging | 端点匹配完成或 Abort 确认、Payload 无原帧引用、已停止后续复制读取，然后才能 Return。只看 USB 枚举、Disconnect 或首包完成不能推出退出。未来零拷贝另需逐读取者 DMA 围栏 |
| Storage SDMMC IDMA | `StorageFS_Write → f_write → diskio → BSP_SDMMC` 经 CPU 复制，`HAL_SD_WriteBlocks_DMA` 使用专用 AXI `g_sdmmc_dma_buffer`。 `Sdmmc_WaitBatch`、IDMA/DPSM 及恢复管理该缓冲 | 证明对原帧的一/两 span 复制结束且无后续文件回调读原帧；IDMA 的 AXI staging 生命周期独立保留。文件 Finish/Sync/Close/Unmount 不证明 M 已释放。不得用 Abort 请求或超时释放仍被读的帧 |
| CPU 复制 | 分包及 Storage 写入都会读原帧，且上层可保留 view 指针 | 逐项记录该操作的最后一次读取及不会再读的状态，核对目标缓冲物理非重叠。PRIMASK 或 DMB 只解决并发发布顺序，不能证明 DMA 不再访问 |

## 接管边界

Control Service 唯一业务请求入口，Composition 生成候选，Supervisor 按 Resolver 实际依赖及批准的运行状态组织计划，Execution Manager 执行计划及基于事实的补偿。它们均不得操作 DCMI、USB、SDMMC 或替媒体记账。CameraTask 保留物理采集和媒体归还调度权，UsbTask 和 StorageTask 分别保留设备/文件权威。LC 状态只能由对应 Endpoint owner 用真实完成事实推进。选择 V2 后旧 BOOT 编排、Watch 直投及任何第二生命周期路径都必须被排除。IPC V3 原心跳/状态仍独立，M4 新控制路径需要另行授权。

当前阻塞正式接管的是活跃 Keil 登记与 RTOS 端口、唯一 BOOT/STOP 权威、G/R/H/B/M/S/T 的真实贯通、完整 Offer/Retain 关闭屏障、USB 匹配 ACK 消费确认、按消费者核验及 DMA/文件读者证据。V2 合成测试和旧业务兼容构建只能分别支撑各自结果。

## V1 板级基线补录

上文记录原 Phase 8.0 的审计快照。后续工程集成以已核对远端的 `a3cc70cbd7e55253c076fbf5f9241155e53c7e01` 为 V1 输入。与原审计提交 `96868a0` 相比，生产差异仅为 `CM7/BSP/bsp_camera.c` 和 `CM7/BSP/bsp_camera.h`，与已评审的 `8b87a7a` 两个文件逐字节一致。没有变更 CM4、Keil 工程、V2、Legacy RTOS 调度、Media 或 USB。GitHub 核对时 main 为 `f0ce45f`。

- `BSP_Camera_EarlyInit` 在 CM4 放行前配置 PA15 和 PB8/PB9。PA15 为 U10 的高有效 CAM_DVDD_EN，配置输出前预装低电平。PB8 / PB9 分别为 I2C1 SCL / SDA（AF4 开漏）。现有 PH13 PWDN、PH15 RESETB 和 PA8 XCLK 沿用。I2C1 初始化校验共享时钟源，不改 CM4 OLED 所用 I2C2 的时钟选择。
- V1 电源软件专项 14/14，I2C1 软件专项 24/24。用户已确认编译通过。隔离 ARMCC5 完整构建 CM7、CM4 各为 0 Error、0 Warning，构建源 SHA 为 `a3cc70c`。CM7 Code/RO/RW/ZI 为 139702/3114/1344/100992 B，CM4 为 26350/1438/936/3576 B。与上一节原 Target 的差量，CM7 为 +2012/0/+4/+60 B，CM4 为 0/0/0/0 B。它们仍是运行 Legacy 的 Target 数据。
- 本地证据在 `output/V1ArchitectureIntegration/build_20261118_184835/results.json` 及其两核日志，AXF/Map 哈希已复算。`.uvprojx` 构建前后哈希一致。构建脚本的外部文件总哈希检查报告 `.uvoptx` 在构建期间变化，XML 对照只有 9 个 `tvExp` 工程树展开值。
- 这版固件的断电冷启动、PA15 及 U10 1.5 V、实际 I2C1 事务、OV5640 ID `0x5640`、Legacy USB UVC 出图仍待实板确认。既往其他提交的 V1 实板记录不能替代该镜像证据。两个资源证明保留 **NOT_PROVEN**，验收矩阵仍适用于未来集成 Target。
