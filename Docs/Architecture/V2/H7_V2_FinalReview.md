# Application V2 Phase 0 — 最终审查报告

## 1. 交付范围和真实基线

研究分支 `codex/architecture-media-graph-lifecycle`；输入HEAD `8a0359640007d2c39c74b4983ebdd22126ab406e`。本地与远端main核对为 `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`，研究分支远端输入同HEAD，开始工作区干净。Phase0旧研究文档提交59575fe，M4用户延时保留提交8a03596；原Phase1未生成落盘交付，此任务已转为V2 Clean Restart，不声称旧Phase1完成。

c6d2bed、fa9ce5d、1c97d7c是main祖先（git merge-base --is-ancestor均返回0）。本地保存的C2.2B跟踪分支d815208与main共同祖先为f0ce45f，main...C2.2B提交计数0/30；研究分支未合入这30个提交。该分支的Final Closure仍为冻结WIP，不把其代码/设计当main事实。随后单独查询C2.2B远端时发生Recv failure: Connection was reset，因此该支线的实时远端HEAD未再次确认，d815208只作为本地跟踪引用；未据此覆盖/更新任何分支。研究分支最终备份状态以提交后的ls-remote和交付回复记录为准。

任务只新增本目录11份Markdown文档，不创建新分支，不改main、生产源码、测试、ABI、Replay、任务配置、Keil或M4文件；不删除Legacy、不动历史output、不执行测试/构建、不进入C2.2C。提交号由实际Git提交产生，不在文档里预填或自引用预计SHA。

## 2. 实际交付文件与 Legacy Cleanup 合并映射

| 文件 | 内容 |
|---|---|
| [H7_V2_ApplicationArchitecture.md](H7_V2_ApplicationArchitecture.md) | 当前事实/最新六层统一目标、控制与媒体边界、Clean Restart |
| [H7_LegacyApplicationAudit.md](H7_LegacyApplicationAudit.md) | 全文件/函数、全局对象、宏、私有字段、回调/间接调用、初始化与执行链 |
| [H7_LowLevelReuseMatrix.md](H7_LowLevelReuseMatrix.md) | 十二类底层KEEP/ADAPT及未决板型/接口问题 |
| [H7_V2_ModuleContract.md](H7_V2_ModuleContract.md) | 第1节为R2 Static Typed Media Graph的C结构/表/验证/实例；其余为既有模块契约草案 |
| [H7_V2_DirectoryStructure.md](H7_V2_DirectoryStructure.md) | V2目录、允许/禁止依赖、开发隔离与最终唯一活动Application |
| [H7_V2_DependencyRules.md](H7_V2_DependencyRules.md) | 状态权威、同步/运输/ISR、依赖门禁设计 |
| [H7_V2_MigrationPlan.md](H7_V2_MigrationPlan.md) | 迁移计划；同时承载 LegacyCleanupPlan 的清理原则、逐文件退休、Keil/链接清理及验收回滚内容，见下方映射 |
| [H7_V2_OpenSourceArchitectureStudy.md](H7_V2_OpenSourceArchitectureStudy.md) | 五个官方项目固定版本源码、对象/函数/机制、H7映射和读取指纹 |
| [H7_V2_ArchitectureDecisionRecord.md](H7_V2_ArchitectureDecisionRecord.md) | 十项待批ADR（01～07原设计，08～10注册/控制/配置事务新增） |
| [H7_V2_ResourceBudget.md](H7_V2_ResourceBudget.md) | RAM/SDRAM/Flash/CPU/栈/队列/缓冲与扩展预算 |
| [H7_V2_ResourceOwnershipArchitecture.md](H7_V2_ResourceOwnershipArchitecture.md) | R3：固定版本源码映射、后端/身份、关闭/ACK/DMA/退休及独立停止设计 |
| [H7_V2_ResourceOwnershipAcceptance.md](H7_V2_ResourceOwnershipAcceptance.md) | R3：32项并发场景、9项不变量、5组硬件门禁；全部NOT_RUN |
| [H7_V2_ControlPlaneArchitecture.md](H7_V2_ControlPlaneArchitecture.md) | 统一控制API、多客户端配置事务、跨核/媒体边界及恢复根 |
| [H7_V2_ModuleDependencyDesign.md](H7_V2_ModuleDependencyDesign.md) | 静态模块能力、运行时有界依赖/Provider解析、Pipeline选择与Plan |
| 本文件 | 范围核查、结论、未决事项和后续审核门禁 |

