# C2.2A 产品接口拆分审计

## 1. 基线与结论

源码基线为清理分支的 3b940c6cfb76866fc7e24064477335dd26ca4f8f，上一归档输入为祖先 6e36b01。核查时本地 main 为 f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb，origin 清理分支的本地缓存为 6e36b01。初次实时查询及普通推送超时，正常重试成功推送 3b940c6。实时远端查询确认清理分支为 3b940c6cfb76866fc7e24064477335dd26ca4f8f，main 为 f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb。工作区另有用户的 uvprojx、uvoptx 和 output，所有证据以只读方式使用。

结论为**可以把旧 C2.2A 服务移出产品工程，实施需单独授权和历史夹具适配**。CM7 镜像已剔除旧服务的收件、执行、回复和状态入口，旧适配仍嵌在 Legacy Camera 的上下文及 START/STOP 记账中。app_camera_qualification.c 同时提供 V2 两个消费者实际引用的代次分配函数，整文件删除会切断共享符号。当前产品仍需 Legacy Camera 采集模块、原业务 ACK/FAULT、V2 消费者 Tx 与 Resource 路径。两项资源证明继续为 NOT_PROVEN。

证据分三层。源码用于识别显式调用、函数指针、条件编译和测试依赖。HEAD 工程与工作区 XML 分别用于核对登记。链接可达性来自上轮隔离构建生成的 CM7 Map。已逐一比对该隔离构建源码中的 app_camera.c、app_camera_qualification.c、camera_task.c、v2_rtos_runtime.c、usb_consumer.c、storage_consumer.c 与本轮源码，六文件内容一致。Map 位于 output/LegacyLifecycleArchiveValidation/v155618/candidate/MDK-ARM/h7_mac_CM7/h7_mac_CM7.map，SHA256 为 44ebe485bab0afaf589816cd52488a6cc7d48a2b227c11354b8db778b802cb66。Map 的输入段交叉引用也包含后来被剔除的代码，必须结合 Removing 和最终 Section/Symbol 表判断。本轮未重编、未运行测试，用户本地修改的 Target 未重新链接。

## 2. 当前旧资格调用链与状态所有权

旧服务定义于 CM7/Application/app_camera_qualification.c。App_CameraQualificationPrepare、Receive、Process、Flush、GetStatus（537–580）经 Qual_AppAccess（528）核对非 ISR 和 Camera 消息所有者，再经 App_CameraQualificationAccess（app_camera.c:626）取得 Camera 私有 qualifications 与 s_qualification_ops。首次 Prepare 要求尚未配置、准备、初始化、运行或停止。App_QualificationInit、Receive、Process、Flush、GetStatus 管理每消费者独立状态和原 Rx 窗口。Qual_Execute 使用回调执行 QUERY、ACQUIRE、RELEASE、STOP_IF_IDLE，Qual_Window、Qual_Decide、Qual_Reply、Qual_Submit 保存完整事务身份与 DECIDED/BLOCKED，继续使用 App_LifecycleRx 的完成和运输语义。旧 Ops 由 App_CameraResourceState、App_CameraQualificationStart、App_CameraRequestStop、App_CameraReadStopResult、App_CameraConsumeStopResult 构成，released 和 idle 均为 NULL。该适配无法提供完整退出证明。

CM7 运行时没有对这些入口的调度。CameraTask 在 BOOT 后分别循环驱动现有 Legacy Camera 路径和 V2Runtime_Round，后者没有注册这些旧 Ops。Map 5440 剔除 App_CameraQualificationAccess，5668–5687 剔除核心、适配入口和私有处理函数。归档夹具直接调用核心、旧 Camera 适配或经测试 Ops 注入，因此归档测试仍依赖这条完整调用链。历史调用均为历史源码/夹具中的调用，以该证据判断迁移所需覆盖。

