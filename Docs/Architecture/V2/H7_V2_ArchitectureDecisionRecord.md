# Application V2 — Architecture Decision Record

## 状态说明

本轮允许研究和设计，不允许生产实施。以下ADR统一状态为 **UNRESOLVED — 推荐方案待人工批准**，不是已采纳的生产决策。用户已经批准的安全原则作为约束输入，不代表具体字段、API、布局、目录、状态机或编译切换获批。外部源码版本和链接详见OpenSourceArchitectureStudy，H7基线为8a03596/main f0ce45f。

## ADR-01：Graph采用const描述，不担任控制器

参考Linux MC的Entity/Pad/Link/Pipeline、link_validate和失败回滚。推荐保留原模型，H7用固定表和有界校验；不采用Linux动态对象、图计数控制硬件，也不只用Zephyr Device接口代替跨模块拓扑。原因是产品路由静态、无需运行期拓扑热插拔。ROM主要是描述/名称，RAM仅查询/校验workspace；无Graph Task，不进入DMA ISR。具体大小见预算，未编译测量。

未来USB/Storage多sink由一source分支表达；Ethernet/Wi-Fi/AI可增加逻辑节点，是否实现由能力标志验证。增加Link不产生消费者资格或帧引用，未来节点不可被误激活。

### R2深化审核定位（仍UNRESOLVED）

首轮已完成于cd8daa6；第二轮输入cd8daa6，生产源码仍为研究分支实际源码（除既有M4延时外与main f0ce45f相同）。人工认可R2-01 Source Pad显式fan-out、R2-02固定Raw/JPEG Pipeline；R2-03仅原则认可。后续人工已通过01/02/04/05/07/08，06有条件通过，最新澄清见ModuleContract第1.17节。**ADR-01仍UNRESOLVED**，本轮推荐不构成接口/容量或代码实施批准。

第二轮精确契约见[ModuleContract第1.12～1.16节](H7_V2_ModuleContract.md)，并同步第1.2～1.11节的结构/示例/预算：

| 议题 | 推荐与未决边界 |
|---|---|
| R2-04 | CameraCapture统一Capture Entity封装耦合DCMI/DMA；Raw/JPEG Provider仅能力视图，非新Pool；不采用独立硬件启停节点。依据BSP_DCMI_Init/StartContinuous/Stop与CameraCapture_StartStream/Stop的共同handle和停止顺序 |
| R2-05 | FrameRoute纯逻辑Graph节点；USB经Bridge、Storage经Camera端交接适配/Resource端口；不建立另一运行时Dispatcher、引用计数或Ledger |
| R2-06 | 有条件通过。Storage纯Validator拟接受CoreMediaFormat，不能直接依赖CameraConfig_t；GraphFormat别名不另建类型副本。Ports转换旧配置；真实Offer与预检共用唯一能力源，不能用Offer/Prepare试探 |
| R2-07 | 10FPS稳定YUY2基线为用户确认；25FPS只是目标/待验证。格式合法、配置成功、实测性能分级记录，Graph不证明FPS |
| R2-08 | 一个用户配置快照派生Graph媒体值、各Sink速率策略、Camera/UVC/Storage配置；Commit只选择已广告能力，不直接改Camera。当前一次配置API不支持通用热重配，相关API和排空证明仍待审 |

接口修订理由：共享GraphFormat移出FPS（草案56B），独立GraphSinkPolicy记录各Sink目标（12B），避免USB交付率与Storage采样率被错误绑定。Ops分媒体/终端配置验证；这是未实施V2本地类型调整，不改变52B/56B消息ABI。新主表10 Entity/14 Pad/10 Link/7 Pipeline，保留16/32/32/8上限。预测const描述2108B、含名称≤2652B；最坏8请求同时保留RAM1468B，全部待ARMCC5/Map证实。

这些取舍是H7基于Linux MC思想的本地适配，不是要求照搬Linux实体划分。Graph不分配Frame、不Retain/Release、不启动硬件；两个Resource NOT_PROVEN不变。R2-04/05/07/08设计方向已通过，R2-06依赖澄清及具体新接口、布局、预算仍等待人工复核，不授权Phase1/C2.2C。