### 2.1 清单问题处理记录

Phase0交付时本目录有11份物理文档；R3首轮新增两份为13份，本次统一架构另新增两份，当前15份。`H7_V2_LegacyCleanupPlan.md` **未作为独立文件生成**：此前有意将清理计划放入MigrationPlan，依据是该文档原有“迁移与Legacy退休计划”标题及第1、3、4、5节的清理内容。但最终交付索引遗漏了合并说明，不能把“11份实际文件存在”表述为“原定11个文件名逐项齐全”。本次只补明合并关系，不拆分或改写已提交的清理设计。

`H7_V2_ResourceBudget.md` 是独立资源预算交付，**不是 LegacyCleanupPlan 的替代文件**。Legacy Cleanup 按下表作为逻辑交付项定位；没有创建同名空壳文件，也不声称独立文件已经交付。合并交付形式记录在此供人工审核，不表示清理方案已获批准。

| Legacy Cleanup 审核内容 | 合并承载位置 | 定位内容 |
|---|---|---|
| 清理策略及删除前置条件 | MigrationPlan 第1节“迁移纪律” | 当前保留旧Application；功能承接、引用和链接/实板门禁后再删除；Git历史回滚 |
| 各旧源/头文件承接与退休 | MigrationPlan 第3节“文件级退休映射” | 19组旧C/同名头及internal头的V2承接、阶段和删除前特有检查 |
| Keil和链接清理 | MigrationPlan 第4节“Keil与链接清理门禁” | 有效源、头路径、宏、初始化入口、重复/未解析符号及唯一活动Application |
| 回归、验收和回滚证据 | MigrationPlan 第5节，结合第2节阶段矩阵 | 软件/硬件证据、资源核对和逐阶段完整回滚；未迁移必要功能不能删除 |

ADR-01～ADR-07仍全部UNRESOLVED；Grant→Frame Lease与Stable Admission Closure仍NOT_PROVEN。本次清单澄清不改变任何架构决策、证据结论或实施授权。

### 2.2 后续分轮人工审核文件索引

以下仅为文件和章节定位，不预先批准审核顺序或方案。具体文件链接见上表；以节标题为稳定定位，代码/文档行号可能随后续批准的修订移动。

| 建议审核轮次 | 文件与章节定位 | 本轮审核关注 |
|---|---|---|
| R1 事实基线与审计覆盖 | LegacyApplicationAudit：文件状态表、真实运行链、间接入口、全函数索引、全局对象/宏/私有字段补充；LowLevelReuseMatrix 第1～3节 | 源码事实、底层复用边界、依赖覆盖限制 |
| R2 成熟架构与静态拓扑 | OpenSourceArchitectureStudy 第2～8节；ArchitectureDecisionRecord ADR-01；ModuleContract 第1节 | 固定Tag/SHA、Graph/Format职责与MCU适配 |
| R3 Resource Ownership | ArchitectureDecisionRecord ADR-03第3.1～3.6节；ModuleContract 第3节；DependencyRules 第2～4节 | 身份关联、ACK/DMA、独立归还、稳定关闭及两个NOT_PROVEN |
| R4 CameraCapture边界 | ArchitectureDecisionRecord ADR-04第4.1～4.4节；ModuleContract 第4节；LowLevelReuseMatrix 中Capture/Stream/FIFO条目 | 唯一采集门面、复用后端、普通/保护性STOP |
| R5 模块生命周期与Supervisor | ArchitectureDecisionRecord ADR-02/05/06；ModuleContract 第2、5～8节及兼容补充 | 异步完成、业务与资格分离、ID23/31和错误策略 |
| R6 目录与依赖 | DirectoryStructure 第1～4节；DependencyRules 第1～6节 | 允许/禁止依赖、任务/ISR/Framework/ABI边界 |
| R7 迁移与Legacy Cleanup | MigrationPlan 第1～5节；ArchitectureDecisionRecord ADR-07；本文件第2.1节 | 功能承接、逐文件退休、Keil清理、最终唯一应用及回滚 |
| R8 预算及综合门禁 | ResourceBudget 第1～8节；ApplicationArchitecture 第5～7节；本文件第5～8节 | 计算/估算/实测区别，未决项、扩展成本与实施前置条件 |