| 单独核查的接口或辅助职责 | 实际调用与处理建议 |
|---|---|
| App_CameraStartResource | App_CameraHandleCommand 的正常 START 与旧 App_CameraQualificationStart 共用。保留真实 Prepare、BridgeCameraBegin、InitStream、失败回退，只分离旧资格 preparing/stop-confirmed 写入。 |
| App_CameraQualificationAccess | Qual_AppAccess 取得旧对象和 Ops。本轮产品无可达调用。移入历史适配，不开放生产私有 Camera Context。 |
| App_CameraQualificationPrepare / Receive / Process / Flush / GetStatus | 旧适配入口。逐项迁出至历史测试构建边界，保留对应语义。 |
| App_CameraRequestStop / ReadStopResult / ConsumeStopResult / StopIdentityMatches | 旧 STOP_IF_IDLE 专用完整身份与结果。历史夹具仍需旧请求受理、BUSY、结果冻结和按身份消费。 |
| App_CameraResourceState / App_CameraQualificationStart | 旧 Ops 的硬件状态投影和启动包装。生产相同硬件功能从独立的 Legacy 业务入口可达。 |
| App_CameraAdvanceStop / FinishRound / GetPendingAck / ConfirmAckEnqueued / GetPendingFault / ConfirmFaultEnqueued | 继续负责 Legacy 普通停止、52B 原业务 ACK/FAULT、状态和统计。只拆 FinishRound 的旧资格 STOP 收据逻辑。 |
| App_QualificationNextGeneration | 保留共享定义及 app_camera_generation.h 的声明。USB 与 Storage 分别维护调用者自己的计数，检查 NULL、别名和 UINT32_MAX，成功后单调推进。没有旧服务上下文依赖。 |

## 3. Legacy Camera 数据面和停止

CM7/Application/Tasks/camera_task.c 的 CameraTask_Process（127 起）依序 BeginRound → 尝试送既有回复并接收允许的 52B 控制命令 → App_CameraHandleCommand → App_CameraProcess → App_UvcBridgeCameraProcess → App_CameraFinishRound，随后继续状态和回复交付。START 走 App_CameraStartResource → App_CameraPrepare / App_UvcBridgeCameraBegin / App_CameraInitStream → App_CameraStart → Device_Start / CameraCapture_StartStream。失败回退仍以轮内停止事实防重复。普通 STOP 由 App_CameraHandleCommand 登记，FinishRound 调 App_CameraAdvanceStop。AdvanceStop 检查 Media 停止事实和本轮是否已经尝试，待 Media 完全停止后调用 Device_Stop。FinishRound 再核验 Bridge Idle 才允许确定 STOP 成功，保留 BUSY 等待、原失败 ACK、独立 FAULT 和后台恢复。Media ERROR 的立即保护仍由 CM7/Media/Src/camera_capture.c 的 CameraCapture_Process 推进，旧资格回调只登记意图。三条停止边界和 Bug B1 ID9、ID10 均不得因拆分而改写。

Legacy USB 数据面由 app_uvc_bridge.c 的 Camera owner 侧取得真实 Frame（220、243），Bridge_CameraRelease（456）负责 CameraCapture_StreamReleaseFrame，真实 Release 之后才推进原 Return ACK。Legacy Storage Offer 在 app_storage.c:282 保留独立 JPEG lease，归还由 Camera 路径在 357 执行 CameraMjpegFifo_ReleaseLease。FrameDispatcher 与 Bridge 协调投递、回收和再试。USB、SD 路径均不借旧资格服务取得或释放实际物理 Frame。App_CameraInit、App_CameraStop、App_CameraGetLastResult 即使在当前产品 Map 被剔除，仍是历史或其他 Legacy 入口，超出 C2.2A 删除范围。

## 4. V2 的真实引用与未接入部分

