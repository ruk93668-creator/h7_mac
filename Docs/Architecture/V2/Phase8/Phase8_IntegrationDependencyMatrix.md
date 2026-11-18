# Phase 8.0 集成依赖矩阵

V2 = `CM7/ApplicationV2/`，Legacy = `CM7/Application/`。“复用”沿用现实现和权威。“适配”需要接入或取证。“审批”表示当前无生产修改授权。

| V2 C/H 或接口 | Legacy 或底层来源 | 数据流和唯一权威 | 接入判定、缺口及风险 |
| --- | --- | --- | --- |
| Core `v2_types.h`、`media_format` | `Common/Inc/camera_config.h`、Legacy `video_config.c` | 中立 ID/格式 → 候选 → 各所有者派生配置 | 复用。显式映射原生枚举。区分采集率、UVC 交付率、Storage 采样率 |
| Configuration `v2_registry`、`v2_composition` | 当前无活跃 V2 catalog，Legacy 以 `video_config.c` 提供默认值 | `V2Resolver_Resolve` 解析提供方及依赖，`V2Composition_Validate` 合并拓扑验证。scratch 单 Control owner | 复用算法，新增板级注册、角色、依赖及 profile 装配。候选独立存储，执行端只接收不可变数据 |
| MediaGraph `media_graph` | `app_frame_dispatcher.c`、UVC Bridge | Entity/Pad/Link/Fan-out → `Graph_ValidateSet`。Graph 无物理帧所有权 | 复用，适配板级拓扑。Raw/JPEG 互斥，JPEG 可 fan-out，Raw 目前单 owner。底层格式验证保留否决权 |
| Core/Runtime `control_service`、`control_transport` | `control_task.c::ControlTask_RequestStart/Stop`、Legacy BOOT、Storage Watch | peer/profile/session/ticket → `CS_Submit/Process/Query/Confirm`，Control owner 保留首错及冻结响应 | 适配 1 个权威入口、Watch/用户命令。须审批旧命令执行与 BOOT 协调失能点。断线仅影响运输 |
| Core/Runtime `control_execution`、`control_execution_link` | `app_events.c`、静态 FreeRTOS 任务 | `CSExecClient_Submit` → 执行 owner 的 `CSExecServer_Process`/EM → 冻结结果 → 消费 ACK | 适配两端有界信箱、通知映射和饱和策略。两个 owner 角色可在同一任务串行化。不能将新结构强塞进 52B/56B |
| Supervisor `execution_plan`、`execution_manager` | 旧 `ControlTask_Process` 和 Camera/USB ACK | `EM_BuildPlan` 建依赖计划，`EM_Load/Process` 执行端口及补偿 | 复用算法，缺真实端口和 owner-local 完成事实。超时而执行结果未知时保持阻塞。不得直接操作硬件 |
| Lifecycle `managed_lifecycle` | 模块任务、真实硬件/文件/资格事实 | `LC_Request → LC_Process/Complete → LC_Read/Confirm`，每模块独占 LC Context | 复用。状态、超时及 ACK 入队均不提供物理释放证明 |
| Camera `camera_format` | `CM7/Device/Src/ov5640_device.c`、`CM7/Media/Src/camera_capture.c` | `VCamera_Translate` 复用 OV5640、CameraCapture 验证，端口追加 Layout 约束 | 复用纯验证，缺真实 SDRAM/DMA 适配。Configure 不启动 DCMI |
| Camera `camera_module`、`camera_endpoint` | `camera_task.c`、`app_camera.c`、Device/Capture/Bridge | `VCameraEndpoint_RunRound` → Begin/Process/Returns/Finish → 完整 V2ExecId 完成 → 消费。CameraTask 唯一调度 | 需 VCameraOps、任务运输和普通 STOP 绑定。保留保护性 STOP 与首错。端点完成消费和底层完成消费分别匹配 |
| USB `usb_format` | `CM7/Service/usb_uvc_class.c`、USB Mode/VideoStreamConfig | Core 格式 → USB 原生配置及底层确认 | 适配能力及 Prepare/Commit 边界。Host UVC Probe/Commit 不能直接替换 Applied |
| USB `usb_module`、`usb_endpoint` | `usb_task.c`、`app_usb.c`、UVC Class/Payload、BSP USB | 命令 → 模式及传输事实 → LC 完成 → 消费，UsbTask 唯一 owner | 适配 VUSBOps 和 Endpoint。区分 USB Device、UVC、资格、数据面。保留首次 STOP 错误及 B2 ID23/31 审批语义 |
| USB `usb_consumer`、`usb_frame` | `App_LifecycleMessagesSend/Receive`、`app_uvc_bridge.c` | G/H 发布 → Camera Get M → 分配 B → Offer → UVC 读取完成或 Abort → Return → 真 Release → 匹配 ACK 消费 → T 退休 | 缺双端元数据同步、真实 ACK 消费回传。复用 52B 运输和既有消费位置，需审批 Bridge 最小适配。SD-only 不占用有效 USB T |
| Storage `storage_format`、`storage_backend` | `StorageFS`、FatFS、`diskio.c`、SDMMC、`mjpeg_avi.c`、`jpeg_frame.c` | 中立配置 → StorageTask 的文件和卷 Ops → 底层 | 复用原实现，缺真实模块能力/handle 绑定。能力由原逻辑唯一给出，不能调用副作用 Offer 作格式查询 |
| Storage `storage_module`、`storage_endpoint` | `storage_task.c`、`app_storage.c` | 录像/抓拍及 STOP → 文件收尾、帧排空、资格释放证明 → LC 回复 | 需 VStorageOps 和真实任务运输。验证 pending START 与 STOP，文件结论和 Frame Return 独立 |
| Storage `storage_consumer`、`storage_frame` | `App_FrameDispatcherPublish`、`App_StorageOfferFrame/ProcessReturns` | Camera 同步 Retain 时绑定 G/S/M，Storage 写完标记 Return，Camera 实际 ReleaseLease 成功才 FREE | 复用槽代次及 PRIMASK 同步规则。缺 G/S/M 同窗口登记和按 G 核验。RETURN_PENDING 不表明归还，槽位适配需审批 |
| Resource `media_source` Raw | `camera_capture.c::CameraStream_GetFrame/ReleaseFrame` | Camera owner 取还，原固定池归 DMA/Media | 复用物理路径，缺只读槽身份、代次、quiet 及指定 G 的 exit_safe。只接受支持的单外部 owner |
| Resource `media_source` JPEG | `camera_mjpeg_fifo.c` Get/Retain/ReleaseLease，`app_uvc_bridge.c` 内部 Get | Camera owner 调度物理操作，FIFO 唯一维护 owner bits，内部/USB/SD 角色分开 | 需审批最小只读 per-owner identity 查询及 G/M 关联。聚合 HasLoan 无法独立确认消费者 |
| Resource `media_resource` | Legacy 资格、Frame Request/Offer/Return、Storage 槽和 ACK 消费 | `MR_AssociateGrant/Stage/Commit/Admit/Close/Obtain/BindBridge/Return/ConfirmConsumed/Retire/VerifyExit` | 复用关联算法。缺真实 MRSourceOps、G/R 绑定、L_admit/L_close/L_verify、T 退休贯通。物理账本仍归 Media。未证明保留 BUSY/BLOCKED |
| Qualification / Replay | `app_camera_qualification.c`、`app_camera_lifecycle*.c`、`app_messages.c` | Camera owner 的 Grant；USB/Storage 各有 Tx，Camera 各有 Rx。独立 R 关联 G.acquire | 重用 56B 和 C2.1.1；适配 CameraTask 在硬件准备前 Prepare、限量接收及可靠回复。released/idle 仍为 NULL，接通需另批准 |
| IPC V3 | `CM7/Service/ipc_service.c`、`Common/Src/ipc_core.c`、`ipc_runtime.c`、`Common/Inc/ipc_shared.h` | 现有双核状态、心跳及诊断 | 直接保留。新 M4 控制协议需另批。IPC、USB、BLE、Wi-Fi、Ethernet 均可作为未来受信传输适配 |
| 工程与启动 | `MDK-ARM/h7_mac.uvprojx`、`main.c`、`app_tasks.c`、`app_events.c` | CM7 选择唯一活动应用，五任务及 BOOT 放行顺序延续 | 登记、任务端口和权威切换分别批准。CM4 不构建 V2，保留 OLED 延时及栈/IRQ/链接设置 |

G = session + consumer + generation + original ACQUIRE。R 的请求身份独立并指向原 G。H 标记交接，B 为旧 Bridge epoch/lease/command，M 为真实 Media 标识，S 为 Storage session/slot/generation，T 为有界终结收据。不得以地址、相等的请求编号数值或 Debug 快照匹配。

ACK 入队 → USB 消费匹配 ACK → Camera 得到可信消费确认 → T 退休。Storage 对应 Camera 实际 ReleaseLease 且匹配 S 转 FREE，文件结果另行传播。Frame ACK 不替代 56B Lifecycle Replay。L_close 后只推进已经受理的 H 排空，offer_pending 不得新增物理 Get/Retain。本设计未证明真实跨任务排空。