当前只准备以上定位，等待人工逐轮意见；不开始Phase 1或V2生产代码实施。

### 2.3 Application V2 R2审核包定位

首轮交付cd8daa6；第二轮输入HEAD `cd8daa6536802b403d40fefdba3b57f52df2bd09`，仍在研究分支。CM7生产源码仍与main f0ce45f一致，既有M4 OLED延时完整保留。只修改五份已有架构文档：ModuleContract、ArchitectureDecisionRecord、ApplicationArchitecture、ResourceBudget、FinalReview。其余V2文档和Legacy不变。

| 审核项 | ModuleContract定位 | 第二轮结论/状态 |
|---|---|---|
| R2-01 Source Pad fan-out | 第1.1、1.3、1.4节 | 人工方向认可，多静态Link不自动产生Frame持有 |
| R2-02 Raw/JPEG Backend | 第1.1、1.4、1.5节 | 人工方向认可；固定Pipeline，同源相容媒体可合并 |
| R2-03 格式聚合 | 第1.6、1.14节 | 原则认可，纯接口和权威契约继续待审 |
| R2-04 Entity边界 | 第1.12节 | 推荐CameraCapture统一封装DCMI/DMA；B仅保留比较，非主表 |
| R2-05 FrameDispatch定位 | 第1.13节 | 推荐纯逻辑FrameRoute，真实交接在Camera/Resource/消费者端口 |
| R2-06 最终能力约束 | 第1.14节 | 各层纯验证器均有否决权；Storage纯查询最小ADAPT，共享唯一上限，不以Offer/Prepare预检 |
| R2-07 FPS证据 | 第1.10、1.15节 | 10FPS稳定基线来自用户确认；25为目标/待验证；合法性、配置成功、实测分级 |
| R2-08 配置关系 | 第1.2、1.7、1.16节 | 一个用户请求派生媒体与每Sink策略；Commit不直接重配Camera，当前动态配置不支持 |
| C结构/容量/算法 | 第1.2～1.8、1.11节 | 新主表10/14/10/7；56B候选媒体值+独立Sink策略；无消息ABI改动；有界验证及失败out不变 |
| 拓扑实例与运行边界 | 第1.9～1.10节 | YUY2→UVC、JPEG→Storage、未来Ethernet+Storage；未来节点依旧不可执行 |

ADR-01～07全部UNRESOLVED，Grant→Frame Lease及Stable Admission Closure仍NOT_PROVEN。配置派生与回调签名变化仅为文档草案，不是生产实现；Source采集率没有可虚构的配置字段。保留B2 ID23/31批准行为，不调整Lifecycle/Replay契约。无测试、构建或实板执行；软件/ARMCC5布局/WCET均NOT_RUN/UNMEASURED。审核源码依据见ModuleContract第1.12～1.16节。

本轮文档检查包括：五文件差异范围、所有旧生产/测试/工程/M4文件不变、结构预算算术、描述ID/路由/速率配置及章节交叉一致性。检查结果在提交后交付回复记录；不把文档检查当作生产测试。待人工审核，不进入Phase1/C2.2C，不合并main。

### 2.4 R2人工条件闭合与R3审核包

输入HEAD54718ee，研究分支/远端输入同SHA，main仍f0ce45f，开始工作区干净。R2-01/02/04/05/07/08人工通过；03原则通过；06有条件通过。补充内容是CoreMediaFormat中立媒体描述及GraphFormat别名，Storage公共Validator不依赖CameraConfig_t；各边界Ports再转旧配置，唯一能力源不复制。FrameRoute无真实Dispatcher/第二账本，Source Capture Rate、USB Delivery Rate、Storage Sampling Rate分别定义。ModuleContract第1.17节和DependencyRules第7节为复核位置，ADR-01不因此批准。