| 路径 | 源码和实际边界 |
|---|---|
| Runtime 与任务绑定 | CameraTask、UsbTask、StorageTask 和 ControlTask 分别调用 V2Runtime_Round。CM7/ApplicationV2/Core/Runtime/v2_rtos_runtime.c:213–249 绑定诊断 Camera/BSP 事实、no_lifecycle_send/read、no_release 与其他拒绝数据端口。真实 P8.2 任务运输与 Legacy 硬件数据面仍隔离。 |
| USB Endpoint → 身份 → Tx | Modules/USB/usb_endpoint.c 推进完整 Lifecycle 请求与完成身份。usb_consumer.c 的 VUSBConsumer_Begin（48 起）调用 App_QualificationNextGeneration（56），以现有 Tx 绑定 QUERY/ACQUIRE/RELEASE 并匹配回复。只有真实匹配 ACQUIRE 成功才形成 MRGrant。usb_frame.c 的 VUSBFrame_Process 推进 G/H/B/M、读者结束、Return、收据消费与退休。V2 P8.2 运行端口没有连接旧 Camera 资格 Rx 或 Legacy 真 Return Backend。 |
| Storage Endpoint → 身份 → Tx | Modules/Storage/storage_endpoint.c 推进任务请求/完成。storage_consumer.c 的 VStorageConsumer_Begin 调同一纯代次函数（51），维护独立计数并按现有 Tx 匹配真实回复。storage_frame.c 使用独立槽位身份关联真实 Media loan，文件收尾、读者结束、Return、终结收据消费分开。当前诊断端口尚无真实 Backend。 |
| V2 Camera / Resource | Modules/Camera 的 camera_endpoint.c 与 camera_module.c 使用 Managed Lifecycle；VCamera_QueryExitEvidence 在有合法 R 与资源证据时调用 MR_VerifyExit。它不读取旧 AppCameraQualification_t。Grant → Frame Lease 及 Stable Admission Closure 均为 NOT_PROVEN。 |

CM7 Map 3981、4135 显示两个 V2 Begin→代次函数，8851/9697 将该函数保留为 40 B Thumb 代码。Endpoint 的命令完成被消费、C1/C2 客户端的协议回复、物理 Return ACK 与 Media Resource 收据是独立事件。此次拆分不会把已有 V2 客户端变成可用的真实资格服务器，也不能以这些事件替代 DMA/帧归还证明。

## 5. Keil、结构和 ABI

| 审计项 | 影响与约束 |
|---|---|
| Target 登记 | HEAD CM7 登记 app_camera.c 和 app_camera_qualification.c，HEAD CM4 也登记 CM7 app_camera.c。工作区用户 XML 又在 CM4 登记了 app_camera_qualification.c 及生命周期相关编译单元。登记是编译输入，不代表运行可达。需要核对选项、最终链接和双核日志后才可更改文件路径。建议首轮保留生成器原编译单元路径，用户工程配置原样保留。 |
| app_camera.c 私有对象 | Map 8389 中 Legacy s_camera 为 988 B，和两个同名 V2 对象无关。可候选迁出 qualifications、resource_preparing、resource_stop_confirmed、stop_identity、stop_result、stop_result_valid 六个专用字段及 s_qualification_ops。保留 configured/prepared/initialized/running/stopping、原 ACK/FAULT、round_* 停止事实、诊断与统计。不能由现有 Map 推导拆分后节省量。 |
| 旧资格类型 | AppCameraQualification_t、AppQualificationEntry_t、AppQualificationDecision_t、AppQualificationStatus_t、AppQualificationStopResult_t、AppQualificationResourceOps_t 和相应布局断言可随历史服务迁出。旧对象的两个 Rx 实例属于该服务。AppLifecycleRxContext_t 类型与实现、共享身份布局均需保留。 |
| ABI 与运输 | 原 AppMessage_t 52 B 和 Legacy 静态消息队列仍在使用，队列深度 4。56 B AppLifecycleMessage_t、C1 协议、C2.1.1 Replay 的编号、退休高水位、候选接替不变。归档 C1 运输在 Tests/LegacyLifecycle/Source，测试深度 4。P8.2 的九条 V2 Endpoint 队列深度 2，仍按三个模块分别使用 Request、Completion、Consumed；不迁移、不合并、不复用两种原协议队列。 |
| 兼容 | 无已知理由改变 V2 消费者的 Begin、独立计数或回复匹配。不得因旧资格服务迁出重写 generation 或 Replay。重复、旧 generation、耗尽与原拒绝路径都需保留。 |

