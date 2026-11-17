# Phase4 Camera Module：本地C接口与端口契约

基线9179dde；源码CM7/ApplicationV2/Modules/Camera/{camera_format,camera_module,camera_endpoint}.c/.h。本文是正式授权的软件实现说明，不代表Legacy/BOOT/实板接入授权，不改五核心、Phase0～10路线或原52B/56B ABI。

## 1. 唯一权威与配置

LCContext只由Phase3 Managed Lifecycle写状态；VCamera只拥有不可变配置与一次操作协调记录，不复制running/stopping或Qualification表。CameraCapture/Device是硬件与采集事实权威，Media是物理Frame权威，原Qualification是G权威，MR是G/H/M/准入/退出/T权威。

VCamera_BindCandidate只在首次配置前固定CoreMediaFormat及显式CameraConfig_t转换；同配置重复幂等，不同配置即使Cleanup后也拒绝，本轮无热重配。Candidate必须来自真实Composition校验，candidate_only不是防伪证明；模块检查选中ID/bit/order、非空媒体路由及格式。仅复制Camera配置，不持有Graph或Frame。测试70/71通过真实V2Composition_Validate和EM_BuildPlan/EM_Process接入，不将计算成功叫实际配置成功。

VCamera_Translate先CoreMedia_Validate，再显式映射Y8/RGB565/YUY2/JPEG，然后调用真实Ov5640_ValidateConfig与CameraCapture_ValidateStreamConfig。没有复制160/320/640尺寸权威、FIFO容量或65535-word限制。Raw/JPEG由Core格式互斥选择，QueryCapabilities返回原MRSource_Query能力及可信纯validate_layout结果；后者负责实际Backend payload、DMA/Frame布局约束。缺少真实布局查询不得补成功；Module不引入70000B等并行常量。原物理池/FIFO不变，Graph不Get/Retain/Release。

## 2. 公开操作与完成

Configure/Activate/Deactivate/Cleanup/ErrorProcessing通过VCamera_Request中的LC_CONFIGURE/ACTIVATE/DEACTIVATE/CLEANUP/ERROR_PROCESS提供，完整V2ExecId包含session/request/module/operation。唯一发行者、单调编号、不回绕由Phase3及Client维护；不是Camera Grant session、ACQUIRE或RELEASE编号。

| API | 语义 |
|---|---|
| VCamera_Init | Camera owner一次性零初始化；绑定既有MR Context/Source及可信Ops，无硬件初始化 |
| VCamera_BindCandidate | 冻结配置，仅纯验证；不启动传感器/DCMI |
| VCamera_QueryCapabilities | Camera owner纯查询请求格式能力；真实验证器/Backend端口可否决 |
| VCamera_Request | Round控制阶段受理本地命令，随后LC/端口异步推进 |
| VCamera_BeginRound / Process / Returns / FinishRound | 强制一次合法顺序，重复/乱序拒绝；所有后台轮仍处理归还 |
| VCamera_ReadCompletion / ConfirmCompletion | 匹配冻结LC结果及消费确认；输出不得覆盖Context |
| VCamera_QueryStatus | Lifecycle、配置准备、当前物理观测、历史第一故障及停止失败分别投影，无业务副作用 |
| VCamera_QueryExitEvidence | R完整身份经资格端口确认，再MR_VerifyExit；返回0不直接撤销Qualification |

Configure是模块配置准备，绝不调用Native request，不启动DCMI。Activate只提交Native操作一次；需要真实匹配结果、冻结运行事实、当前Device/Media运行观测以及gate(PROVE)同时成立。Cleanup是V2上层配置准备记录清理，不伪造Device_DeInit；必须经过资源/入口/物理停止证明，最后才清prepared。bound不可变配置保留以便同配置重新Configure。

Deactivate begin只登记意图；VCameraOps.request的STOP契约也只能请求原owner协调，禁止直接CameraCapture_Stop。普通停止唯一落点是ops.finish_round→原App_CameraFinishRound。已dispatch超时不清资源；Native尚未提交的操作超时后不再提交新硬件动作，报告失败。后台停止成功不能改写已冻结IO/原命令完成结果。

Process返回0只表示本轮推进，不表示业务成功；必须读取LCCompletion.result/actual_result/fact。请求入队、模块受理、Native提交、实际完成、Native结果消费、完成入队、Supervisor消费、Camera确认消费分别存在。

## 3. Camera owner轮次与Ops

```mermaid
flowchart LR
 B[BeginRound / R2 stop facts boundary] --> C[consume execution ACK then at most one command]
 C --> L[LC_Process: validate / register operation]
 L --> I[submit native intent once]
 I --> P[Camera Process / immediate protective stop]
 P --> R[Bridge + Storage Return]
 R --> F[FinishRound / sole ordinary stop coordinator]
 F --> O[read exact operation result and prove facts]
 O --> T[freeze LC completion / consume native record]
 T --> Q[reliable reply enqueue and later consumption]
```

Native Ops必须是单一现有Camera资源owner的适配，不是另一硬件控制器。begin_round/process/returns/finish_round映射现有顺序；returns映射App_UvcBridgeCameraProcess，内部已调用App_StorageProcessReturns，不额外重复归还。冷态轮次不能为了STOP查询而初始化传感器。START失败已保护性停止时，既有R2轮次事实负责抑制同轮重复BSP操作；V2不增加Stop调用。