R3首轮新增两份设计文件，该轮V2目录共13份；本次统一任务后共15份，见第2.6节。实际重新读取官方Linux v6.12 vb2与GStreamer1.24.10六个源码文件、核对原hash；SourceStudy第9节记录，完整映射见ResourceOwnershipArchitecture第2～3节。后者第4～8节含身份关系图、受保护D/T发布与退休、未提交/已提交区分、G/R关闭核验；第9节逐访问者审DMA；第10节含状态机及两消费者独立停止。Acceptance第3节有32项设计场景，未执行测试，不填PASS。

仍待审/待实现：现有非回绕FrameRequest票据在新D中的关联/退休边界；Raw/JPEG最小只读身份查询；USB ACK消费/双端退休确认；G/R资源核验及released/idle真实适配；D/T最小容量和ARMCC5成本。当前Core不直接检查RELEASE.related等于grant_request，不擅改C1语义或写成已证明关联。Grant→Frame Lease和Stable Admission Closure两项继续NOT_PROVEN，所有ADR继续UNRESOLVED。

本轮仅改架构Markdown，不改生产/测试/ABI/Replay/Keil/任务/M4，不删除Legacy或历史output；不执行测试、编译、实板，不进入Phase1/C2.2C、不合并main。R3是设计交付，不是Resource软件/硬件验收完成。实际Diff/路径/指纹检查及提交推送状态以本轮最终报告为准。

### 2.5 R2 Final Closure + R3第二轮先行草案（输入c52490b，未独立提交）

本轮在原研究分支继续，不创建分支。输入本地c52490b/main f0ce45f，工作区干净；首次实时远端查询发生Connection reset，未据缓存声称远端已确认，最终备份以提交后查询结果为准。先行草案修订指定七份文档；随后追加统一架构任务，最终范围扩展为第2.6节的八份修订/两份新增，未先提交旧草案。无新增生产/测试/工程文件。

| 审核项 | 精确定位 | 本轮结论 |
|---|---|---|
| R2-09 | ModuleContract1.18 / DependencyRules8 | Core中立媒体/Storage纯接口和唯一能力源正式文档修订；实现待授权 |
| R2-10 | ModuleContract1.19 | 三速率配置/运行统计/证据所有者明确；10输出基线不等于Source实测10，25目标未升级 |
| R2-11 | ModuleContract1.20 | Graph公共对象与ID/端点/合并/纯Ops一览完成，无Camera私有类型泄漏 |
| R2-12 | ModuleContract1.21 | 每循环上限、最多18路径helper槽、边探测预算、原子失败/首错规则完成；未测WCET |
| R2-13 | ModuleContract1.22 / ResourceBudget9 | Graph/Resource候选布局统一、预测与ARMCC5/Map/栈/DWT/实板证据分离 |
| R3-01 | ResourceArchitecture13.1 | Raw单IN_USE与JPEG两owner组合、SD-only/独立退出/未来复制与零拷贝分别限定 |
| R3-02 | ResourceArchitecture13.2 | 完整G/R/H/B/M/S/T字段、关联、耗尽、ABA与退休契约，优先原command票据 |
| R3-03 | ResourceArchitecture13.3 | 双端单写、原ACK消费发布/退休cookie/D复用、T不持Frame；Storage只扩原槽 |
| R3-04 | ResourceArchitecture13.4 | 覆盖所有Request/pending/Offer/Publish/Retain/Return/通知入口，同Camera owner关闭/受理 |
| R3-05 | ResourceArchitecture13.5 | Raw64B/JPEG256B纯Snapshot草案，上下文、同步、有效期及错误/不变输出规则 |

本轮所有新增/深化结论为DESIGN_PROPOSED，依据的当前事实SOURCE_CONFIRMED、外部机制EXTERNAL_SOURCE_CONFIRMED仍按原文件分栏；新字段/API为IMPLEMENTATION_REQUIRED；测试/构建/布局/Map/栈/DWT/硬件NOT_RUN，运行安全RUNTIME_NOT_PROVEN。候选Resource496B只代表当前约束下数据布局预算，不是实测增量，不能宣布性能或内存已通过。