## 6. 后续最小实施计划（待批准）

1. Tests/LegacyLifecycle/C2.2A 与 R2 先建立测试专用旧服务、私有适配和原 Ops 的编译边界。保留原测试入口、ID、断言和失败记录。在从生产私有上下文解耦之前，证明 R2 仍驱动真实 Legacy FinishRound/Media 停止与故障，避免把旧适配全部换成没有真实停止证据的 stub。必要时保留一份带明确历史提交的冻结 fixture，用独立当前产品测试证明 Legacy 当期行为；明确两种证据的范围。
2. CM7/Application/app_camera.c 只迁出本报告列出的资格回调、Ops 表、私有 Access、六个字段和 START/FinishRound 中相应旧资格记账；保留 CameraTask 调用链及 Legacy API。app_camera.h 移掉旧入口声明和不再必要的资格 include；保留原业务公开能力。app_camera_qualification_internal.h 和 app_camera_qualification.h 的旧接口和断言进入历史夹具后才可退出产品头文件集合，产品不得反向 include Tests。
3. CM7/Application/app_camera_qualification.c 首选保留原编译单元路径，只留下完全相同的 App_QualificationNextGeneration 实现及所需 include。app_camera_generation.h 保留。若未来要重命名共享编译单元，另行批准登记变更并同步核对两个 Target，禁止覆盖用户的工作区 Keil 配置。
4. CM7/Application/Tasks、CM7/ApplicationV2、app_messages、app_camera_lifecycle、app_camera_lifecycle_replay、Media、BSP、Device、M4 及任务配置均无需配合本次拆分改动。先验证两个 Target 及宿主链接，再判断归档后是否还有旧服务段或不必要的局部状态。P8.3 不属于此次拆分。

## 7. 回归门禁与剩余风险

| 门禁 | 后续验收判据及本轮证据 |
|---|---|
| 归档 C2.2A/R2 | O0/O2 原 ID、顺序、断言与退出码比对；上轮原专项 162/162、74/74。本轮未复跑。 |
| C1/C1.1/C2.1.1 | 保留 419/24/648 上轮证据；实施后复验所有原 ID、52B/56B 大小偏移、无重放策略差异。 |
| B1/B1 Media/B2/B3 与 P8.2 | 校验 START 回退、Media STOP 失败、Bridge BUSY、设备仅在 Media 完全停止后停止、ACK/FAULT 首错、原队列和三模块 Endpoint。B1 固定组合 f2da385 保留 37/39、ID9/ID10 FAIL、退出码 1。 |
| 已有历史问题 | 当前夹具中的 R2 run_original、run_layout 有 V2Runtime_Round 未链接；run_regression 有既有失败套件；audit_scope 比较旧生产白名单。原证据及退出语义维持，实施验收需区分新增失败和历史限制。详见 LegacyFixtureValidationReport.md。 |
| 编译与资源 | ARMCC5 双核 Rebuild 0 Error/0 Warning，按实际 Map 对照共享符号、旧映射、RO/RW/ZI 与 Callgraph，单列用户本地 Keil 差异。当前仅核验旧 Map，真实拆分后数值尚未知。 |
| 实际硬件 | 真 Backend、DMA/ACK 消费与独立退出证据尚待实板/集成；两项 NOT_PROVEN 均保留。 |

最大风险是将旧资格适配与 Legacy 普通停止一起删除，或删除 V2 当前依赖的代次符号。其次是归档测试改用无实际业务的替身、用户 CM4 工程登记与仓库基线差异。逐符号位置、传递关系、测试引用和建议见 C2_ProductInterfaceAuditMatrix.csv。上述实施尚未开始。