### R2最终复核记录（输入c52490b，不改变ADR状态）

| 项 | 独立结论 | 设计/证据状态 |
|---|---|---|
| R2-09 | 唯一CoreMediaFormat/Graph别名，Storage纯能力只依赖Core；Camera/Frame/Lease适配留Ports，70000B同一来源 | DESIGN_PROPOSED修订完成；API实现IMPLEMENTATION_REQUIRED |
| R2-10 | Source Capture、USB Delivery、Storage Sampling分别配置/统计/验收，10输出基线与25目标不混写 | DESIGN_PROPOSED；实测NOT_RUN |
| R2-11 | 全部Graph对象/ID/纯Ops/Getter与Selection/View公共面完成核对，无Camera私有类型 | DESIGN_PROPOSED；精确API仍待批 |
| R2-12 | 有界算法、合并/环/Sink冲突/否决/失败out不变；默认含Catalog最多18路径helper预算槽位 | DESIGN_PROPOSED；实际CPU/WCET RUNTIME_NOT_PROVEN |
| R2-13 | Graph及Resource按同一候选布局预算，J01～J08覆盖图fan-out与后端不支持的差异 | DESIGN_PROPOSED；ARMCC5/Map/栈/DWT/实板NOT_RUN |

详见ModuleContract1.18～1.22、ResourceBudget9和DependencyRules8。R2方向审核通过不使ADR-01从UNRESOLVED变为批准，也不意味着Resource问题已闭合。

## ADR-02：每模块Managed Lifecycle与异步完成

参考ROS 2状态/转换与业务回调结果分离，推荐Configure/Activate/Deactivate/Cleanup/Error Processing契约；owner任务异步推进。拒绝全局模块状态镜像和同步串联回调等待，避免形成新版巨型ControlTask和死锁。每模块保留一份权威上下文，受控事务保留冻结结果；不新增Lifecycle Task或ROS运行库。

ROS源码的转换机制返回值不等于业务成功，这个区别必须体现在V2完成接口。CPU成本来自有界状态推进；Flash为转换表和公共辅助，RAM为每模块结果/身份记录；精确尺寸UNRESOLVED。网络重连/AI取消按各自模块处理，不自动释放Camera资格。

## ADR-03：Resource Ownership专项 — 统一契约，保留唯一后端所有权

### 3.1 问题与来源

参考vb2的queued/active/done/dequeued和驱动完成契约、GStreamer的Buffer/Memory/Pool分离、Zephyr的轻量Device Ops/Endpoint队列。三者都不能直接解决本项目完整Grant→Loan关联；普通refcount也不能说明谁取得哪一代资格、DMA是否结束或ACK是否消费。

H7源码：`camera_capture.c`的固定池、`camera_mjpeg_fifo.c`的块/Loan、`app_uvc_bridge.c`双端账本、`app_storage.c`槽位以及`app_camera_qualification.c`资格表分别权威。推荐Resource Port统一操作形态，不统一底层物理算法，不增加第二套引用计数。

### 3.2 对象与身份

| 对象 | 含义与权威 | 不能替代 |
|---|---|---|
| Buffer / FrameView | 已完成帧的不可变数据视图，包含总长、格式、时间、1或2span | 不证明有效grant或当前owner |
| Pool / Provider | 现有固定帧池或JPEG FIFO；负责物理块状态/引用 | 不管理业务Pipeline |
| Consumer | 模块身份；初期USB/Storage，未来按真实需求扩展 | 不复用Task handle或Graph Pad ID |
| G / ACQUIRE identity | consumer+session+generation+ACQUIRE request | 与RELEASE request不同 |
| R / RELEASE identity | 独立request，显式关联G | 不把两个request_id等同 |
| H / 交接identity | 每次受理的单调交接号，绑定G | 不用地址代替序号，避免ABA |
| M / Media identity | 后端真实Loan session/id或固定池有效令牌 | 不把Bridge lease假装成后端Loan |
| B / Bridge identity | stream_epoch/lease_id/command_id | 不等于Qualification session |
| S / Slot identity | slot/generation/Storage session | 不等于Camera generation |
| T / Terminal Receipt | 原交接的终结/确认收据，不持有Frame | 不增加Media引用，也不能覆盖仍有效旧身份 |

