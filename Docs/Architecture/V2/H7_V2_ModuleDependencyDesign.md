# Application V2 — Module Registry、Runtime Resolver与Pipeline选择

## 1. 范围与证据

**冻结归属约束**：Application V2只有Control Service、Composition Manager、Execution Manager、Application Modules、Media Resource五个核心。本文Gateway/Request/Event等属于Control Service；配置事务、Registry/Resolver/Selector/Plan Builder属于Composition Manager；Supervisor/Lifecycle执行协调属于Execution Manager。它们是内部职能，不新建顶层管理器或调整目录。Phase0～10原路线不变；提前定义Phase7/8接口不表示实施授权。


输入本地c52490b，main f0ce45f，研究分支不变；本轮原有七份文档修订尚未提交时收到统一架构要求，保留并继续深化，不覆盖历史提交。以下是DESIGN_PROPOSED/UNRESOLVED，全部实现、布局及运行验证NOT_RUN。没有创建Task/Queue/动态内存、网络模块或编译登记。

SOURCE_CONFIRMED：`CM7/Device/Inc/device.h::DEVICE_EXPORT`和`CM7/Device/Src/device.c::Device_RegisterAll`14使用静态导出及Framework Initcall；Device_InitAll45执行auto_init。`CM7/Framework/framework_init.c::Framework_InitLevel`9按level调用导出。这是硬件Device注册/初始化，不是V2模块能力求解，不能拿Device状态当业务模块已ACTIVE。

## 2. 固定版本源码参考与适配界线

既有Linux MC、ROS Lifecycle、vb2、GStreamer、Zephyr Video固定版本及源码研究见OpenSourceArchitectureStudy。Linux MC不负责模块版本依赖，vb2不负责产品组合，GStreamer的动态元素/对象/协商不直接移植MCU。

本轮新增EXTERNAL_SOURCE_CONFIRMED：