原R3-01～32设计和状态保持NOT_RUN；新增J01～J08联合设计也NOT_RUN。Grant→Frame Lease及Stable Admission Closure继续NOT_PROVEN，全部ADR保持UNRESOLVED。B2 ID23=A、ID31=B、AC-01～06、52B/56B ABI、Replay单调编号/高水位/退休约束保留。M4 OLED延时、Legacy、output、任务栈/优先级、Keil均不修改。本轮文档完成不等于生产授权，不进入Phase1/C2.2C、不合并main；等待人工最终复核。

### 2.6 R2/R3 Architecture Consolidation最终审核包（最新任务）

输入仍c52490b27300558f2f654fde62959094aea84db0/main f0ce45f，原研究分支；前一任务未提交草案完整保留并纳入本次，不reset/rebase/覆盖历史。先前远端查询reset不当作成功证据；最终提交和远端SHA由实际推送后报告。只改Docs/Architecture/V2：八份既有文件（含ApplicationArchitecture）及两份有独立内容的新文档ControlPlaneArchitecture、ModuleDependencyDesign。目录总15份，Legacy Cleanup合并索引保持。

| 议题 | 本轮独立结论 | 仍未决/未验证 |
|---|---|---|
| R2-09 | Storage纯接口只依赖唯一CoreMediaFormat；Graph别名；唯一70000B规则，运行Frame/内容/所有权分层 | 纯接口实现/新API批准待定 |
| R2-10 | Source/USB/Storage/Network/AI五率分别目标、测量、缓冲/超限 | Network/AI实现/统计口径；25FPS仍目标 |
| R2-11 | const静态图与运行选择分开，公共对象/ID/Pad/Link/Ops完整 | API/布局尚未编译批准 |
| R2-12 | 边界/环/合并/否决/失败out不变，有界算法与最大遍历预算 | 实际实现、CPU/WCET NOT_RUN |
| R2-13 | Graph/Resource+Registry/Resolver/Plan/Tx/Client/Event/IPC预测统一，实测分栏 | 每bank/真实栈/代码/驱动与传输缓冲未测 |
| R2-14 | 静态Module声明+运行Required闭包/版本/Provider/Optional/冲突/环/资源汇总，确定有界选择 | 策略/容量/搜索预算、新ADR08待批；未实现 |
| R2-15 | Desired→Modules→声明图→Graph→Resource→Plan，显式等价替代 | 热重配与后端不足不得放行；实际适配缺失 |
| R2-16 | 多入口统一Gateway，M4普通client，Adapter核心按硬件owner，控制与媒体逻辑分离 | 认证/控制codec、当前IPC/独立USB Control缺口 |
| R2-17 | M7单活动写、base_revision、幂等/过期拒绝、取消/断线/真实补偿 | boot/client身份、缓存退休/恢复策略/协议与容量新ADR10待批 |
| R3-01 | Raw单IN_USE/JPEG两owner、SD-only、单路退出及复制/零拷贝分别限定 | 新第三owner不支持，不能以普通refcount补齐 |
| R3-02 | G/R/H/B/M/S/T候选字段、ABA、耗尽、冷启动/重配拒旧身份明确 | Raw epoch/query、D绑定仍需实现/批准 |
| R3-03 | ACK生成/入队/消费发布/Camera核验/retire/D复用，双端单写、不新建ACK队列 | 消费证明、T容量前提/接口仍未实现 |
| R3-04 | 所有现入口/未来Network/AI同Camera owner L_admit/L_close，关闭后旧G不重开 | 稳定屏障真实路径未接入 |
| R3-05 | Raw/JPEG只读Snapshot、上下文/临界区/有效期/错误候选 | 纯query/ARMCC5/真实并发证据缺失 |
| R3-06 | Control/Transport/ConfigTx与媒体身份/ACK分域，M4不碰M7资源表，IPC只Transport | 现IPC不具备完整Control命令；无ABI修改 |