关联表只记录G↔H↔M/B/S、交接与终结阶段，不复制后端引用数。Backend release失败时记录仍持有；不能先删除关联再假装释放成功。完整layout及容量须精确API审查后冻结，当前仍UNRESOLVED。

### 3.3 受理、关闭与归还协议

1. 消费者通过可靠ACQUIRE取得G；资格存在不等于已取得帧。
2. Camera owner在同一串行边界检查G有效并受理H，实际取得/Retain后绑定M；队列满保留同一身份，未受理不产生持有。
3. 关闭同一G的准入（候选为ACTIVE→RELEASING）后，不接受新H；已经正式受理的交接可继续完成/取消/Return。此前入队未消费的请求必须逐一拒绝或终结，不能因关闭而丢弃已取得资源。
4. Consumer停止使用原帧后提交Return；Camera匹配完整身份并实际Release后固定终结结果。USB ACK入队和消费分开；重复/迟到/错owner消息不能重复执行Release。
5. 同一owner核验G入口已关、无未终结交接、后端无该G的实际持有以及相关终结确认条件满足，才完成R并撤销资格。另一G仍ACTIVE不影响本G释放，Camera继续运行。
6. 最后消费者退出不自动证明硬件停止：无资格申请/交接、所有后端资源安全，再由Camera唯一停止协调路径停Media/Device。

现有main没有上述完整关联与稳定关闭实现，两个总体缺口仍NOT_PROVEN。只读Snapshot API应返回同一Camera序列点的后端事实，不跨任务裸拼接debug，也不能通过查询创建Loan。USB端ACK消费由既有协作机制提供受保护证明，不新增平行ACK协议。

### 3.4 DMA安全退休

Capture DMA写目标由后端管理，不能在READY/IN_USE保护外重用；JPEG块只有相关Loan与解析引用均释放且不再是DMA目标才可回收。USB/网络DMA读原Frame时，发送函数返回成功、驱动接受、TCP接受均不表示读取结束。

复制式发送：所有CPU/异步DMA对原帧的复制读取结束，数据进入独立私有缓冲后，才允许原Frame归还；发送私有缓冲的后续DMA独立管理。零拷贝：所有异步操作逐一证明不再访问原Frame后才可Return。多个DMA不能用其中一个完成代替全部完成。缓存属性/屏障/内存区继续由既有Platform/BSP约束。

终结收据保存逻辑终结证据但不保留物理Frame；必须防止USB ACK未消费时被新的SD-only内部记录覆盖。推荐研究独立有界T而非一直占住Camera公共帧记录阻塞Storage；满时对相关入口施加背压，不无限增表、不回绕H。T的精确退休条件、迟到拒绝与原Replay关系UNRESOLVED，不能直接导入C2.2B WIP当已批实现。

### 3.5 多消费者与错误恢复

USB、Storage各自Return；一个退出不得清另一个owner。SD-only内部原始持有需标Camera内部用途，不标USB grant。当前raw后端单IN_USE不能仅靠前端refcount升级成并发消费者；不支持的组合明确拒绝，保留原有单路格式能力。

文件失败可以与帧安全归还并存：业务结果/清理结果/资源结果分开。STOP首次失败的原ACK冻结，后台成功另记，不清grant冒充完成。超时不强制释放、不清Replay、不覆盖旧generation；资源仍不明则保持阻塞。恢复只能在真实完成、身份核对和后端安全条件下进行。

### 3.6 比较、成本与扩展

不采用“统一普通refcount”：它不能验证consumer身份、请求代次、入口关闭、ACK和DMA事实。也不采用“每模块各自管理物理池”：会重复分配与破坏唯一释放者。推荐后端权威+有界关联账本，Flash是适配/校验逻辑，RAM是描述元数据，SDRAM不额外复制整帧；容量公式及未决上限见预算。无新Resource Task；Camera owner操作和临界区必须有界。

Ethernet/Wi-Fi/AI扩展时，后端若不能表达新增owner必须先单独升级能力或用完成复制的独立数据，不借用USB/SD owner位。网络/AI不影响其他消费者的退出证明。具体API、元数据和端口扩展均UNRESOLVED，后续逐项批准。