request返回0仅表示可靠提交；BUSY/NO_RESOURCE为未提交无副作用，原身份重试；其他错误也必须无提交副作用。部分Sensor/Capture启动失败必须按已提交记录，再read_result返回真实失败和完整身份。结果成功还需gate(PROVE)，flags本身不是资源证明。read_result出错/错身份保留现场，不重发已提交动作。consume_result只退休已被冻结的Native操作记录，不能改变硬件/Frame。消费背压保留work，不允许Camera确认上层完成后覆盖旧Native结果。

VCameraObservation将Device运行、Media运行、Media停止、Device READY、DMA静止、Bridge Idle、全消费者退出、交接清空分为独立位；另外记录stop_sequence、stop_result、stop_kind、media_result。原生事实必须来自同Camera owner的一致性证明，不用Debug/App_Status或跨任务裸快照拼接。历史first_stop_failure与当前停止结果分别保存；后台成功不会覆盖旧失败。NEVER_STARTED只允许冷态安全证明，不能冒充实际执行过硬件Stop。

所有回调有界、非阻塞、不重入；is_owner必须纯粹验证任务/ISR权限。busy只是防重入，不是锁。Caller存储、Ops、Source、MR Context寿命及互不重叠必须有效；每个物理Capture只装配一个VCamera及Camera Endpoint，每个物理Backend只使用既有唯一MR Context，不能用第二Context绕过关闭。

## 4. Resource、资格和独立退出

模块不创建G、R、H、B、M、S或T替代值；VCameraResources绑定既有Raw/JPEG Source与MR Context。QueryExitEvidence先由release_identity验证完整R关联原G（不是数值比较request ID），然后MR_VerifyExit确认指定G/R。MR回调依旧必须证明实际Media归还、在途交接及Receipt消费退休；未知时返回BUSY/NOT_SUPPORTED/错误。

USB/Storage独立退出核验不检查“全消费者Idle”来替代指定消费者证明；另一个消费者仍持有不被释放。全局Deactivate另经可信gate和所有停止事实确认，最后消费者退出前不许Native finish执行硬件Stop。模块没有调用Qualification RELEASE成功回调；生产released/idle仍为NULL。

测试使用真实MR_AssociateGrant/Stage/Commit/Admit/Obtain/BindBridge/Return/Close/VerifyExit/Receipt API，M来自可控Backend令牌，不用地址猜身份。它证明本地协议，不证明真实Capture/FIFO/G关联或真实USB ACK消费，两个总体NOT_PROVEN保留。

## 5. 双端Endpoint与消息边界

VCameraClient只由Supervisor owner写，含运输端口但没有VCamera/LCContext指针；VCameraEndpoint只由Camera owner写。EMEndpoint的submit/read/consume使用可靠队列端口；Camera端每轮最多取一条控制消息，先处理匹配消费确认，再推进模块轮次。物理队列/锁由未来现有运输适配提供，本库不创建FreeRTOS Queue、Task或修改优先级。

三个逻辑方向是执行请求、冻结完成、消费确认；本地LCRequest24B/LCCompletion28B/VCameraReply32B不是新增wire ABI，不能直接当修改旧52B/56B的许可。实际任务运输编码/权限/静态内存仍待审核。notify仅提示，失败/合并不撤销已入队消息；周期Process仍读取可靠队列。

请求满不绑定新身份；已提交相同请求不重复入队，冲突正文/旧编号拒绝。回复满不重启Sensor/Capture，原结果重试。消费确认满保持Client当前请求；确认成功入队不代表Camera已经消费，Camera确认后LC才允许下一操作。迟到旧回复被排空而不改当前请求；错误消费身份不清当前LC。

非法状态/无法可靠描述的受理拒绝使用delivery_error，Supervisor安全BLOCKED，不伪造稳定完成、不自动取消未知资源。未提供强行Reset恢复入口。此执行完成通道不是Frame Return ACK，也不是C1/C2资格Replay的第二实现。

## 6. 保持未完成的门禁

硬件未接入，Native字段由受信任端口提供。运行中独立Media ERROR即使保护Stop成功，也保留media_result/first_fault；LC ACTIVE表示模块仍被激活处理事件，不能用它声称硬件仍运行。当前Phase3没有独立的异步Fault→ERROR公共转换接口，V2不直接改LC内部状态；需要此自动迁移时须单独审核Phase3扩展及冻结旧完成/新故障的时序。操作失败进入ERROR、显式ERROR_PROCESS的可控软件路径已测，不等于所有真实Device ERROR可自动恢复。

B2 ID23有界BUSY和ID31未退出拒绝START/合法成功START后USB故障复位语义未动，本轮未实现USB模块。具体Legacy Gate/身份/Raw epoch/DMA/ACK证明见适配方案，未通过前不得把Mock成功接入真实资格释放。

Native成功必须由原owner证明完整执行身份对应的那份不可变Native配置已经实际应用；不能只回显请求或仅看运行flags。缺少Applied配置/布局查询时gate必须失败。本库不把配置计算成功当作硬件应用成功。