新增外部研究：Zephyr v4.0.0 Device Model实际源码与ESP Component Manager v2.1.2固定SHA，路径/结构/函数/指纹见ModuleDependencyDesign2节；明确后者是构建期求解，不移植为MCU运行库。既有Linux MC/ROS/vb2/GStreamer固定源码参考保持；H7五核心内逻辑分层/有界Resolver/控制事务适配另标DESIGN_PROPOSED，不能冒充外部框架已有本工程协议。

最终16项问题回应：

| 问题 | 最终判断 |
|---|---|
| 1 R2-09～17完成度 | 设计条款均已覆盖，所有具体接口/容量/认证/重配政策仍待批；未实现 |
| 2 R3-01～06完成度 | 精确文档已覆盖，真实G绑定、消费证明、Snapshot与关闭未实现 |
| 3 Storage解除Camera依赖 | 公共格式**设计**已解除；现生产类型适配未修改，不说源码已重构 |
| 4 静态注册/动态解析 | 已设计const Registry+有界运行闭包/选择；未编码/运行，不是动态加载 |
| 5 新Network/AI扩展 | 理想只加描述/端口/合法图/产品策略；不改既有模块业务/监督分支，后端不足明确拒绝 |
| 6 多Control Clients | 设计全部走统一Gateway/Manager，当前多数传输尚无实施 |
| 7 M4角色 | 普通控制客户端，不是唯一总控，不持Frame |
| 8 M7权威 | 配置/选择/执行最终权威；各Module/Media仍唯一拥有具体运行/资源状态 |
| 9 USB/Wi-Fi分离 | 逻辑设计已分；当前USB_Mode_Stop停Device、Wi-Fi未实现，缺口保留 |
| 10 多客户端冲突 | 单活动写、base_revision、同请求固定结果、无默认LWW；boot/session仍待批 |
| 11 分层职责 | Graph拓扑、Resource交接、Lifecycle模块转换、Supervisor计划执行独立 |
| 12 Raw/JPEG限制 | 已明确单IN_USE/两owner；Graph fan-out不产生物理能力 |
| 13 Grant→Lease真实证明 | **NOT_PROVEN**，未升级 |
| 14 Stable Closure真实证明 | **NOT_PROVEN**，未升级 |
| 15 验收场景 | 原R3-01～32、J01～08、新C01～32共72设计条目，执行0，全部NOT_RUN |
| 16 新ADR | ADR08 Registry/Resolver、09 Control Plane、10 ConfigTx需批准；ADR01～10均UNRESOLVED |

候选已列metadata RAM7148B/描述ROM≤7948B仅部分布局估算，不能当完整V2净资源、Map或性能测量。实际布局/代码Flash/传输认证缓存/任务栈/DWT与实板均NOT_RUN。下一阶段建议按ADR08～10及Control/IPC/USB独立存活、R3 D/T/只读query分别人工审查，再单独授权实现；不因文档完成进入Phase1/C2.2C。

完成检查门禁：文件范围/UTF8/链接/编号、旧32场景内容不变、预算算术/身份单写/职责、源码锚点、M4/Keil指纹及main不变；这是文档检查，不是运行测试。本轮不改生产/测试/ABI/Replay/任务/Keil/M4、不删Legacy/output，不合并main。最终提交/推送SHA及工作区以实际交付回复为准，不预填自引用提交号。

### 2.7 最新追加约束核对：冻结五核心/路线

五核心固定为Control Service、Composition Manager、Execution Manager、Application Modules、Media Resource；六层图只保留为逻辑分析映射，最新总图已回归五子系统。Gateway/Auth/Request/Event是Control Service内部；Configuration Transaction、Registry/Resolver/Selector/Plan Builder是Composition Manager内部；Supervisor/Lifecycle协调是Execution Manager内部，不新增顶层管理器或重新安排目录。

Phase0～10原路线不改。MigrationPlan及DirectoryStructure文件保持输入c52490b逐字节一致；本轮不改阶段顺序/名称/实现门禁，不为控制/组合功能插入新阶段。Phase7/8只提前定义接口，不实施。静态注册、运行依赖解析、媒体候选选择、真实Pipeline切换四件事分别验收；计算成功不是重配置成功。