### 3.7 R3源码研究与精确所有权审核入口（仍UNRESOLVED）

重新读取Linux v6.12 videobuf2与GStreamer 1.24.10实际源码、复核原指纹；外部完成/池关闭思想与H7新增契约严格分开。推荐原Raw固定池/JPEG FIFO唯一权威、有界G/H/M/B/S关联和独立终结收据，不采用统一普通refcount、第二Frame Pool或Graph运行时Dispatcher。

[ResourceOwnershipArchitecture](H7_V2_ResourceOwnershipArchitecture.md)第2～3节为源码映射；第4～8节为身份、D/T单写、准入/关闭、ACK消费和退休设计；第9～10节为DMA及独立停止；第11～12节为预算和待决项。[ResourceOwnershipAcceptance](H7_V2_ResourceOwnershipAcceptance.md)含32个并发/异常场景、9条不变量和5组硬件门禁，全部NOT_RUN。

必须先审：原非回绕FrameRequest票据在新D中的承接/退休边界、Raw/JPEG纯身份查询、USB ACK消费证明、完整G/R资源核验与D/T容量。现RELEASE不直接验证related字段等于grant_request，不把它写成已证明的线关联，也不擅改C1规则；Camera应从权威资格表关联原G，精确API另审。released/idle仍NULL，无消费者真实退出接入。Grant→Frame Lease与Stable Admission Closure继续NOT_PROVEN；ADR-03和04不因此批准，不授权Phase1或C2.2C。

### 3.8 R3第二轮精确契约（仍UNRESOLVED）

第13节新增Raw/JPEG多消费者矩阵；G/R/H/B/M/S/T完整候选字段；USB Publication64B单写、Camera Endpoint/T100B单写、Storage原槽仅加G/H32B；ACK生成→入队→消费发布→Camera核验→退休cookie→D复用的严格顺序。T是端点终结状态，不另复制T、不持原Frame；原非回绕command_id作为USB ticket，无新增编号器。Raw/JPEG Snapshot64/256B只读，中立接口不泄漏模块私有类型。

Raw实例epoch4B为尚未存在的最小ADAPT提案，只在安全重建时消耗，查询不推进。Persistent200B+static workspace296B=496B为未编译候选，不是实际净RAM测量；具体批准仍等待人工。每个入口的L_admit/L_close由Camera owner串行，入口关闭不阻止Return。32项原场景及新增8项联合设计均NOT_RUN；Grant→Frame Lease和Stable Admission Closure继续NOT_PROVEN。保留AC-01～06、B2 ID23=A/ID31=B、原Replay和唯一普通停止推进，不授权Phase1/C2.2C。

## ADR-04：CameraCapture专项 — 复用采集门面和两种物理后端

### 4.1 当前关系（SOURCE_CONFIRMED）

`CM7/Media/Src/camera_capture.c`提供Init/Start/StartStream/Process/Stop/GetStopFacts等门面，内部有CameraStream；不是独立camera_stream.c。CameraStream固定7个描述缓冲，FREE/DMA/READY/IN_USE，DMA M0/M1目标不应复用IN_USE。`CM7/Media/Src/camera_mjpeg_fifo.c`是JPEG流解析/块与Loan后端，GetFrame取得原始持有、RetainFrame生成独立SD Lease。CameraCapture按格式选择后端并聚合停止/排空状态。

第二轮推荐Media Graph描述Sensor→CameraCapture→Raw/JPEG Provider→逻辑FrameRoute→sink；DCMI/DMA保留为Capture内部诊断路径，不作为独立启停Entity。Provider不是另建池或独立的硬件控制对象，见ModuleContract第1.12节。Resource Manager在采集门面之上适配G/H/M，不拥有另一份Frame Pool；后端继续决定物理资源能否回收。

### 4.2 推荐边界与替代方案

保留CameraCapture为低层采集门面，CameraStream和FIFO保留具体算法；V2 Camera Module执行Device/Media生命周期协调，Resource Port负责帧身份交接。若以后拆分CameraCapture文件，必须证明依赖与资源状态等价，不能以文件长为理由重建缓冲池。

