# Application V2 Phase 0 — 最终审查报告

## 1. 交付范围和真实基线

研究分支 `codex/architecture-media-graph-lifecycle`；输入HEAD `8a0359640007d2c39c74b4983ebdd22126ab406e`。本地与远端main核对为 `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`，研究分支远端输入同HEAD，开始工作区干净。Phase0旧研究文档提交59575fe，M4用户延时保留提交8a03596；原Phase1未生成落盘交付，此任务已转为V2 Clean Restart，不声称旧Phase1完成。

c6d2bed、fa9ce5d、1c97d7c是main祖先（git merge-base --is-ancestor均返回0）。本地保存的C2.2B跟踪分支d815208与main共同祖先为f0ce45f，main...C2.2B提交计数0/30；研究分支未合入这30个提交。该分支的Final Closure仍为冻结WIP，不把其代码/设计当main事实。随后单独查询C2.2B远端时发生Recv failure: Connection was reset，因此该支线的实时远端HEAD未再次确认，d815208只作为本地跟踪引用；未据此覆盖/更新任何分支。研究分支最终备份状态以提交后的ls-remote和交付回复记录为准。

任务只新增本目录11份Markdown文档，不创建新分支，不改main、生产源码、测试、ABI、Replay、任务配置、Keil或M4文件；不删除Legacy、不动历史output、不执行测试/构建、不进入C2.2C。提交号由实际Git提交产生，不在文档里预填或自引用预计SHA。

## 2. 十一份交付文件

| 文件 | 内容 |
|---|---|
| [H7_V2_ApplicationArchitecture.md](H7_V2_ApplicationArchitecture.md) | 当前/目标架构、权威分离、Clean Restart时间边界 |
| [H7_LegacyApplicationAudit.md](H7_LegacyApplicationAudit.md) | 全文件/函数、全局对象、宏、私有字段、回调/间接调用、初始化与执行链 |
| [H7_LowLevelReuseMatrix.md](H7_LowLevelReuseMatrix.md) | 十二类底层KEEP/ADAPT及未决板型/接口问题 |
| [H7_V2_ModuleContract.md](H7_V2_ModuleContract.md) | Graph/Format/Lifecycle/Resource/Camera/USB/Storage/Supervisor/Network草案 |
| [H7_V2_DirectoryStructure.md](H7_V2_DirectoryStructure.md) | V2目录、允许/禁止依赖、开发隔离与最终唯一活动Application |
| [H7_V2_DependencyRules.md](H7_V2_DependencyRules.md) | 状态权威、同步/运输/ISR、依赖门禁设计 |
| [H7_V2_MigrationPlan.md](H7_V2_MigrationPlan.md) | 十阶段依赖/接口/文件/回归/验收/回滚、逐文件清理门禁 |
| [H7_V2_OpenSourceArchitectureStudy.md](H7_V2_OpenSourceArchitectureStudy.md) | 五个官方项目固定版本源码、对象/函数/机制、H7映射和读取指纹 |
| [H7_V2_ArchitectureDecisionRecord.md](H7_V2_ArchitectureDecisionRecord.md) | 七项待批ADR，含Resource Ownership与CameraCapture两项专项 |
| [H7_V2_ResourceBudget.md](H7_V2_ResourceBudget.md) | RAM/SDRAM/Flash/CPU/栈/队列/缓冲与扩展预算 |
| 本文件 | 范围核查、结论、未决事项和后续审核门禁 |

ResourceBudget是按本轮新增要求独立补充的第11份；不把它冒称成此前已有命名文件。ADR两专项作为独立章节完整论证，不另增未经列入的生产或测试文件。

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

V2 Graph的const数据约2528B和RAM示例232B只是未冻结布局的DESIGN_ESTIMATE。关联/T/计划容量和新代码Flash仍UNRESOLVED；真实每bank RW/ZI、调用栈、WCET、CPU利用率均UNMEASURED。本轮不能声称内存已通过或性能已改善。无新增任务、优先级/栈/队列/复制参数变化。

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

文档检查包括：11份指定文件齐全；源码路径/函数行索引与绑定核对；宏/全局/间接入口补充；开源引用固定版本；预算算式与模式复用；未决项不标APPROVED/PASS；跨文档权威和Clean Restart策略一致。提交前检查Git diff --check和暂存清单，要求仅本目录11份新Markdown，无修改/删除既有文件。

软件测试：NOT_RUN；CM7/CM4编译：NOT_RUN；实板：NOT_RUN；新增运行资源测量：NOT_RUN。未使用设计推导替代Grant→Lease或稳定关闭运行证据。

结论：V2 Phase0审计与设计交付完成，可以交人工审阅；不等于允许V2编码、已完成资源安全证明或具备直接启用双消费者/网络的条件。提交推送研究分支、核对远端后停止；不自行实施下一阶段、不合并main。

### 实际完成的文档检查记录

11份文件清单精确匹配；210个函数位置均对照源码声明核对；62个唯一完整源码路径检查存在；UTF-8、代码围栏和本目录Markdown链接检查通过。预算地址算式核对固定池尾0xD041A400、三用途合计4907008B。检查时既有跟踪文件无diff。以上是文档一致性检查，不是运行生产测试。

受保护文件逐字节与输入HEAD比较相同：CM4/Core/Src/main.c SHA256 `1dec895ef44e568a9a263fc8b229fc2622c6a625c54b8e291286e186be36454c`；MDK-ARM/h7_mac.uvprojx SHA256 `a8f9e0d08c9cceb9ec0934fe287f6c71ad99bb846f13246ef884e801369c9aa1`。本轮没有删除/修改M4延时或注释。