MigrationPlan SHA256 `6c0c0c12a422e9307e184650626b3cd09eb89f25503a222f56c1fd9ddcd1d69f`；DirectoryStructure SHA256 `64721c0029422eb0f6da9a546b8392ce737a687ce60d1716dabe5fb70a7a051e`。这证明本轮未修改该文件，不代表本轮重新批准内容。所有未批决定PROPOSED/UNRESOLVED；原R3/Replay/客户端断线与媒体归还的区分保留，NOT_PROVEN/NOT_RUN不变。

## 3. 全量审计覆盖与证据边界

已建立CM7/Application 39个C/H、210个C函数的索引，并补充107个宏定义条目（含头保护/条件分支）、100个文件级数据对象条目、私有上下文/函数表字段、显式回调注册/静态对象以及初始化偏序。它们是源码统计，不是依赖闭包或运行覆盖率；条件编译、函数指针、链接段顺序、外部调用及其他target仍需后续编译/链接/动态证据。

重要事实：App_Initcall没有独立文件，实际分布于App_Init、BridgePrepareOutput、LifecycleMessagesInit三导出；main没有app_camera_consumer；Camera/USB/Storage任务没有C2.2B真实资格接入；s_qualification_ops.released/idle为NULL。ControlTask仍是旧Camera→USB启停编排，NONE也会启动Camera。

Keil XML核对了h7_mac_CM4/h7_mac_CM7及组级IncludeInBuild：两target都列有另一核组，但各自排除另一核；CM7应用分布于CM7\\Application与CM7 Native Runtime。工程登记不等于实际编译，未来清理还需文件级/继承选项和链接清单。本轮未改变用户设置。

## 4. 源研究结果如何改变设计

Linux MC的Pipeline是拓扑占用/校验，不是DMA控制器；V2 Graph因此不持帧、不启停。ROS 2把转换机制和业务回调结果分开，且发布失败不必回滚已发生状态；V2保留真实事实与冻结结果。vb2区分队列/驱动完成/使用者取回；H7不照搬其防御性强制回收。GStreamer停池与outstanding归零分离；V2关准入后仍须真实排空。Zephyr提供轻量Ops与队列接口，但所读版本stop/flush的返回处理和堆buffer策略不能充当本项目退出证明。

这些结论均有固定Tag/SHA的实际官方源路径、结构/函数、H7对应与不适合移植部分，详见研究文档。没有新增第三方运行库或把成熟源码直接移植到工程。

## 5. 推荐架构与判断

| 问题 | 审查结论 |
|---|---|
| 是否完整划分Core/Graph/Lifecycle/Resource/Modules/Supervisor/Configuration/Diagnostics | 设计覆盖；具体方案UNRESOLVED，待审核 |
| 是否有第二份业务或引用权威 | 目标禁止；模块自有业务、Camera资格、Media后端引用、Supervisor计划各自唯一 |
| Resource是否只用普通refcount代替退出协议 | 否；显式G/R/H/M/B/S/T身份、准入/关闭/真实Return/终结确认/退休分别设计 |
| 是否重新建立重复Frame Pool | 否；推荐复用CameraCapture门面、内部CameraStream、JPEG FIFO，统一端口不统一物理算法 |
| 是否改变既有ABI/Replay | 本轮没有；具体工具迁移也需另批，目标保留52B/56B及高水位/候选/冻结规则 |
| 是否保持raw和JPEG差异 | 是；固定池单IN_USE与JPEG独立Lease能力分开，不假装当前raw多消费者已支持 |
| USB/Storage独立退出是否已证明 | 尚未；目标契约明确，生产关联/关闭证据仍NOT_PROVEN |
| Supervisor是否新巨型ControlTask | 目标采用const计划和异步完成，无模块状态副本、无直接硬件/Frame操作 |
| 是否支持未来Network/AI | 留明确扩展边界，不提前注册消费者/增任务/扩大缓存；实际能力UNRESOLVED |
| Legacy是否会永久共存 | 否；当前完整保留，功能承接后逐文件退休，最终Keil和活动源码仅V2 |

## 6. 资源和实时性摘要