不采用“每种格式新建统一BufferPool”或“Graph实体各自start/stop硬件”：会重复占SDRAM、重复停止DMA、丢失R2轮次事实。也不让Resource Manager自己调用HAL重建采集器。只读身份查询若现有API不足，可另审最小Media接口；不改现有Get/Retain/Release算法来追求通用外观。

### 4.3 停止与格式安全

CameraCapture_Stop的真实硬件结果、DRAINING和IsStopped必须区分；GetStopFacts只读不能推进。普通STOP仍只由Camera收尾入口推进，ERROR保护性停止独立记账避免同轮重复；Media完全停止后才能Device_Stop。只有身份和真实归还证据闭合才可重配置/覆盖缓冲。

Y8/RGB565/YUV422保持固定后端支持，JPEG/MJPEG用FIFO；没有证据不能声称所有raw格式可UVC/AVI输出。分辨率和格式切换须在旧资源安全排空后重新配置，当前一次配置约束如何放开属于UNRESOLVED，不能自动承诺在线切换。Graph纯能力成功、设备应用成功和实际FPS分别验收；UVC Commit没有Camera重配权限。Storage纯能力ADAPT和唯一常量来源见ModuleContract第1.14节，禁止调用带副作用Offer进行预检。

### 4.4 成本和扩展

复用时不新增Frame数据SDRAM；新增仅候选只读查询和适配代码/描述元数据，实际Flash/RAM/栈需ARMCC5/Map确认。维持既有Camera task容量、优先级和等待语义，无新Capture Task。网络/AI若需要raw多消费者或不同图像尺寸，先审后端能力和带宽，不为未来预分配新帧池。

验收门禁：原Media/FIFO/R2轨迹、raw/JPEG采集与停止、双span、未归还拒绝重建、保护性失败/后台恢复、目标板DMA真实访问终止。全部本轮NOT_RUN，设计不作为验收通过。

## ADR-05：Supervisor用声明式计划复用现有执行角色

参考MC拓扑与ROS转换表的边界，推荐Supervisor只解释const依赖步骤，异步发送模块请求并匹配完成；保留模块自主START/STOP和Camera资格权威。拒绝巨型ControlTask、图控制硬件、单个全局STOPPED。初期映射原Control角色预算，不开Supervisor新任务。Ethernet/Wi-Fi控制面可以提交同一计划接口，数据面不经Supervisor逐帧处理。计划记录容量/取消/冲突规则UNRESOLVED。

## ADR-06：Framework和运输复用

参考Zephyr轻量Ops，但继续FreeRTOS/Framework Initcall/RuntimeEvent/Timer，不引入Zephyr内核。Timer协作式Process必须有明确单owner；本阶段不新增Process调用或Timer任务。可靠命令保留原队列和Tx/Rx；通知只唤醒。迁移工具目录不改Replay算法，无额外配额。工程短枚举ABI、BASEPRI启动特例及运行期检查保留；潜在目录/符号兼容适配UNRESOLVED。

## ADR-07：Clean Restart与最终退休

参考成熟框架清晰接口边界，但工程迁移策略为H7自行提出。开发期独立ApplicationV2，Legacy完整；最终只有V2活动，删除被承接的旧源/头/工程登记，旧代码只留Git历史。不采用长期双栈或Legacy文件夹。短期Flash/仓库存储可能增加，发布固件不得编入双应用；RAM不得双实例化。逐文件清理与完整回滚见MigrationPlan。具体切换提交/Keil有效源表待批准。

## ADR-08：静态Module Registry与运行期有界依赖解析

状态**UNRESOLVED**。参考Zephyr v4.0.0 Device静态声明/依赖handle、ESP Component Manager v2.1.2固定SHA的manifest/版本约束；后者是构建期，不直接移植为运行期库。H7推荐const Module/Capability Catalog+确定Provider/版本/Optional/冲突/环解析，显式替代最多8次，不动态加载/分配、不启动硬件试探。与固定Supervisor业务分支相比可扩展；与无界SAT/Python下载解析相比成本有上限。细节/源码指纹见ModuleDependencyDesign。