| 项目/固定版本/官方源码 | 实际对象与入口 | 机制与不移植部分 |
|---|---|---|
| [Zephyr v4.0.0 device.h](https://github.com/zephyrproject-rtos/zephyr/blob/v4.0.0/include/zephyr/device.h) | DEVICE_DEFINE141、struct device412、device_required_handles_get539 | 静态设备描述、Ops与依赖handle。借鉴声明/初始化边界，不引入其内核/DT或把它当运行期产品能力求解器 |
| [Zephyr v4.0.0 kernel/init.c](https://github.com/zephyrproject-rtos/zephyr/blob/v4.0.0/kernel/init.c) | do_device_init305、z_sys_init_run_level347 | 固定init section/level及初始化结果；不等于每次用户改配置时的Resolver |
| [ESP-IDF Component Manager v2.1.2，固定SHA eee66010433fee745c491ea34c794387dddb5b54](https://github.com/espressif/idf-component-manager/tree/eee66010433fee745c491ea34c794387dddb5b54) | manifest/models.py:DependencyItem153、Manifest397；version_solver/version_solver.py:VersionSolver26、_solve105、solve147；dependencies.py:download_project_dependencies295 | manifest/version约束形成解并生成依赖结果，旧解失败可重新求解。它用于构建/下载依赖；不能移植Python/下载器、无界包搜索或声称其就是H7运行时Resolver |

实际读取的SHA256：Zephyr device.h `ac468322243c23abdc402a98efa25921fd451b699433e19ae0f2696b21d39f91`；kernel/init.c `83d6e64a6710a14d6485b2c28dcf8e62d8bd26efee17324ea8a62b41a2607e4b`。ESP固定SHA下dependencies.py `525c2caeb208d0988b5e8272003b1634eddaf52a245e702133f0e61fac8d01d3`，version_solver/version_solver.py `d32d120661277f8e2787427c0d5d18ef005b13a7ef7a3fb21df4b30933d84b30`，manifest/models.py `709606b850bad775e70628c4d69c2029f6b06a9ac7223e74760d12ca75177bf6`。源码未复制进仓库，未运行这些项目。

H7自行适配：固定已编译模块集合、纯能力匹配、确定性有界选择、独立Graph与资源预检，再生成异步计划。不是在MCU上运行构建系统或动态加载设备。

## 3. R2-14：静态注册描述与公开接口

候选上限：Module16、Provided32、Required64、Optional32、Conflict32、ResourceRequirement32、单计划64步骤、最多8次显式替代尝试。都是待审产品容量，不表示足以容纳全部未来控制/网络/AI拆分；超限报错，不默默扩容。

ModuleDesc候选字段：module_id、module_version、provided/required/optional/conflict表范围、PublicOps、LifecycleOps、ResourceRequirements、owner_role、flags、name。没有Frame地址/引用计数、运行态副本或任意HAL指针。module_id与Graph Entity ID、Control Client ID、consumer_id不同；版本匹配以Capability接口版本为准，Module软件版本只作兼容/诊断，不自动推导所有接口兼容。

推荐初期由Configuration的`const ModuleDesc *const catalog[]`装配，使用既有Framework Initcall一次绑定/校验，避免为静态注册另造注册任务、动态链表或擅改Keil section。后续如采用导出段，须另审ARMCC5保留/顺序规则；不改现有Device Registry。Module Registry只读描述，不持有模块Lifecycle状态，不能因查到描述就宣称模块可运行。

候选纯API：`ModuleRegistry_Validate(catalog,issue)`、`ModuleRegistry_Get(id)`、`DependencyResolver_Resolve(desired,registry,policy,scratch,out,issue)`、`PipelineSelector_Select(desired,resolution,graph,scratch,out,issue)`、`ExecutionPlan_Build(applied,target,capabilities,out)`。全部本地值接口，二进制ABI未冻结；无启动/文件/GetFrame副作用。公开状态转换由Module PublicOps消息端口发到原owner，不让Supervisor调用任意私有函数。

版本范围推荐显式[major.minor最小,最大]闭区间并默认相同major；不是在MCU解析任意SemVer表达式。范围不合法先拒绝。提供能力≠本次激活能力：同一Camera支持Raw/JPEG不意味着两种后端都被选中，互斥由本次绑定与Graph表达，不误拒支持多个格式的模块。

## 4. 运行期依赖解析算法

输入是不可变DesiredConfiguration、已编译Catalog、显式Provider策略；输出Resolution含根功能、选定模块/Provider、缺失Optional、依赖边、稳定拓扑序、资源需求和解释。每次配置重新计算候选，不直接改变正在运行模块。

1. 校验表范围、ID/版本/Ops/flag，未知或未实现根功能拒绝。控制面的恢复根依赖与用户媒体根功能分开，停媒体不删恢复根。
2. 将根模块/功能放入有界工作数组。用显式stack/工作表迭代展开Required；是逻辑递归闭包，禁止C递归调用。每模块每次尝试最多展开一次，所有索引先界限检查。
3. 按cap ID/版本过滤Provider。依次采用用户显式pin、描述preferred、产品显式rank；同优先级多个合法Provider返回AMBIGUOUS，不随链接顺序挑选。无Provider返回MISSING_CAPABILITY，版本不兼容返回VERSION_CONFLICT。
4. Optional默认只记录已有选中Provider，不自动拉起新模块；只有产品显式AUTO_OPTIONAL规则才允许补入。用户明确要求的“可选功能”在该请求中升为Required，不能悄悄忽略用户意图。
5. 为真实选中绑定建立Module Dependency Graph；重复Provider模块去重。检查排他能力/模块冲突，不能只检查类型名。对Required边作有界Kahn排序，环返回CYCLE及具体链，不靠init level覆盖依赖。
6. 若当前解失败且存在**显式声明、语义等价**的替代Provider/路径，可按policy顺序重算；每次从候选scratch重新构建，不改旧运行配置。最多8次，耗尽返回RESOLUTION_BUDGET_EXHAUSTED（不是已证明全局无解）。不静默降尺寸/FPS或更换用户功能；没有声明替代则原错误返回。
7. 选定模块资源需求按资源kind汇总：共享基础设备按实例去重，独占资源冲突拒绝，容量做溢出检查；不把不同单位直接加和。只生成需求，不分配/取得Frame或挂载卷。
8. 所有阶段成功最后提交out；失败out原字节不变，issue包含requirement/Provider/版本/首错/尝试序号。每次选择有可审计reason，不产生半个可执行解。

单次保守扫描上限：M16轮闭包×最多96需求×32Provider=49152次匹配，8尝试≤393216次；每次依赖矩阵≤16²=256，拓扑/冲突检查有界。不声称该数是CPU周期或实测WCET。初期配置角色协作分步推进，每轮比较预算候选64次并保存cursor，IRQ/高优先级媒体任务不执行全量Resolver；预算/响应时限待DWT审，不新增任务或调栈。

这不是通用无界SAT/PubGrub求解器；受限能力范围、确定策略和明确失败更适合MCU。复杂产品若需要更多搜索，先审容量/时限和方案，不在错误时偷偷启动硬件“试试看”。

## 5. R2-15：运行时Pipeline选择和四种机制

```mermaid
flowchart LR
  D[Desired Configuration] --> R[Dependency closure/provider binding]
  R --> M[Selected Modules]
  M --> P[Declared Pipeline candidates]
  P --> G[Pure Graph validation]
  G --> V[Pure resource-use capability validation]
  V --> E[Immutable Execution Plan]
  E --> S[Supervisor asynchronous execution]
```

Module Dependency Graph回答“需要什么模块/服务”，Media Graph回答“媒体从哪Pad到哪Pad”，Resource记录“谁持有哪张Frame”，Lifecycle记录“模块转换与完成”。四者不共用状态或计数。静态Entity/Pad/Link与固定Raw/JPEG Pipeline仍const；运行时Selection只能挑Catalog已经声明的路线/启用mask，不动态加载对象、不写Link对象的enabled字段。

必须区分Desired、SelectedModules、CandidateTopology、GraphValidatedView、AppliedConfiguration和实际RuntimeModuleState。Graph校验合法不是已应用，Resource预检合法不是G已授权，Apply成功也不是25FPS实测。Graph和资源检查先后独立，都通过才建计划。

PipelineSelector枚举≤8声明候选/合并组合，不穷举无限拓扑。产品policy提供有序允许组合（最多8个组合），每组合Graph/Resource各验证一次，失败保留原因；只在显式等价候选间替代，超过上限拒绝。候选组合表、用户pin与资源可用性共同决定结果；Supervisor不写USB_STORAGE等业务if/else。

例：用户启用USB+Storage，Resolver选Camera能力、UVC、Storage及其基础依赖；Selector匹配同源JPEG两路，Graph聚合纯能力，Resource只允许现两owner组合。Raw同格式双零拷贝需求即使图合法仍由Resource拒绝。不存在Ethernet描述则UNKNOWN_MODULE，描述FUTURE则NOT_IMPLEMENTED；不能注册一个ID就借USB/SD位发送网络。

## 6. Execution Plan与增量重配置

候选Plan是≤64步骤的有向无环依赖。16B步骤预算按module ID/transition各uint16（各先检查可表示范围）、先决mask两个uint32、配置绑定句柄uint32计算；ID与旧Task/协议ID显式映射，不直接cast。无业务组合分支。固定执行动词Configure/Activate/Deactivate/Drain/Cleanup/ErrorProcessing/Check/Report，动作实际由owner公开接口异步推进。Supervisor只持有计划cursor/命令身份/原结果，Lifecycle状态唯一保留在各Module。初期计划逐步串行，最多一个在途模块转换；DAG规定顺序/补偿，不承诺64步并行或另增一组待命令队列。

| 变化 | 计划规则 |
|---|---|
| 同格式启用新Sink | 检查模块/后端真实owner能力、已声明路由与资源容量；只准备/授权/激活新Sink，Camera既有格式不变 |
| 停单Sink | 本地Deactivation→原始访问结束→Return/ACK消费/退出核验→释放该G；另一Sink继续，不把BUSY当失败完成 |
| Sink采样/交付策略 | 只可使用模块声明支持的运行更新能力；当前UVC一次配置约束不支持就拒绝或走显式停止/重配 |
| Raw/JPEG、尺寸、像素/源改变 | 关闭全部受影响入口，旧G/交接真实Drain后唯一Camera停止，再受审的Cleanup/Configure；当前热重配能力未实现，拒绝宣称无缝 |
| 共享物理接口 | 保留Control基础依赖，不能为停UVC把控制Device也停；当前USB_Mode_Stop会调用USB_Device_Stop，需未来独立适配，不表示现已支持 |

共享基础设备是否继续活动由目标模块集合（Control恢复根与媒体根的并集）及公开能力计算，不新增一套Frame/全局资源引用计数；Registry本身不持有running或使用计数。未受影响模块仅在原配置绑定/依赖/资源能力仍有效时保留；共用Source格式变更使所有相关Sink受影响。计划构建失败不执行任何步骤。执行失败按已完成步骤反向补偿，但文件追加不能撤销，原错误和真实清理/Frame归还分别保存；不能“回滚配置结构体”就认为恢复硬件。

## 7. Ethernet/Wi-Fi/AI扩展审查

理想扩展只新增该模块描述/公开Ops、capability及依赖、合法Graph Entity/路线、资源使用适配及产品选择策略，不修改Camera/USB/Storage业务或Supervisor组合分支。Registry/Resolver处理声明即可。前提是后端已经具备所需能力；现Raw单IN_USE、JPEG仅原始/SD两owner，第三零拷贝消费者必须拒绝或另审后端能力，不能在Camera业务层偷偷模拟。

Registry不能推导物理DMA安全、协议端点或所有权。Network/AI仅提供控制服务而不持Frame时不是媒体consumer；真正持Frame才需独立G/H/M和全部退出证明。实际Network Task/driver/缓冲/能力版本均UNRESOLVED，本轮不实现。

## 8. 候选容量、预算与待审事项

按指针4B候选ModuleDesc48B×16=768B；Provided12×32=384B；Required20×64=1280B；Optional20×32=640B；Conflict12×32=384B；ResourceRequirement16×32=512B；每模块PublicOps12+LifecycleOps20共512B；名称≤512B；RegistryHeader48B；ProviderPolicy8×32=256B：总只读描述上限5296B，不含代码/对齐。精确字段/ABI未冻结，不在生产声明这些结构。

Resolver候选512B工作区：需求Provider索引192、依赖mask32、selected/visited/indegree/ready共64、显式stack64、topo32、候选模块32、cursor32、issue32、8次选择历史32。每次尝试重新计算，非完整解树缓存；具体实现需逐字段/循环/失败恢复核对。Plan64×16+header64=1088B运行候选。容量满拒绝，不动态扩容。

R2-14/15设计契约已给出，接口/字段/候选策略/64比较budget、16模块是否足够当前及未来产品，仍须人工审批。布局、ARMCC5/Map、Stack/DWT和所有Resolver/Control场景均NOT_RUN。Source中的Device/Initcall不被改写为这些能力已经实现；两个Resource NOT_PROVEN保持。