SOURCE_CALCULATED：五角色静态栈27648B；普通队列载荷按当前52B工程ABI为1040B，Lifecycle载荷672B；JPEG区含drop为540672B；固定池4300800B；AVI索引65536B，三用途独立范围合计4907008B。Snapshot63KiB/单次JPEG容量与固定区域复用，不重复计。SDMMC独占AXI32KiB、StorageFS8KiB、Snapshot回读512B及USB packet成员1024B分别列账。

R2第二轮Graph示例产品const数据预计2108B，含名称上限共≤2652B；暂存/诊断/发布视图476B，加最多8条可变请求992B时共1468B，均为未编译的DESIGN_ESTIMATE（替代Phase0的2528B/232B占位估算）。关联/T/计划容量和新代码Flash仍UNRESOLVED；真实每bank RW/ZI、调用栈、WCET、CPU利用率均UNMEASURED。本轮不能声称内存已通过或性能已改善。无新增任务、优先级/栈/队列/复制参数变化。

USB/Storage复用当前Service；Ethernet/Wi-Fi加入需独立发送缓存/多DMA与背压预算；AI需模型/tensor/转换开销和真实Frame消费者协议。增加逻辑sink不自动增加Media能力，不释放其他消费者owner。

## 7. 既有批准行为与未决事项

保持用户批准：Replay不加配额；AC-01～06安全约束；B2 ID23按A有界BUSY/ACK/FAULT；ID31按B拆分当前未退出拒绝和未来合法成功START后FAULT复位。VIDEO_OUTPUT_NONE按需采集、USB_TEST不申请Camera是迁移目标，不伪装为main现状。文件失败与帧安全可分别保留，不能把失败文件计为成功录像。

以下必须等待人工决策/精确设计：

1. ADR-01～07整体取舍，以及V2目录/唯一应用切换方案。
2. Resource G→M真实关联、L_admit/L_close/L_verify线性化、有界Terminal Receipt与双端退休、只读Media查询API。
3. raw后端在保留单路能力前提下的多消费者边界；不授权新引用算法。
4. 资格/事务工具的无算法变化迁移与接口适配，不引入新Replay。
5. Supervisor计划冲突/取消与完成契约，不能把超时当安全退出。
6. 板型：当前main是旧软件SCCB，V1 I2C1/CAM_DVDD_EN尚非本研究生产基线；不能将历史V1通过当本分支已验收。
7. 资源字段/容量/Flash预算及ARMCC5/Map/目标板时序验证。

## 8. 本轮检查与下一步

文档检查包括：11份实际文件已登记，LegacyCleanup独立文件未生成及合并位置见第2.1节；源码路径/函数行索引与绑定核对；宏/全局/间接入口补充；开源引用固定版本；预算算式与模式复用；未决项不标APPROVED/PASS；跨文档权威和Clean Restart策略一致。提交前检查Git diff --check和暂存清单，要求仅本目录11份新Markdown，无修改/删除既有文件。

软件测试：NOT_RUN；CM7/CM4编译：NOT_RUN；实板：NOT_RUN；新增运行资源测量：NOT_RUN。未使用设计推导替代Grant→Lease或稳定关闭运行证据。

结论：V2 Phase0审计与设计交付完成，可以交人工审阅；不等于允许V2编码、已完成资源安全证明或具备直接启用双消费者/网络的条件。提交推送研究分支、核对远端后停止；不自行实施下一阶段、不合并main。

### 实际完成的文档检查记录

此前检查确认11份实际文件存在；原定文件名与合并交付的差异已在第2.1节更正，不能据数量宣称逐名齐全。210个函数位置均对照源码声明核对；62个唯一完整源码路径检查存在；UTF-8、代码围栏和本目录Markdown链接检查通过。预算地址算式核对固定池尾0xD041A400、三用途合计4907008B。检查时既有跟踪文件无diff。以上是文档一致性检查，不是运行生产测试。

受保护文件逐字节与输入HEAD比较相同：CM4/Core/Src/main.c SHA256 `1dec895ef44e568a9a263fc8b229fc2622c6a625c54b8e291286e186be36454c`；MDK-ARM/h7_mac.uvprojx SHA256 `a8f9e0d08c9cceb9ec0934fe287f6c71ad99bb846f13246ef884e801369c9aa1`。本轮没有删除/修改M4延时或注释。