估算Registry ROM≤5296B、Resolver512B、Plan1088B，均未编译；无新Task。未来Ethernet/Wi-Fi/AI以描述和端口扩展，后端不足拒绝，不修改既有模块业务来伪造第三owner。Provider策略/搜索预算/16模块容量待批准。

## ADR-09：Transport-independent Control Plane与M7权威

状态**UNRESOLVED**。复用现HSEM/IPC、Framework/Task基础，但不把M4当唯一总控，不把Control连接当Media Consumer。统一Gateway/Auth/Request/Event语义，各Adapter由硬件owner核心执行，M7最终仲裁；USB/Wi-Fi控制与媒体逻辑分离。没有引入新网络库/设备消费者。控制Session/认证/IPC新codec、共享USB基础设备保持仍待设计批准；当前IPC状态通道和USB_Mode_Stop事实见ControlPlaneArchitecture。

候选Client512B、订阅256B、事件256B、Gateway512B，不含TLS/驱动/包缓存，不新增Task/Queue实现；未来控制面可独立恢复，但前提是实际控制通道能力已具备。不能把目标图画成当前USB Composite或跨核命令已可用。

## ADR-10：配置写事务串行化、Revision冲突与真实补偿

状态**UNRESOLVED**。采用M7唯一配置写权威、单活动事务、base_revision核验、同请求固定结果和旧请求不重执行；拒绝Last Writer Wins、断线强清及以配置结构回滚代替资源归还。Supervisor仅执行Plan，Module状态和Media权威不转交Manager。事务生命周期/幂等/取消/文件不可逆与DEGRADED见ControlPlaneArchitecture。

候选Tx1536B+Applied/rollback512B；4记录容量、控制结果退休/boot identity、安全会话/补偿策略未批准。Flash/CPU/实际栈UNMEASURED；原52B/56B、Replay单调编号/高水位/退休约束不变。R3两个NOT_PROVEN继续，不因控制事务方案而关闭。

## 决策审核清单

| ADR | 当前状态 | 实施前必须补齐 |
|---|---|---|
| 01 Graph | UNRESOLVED | 表结构/静态容量、格式验证和诊断API审核 |
| 02 Lifecycle | UNRESOLVED | 每模块转换/完成/错误表和异步接口审核 |
| 03 Resource Ownership | UNRESOLVED | G/H/M关联、入口关闭、T退休、真实查询、DMA事实与成本 |
| 04 CameraCapture | UNRESOLVED | 最小端口/查询API、格式能力交集及等价回归设计 |
| 05 Supervisor | UNRESOLVED | 计划/冲突/取消语义，保持ID23/31批准行为 |
| 06 Framework/运输 | UNRESOLVED | 无算法变化工具迁移、链接唯一性、队列和ABI证据 |
| 07 Clean Restart | UNRESOLVED | 功能承接签核、Keil切换/删除门禁及回滚包 |
| 08 Registry/Resolver | UNRESOLVED | Provider/版本/替代策略、静态容量、确定性/成本验收 |
| 09 Control Plane | UNRESOLVED | 认证/传输核心归属、IPC/USB控制能力、Client/Media身份分离 |
| 10 Configuration Tx | UNRESOLVED | Revision/幂等/取消/补偿、boot身份和记录退休门禁 |

源研究支持采用这些思想，不证明本项目实现安全。此表无“已批准具体架构”条目；用户既有安全约束持续有效，任何变更另行审批。

## 最新冻结约束说明

用户冻结的五个核心及Phase0～10路线不变。ADR08仅深化Composition Manager内部Registry/Resolver；ADR09深化Control Service内部入口/协议边界；ADR10深化Composition Manager内部配置事务，不是提出第六/七/八个顶层管理器。Supervisor/Lifecycle协调仍内属Execution Manager，Module和Media Resource权威不迁移。目录及MigrationPlan均不修改。

这些具体接口/容量/策略未获批准，仍PROPOSED/UNRESOLVED；冻结既有架构方向不使任何本轮ADR自动APPROVED。可提前定义Phase7/8接口，不能提前生产实施；原Replay、客户端与媒体身份分离、两项NOT_PROVEN持续有效。
