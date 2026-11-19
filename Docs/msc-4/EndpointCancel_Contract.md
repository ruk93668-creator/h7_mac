# EndpointCancel 契约设计

日期：2026-11-19。源码基线：`30f4dd007b126bc2f90bdc7bb1818abeb65c3229`。范围：STM32H747 CM7、USB2 FS、当前USB PIO配置、MSC OUT2/IN2；不新增生产实现。

本文的“必须”是拟议接口要求，不代表现有代码已经做到。VERIFIED为指定源码/既有证据确认，INFERRED为推断，UNRESOLVED为未确定，NOT_TESTED为未验证。配套：[硬件证据](EndpointCancel_HardwareEvidence.md)、[停止生命周期](UsbStop_Lifecycle.md)、[测试计划](EndpointCancel_TestPlan.md)。

## 1. 当前实现与目标契约

VERIFIED：当前 `CM7/BSP/bsp_usb.c:337 BSP_USB_AbortEp` 调用 `HAL_PCD_EP_Abort`；本地LL `USB_EPStopXfer:915`同步轮询EPENA，超时返回HAL_ERROR。BSP在后续FIFO处理成功后才返回SYS_OK并归还其PIO占用。当前SYS_OK不是“仅提交取消”。普通Bulk不存在专用取消完成回调，不能把Data IN/OUT完成当作它。

本文保留Task层Cancel事务模型：**Request成功只表示登记；Cancelled才表示有对应取消完成证明；Class/Buffer能否释放还要检查事件引用和其他所有者。** 底层采用下述推荐的同步Abort，不把当前BSP API静默改成“返回OK但仍在取消”。Task事务的异步推进与硬件取消原语的同步语义必须分开。

### ADR-01：同步Abort与异步Cancel架构比较

| 维度 | A：同步有界Abort + Task分阶段协调 | B：底层异步Cancel请求/IRQ完成 |
| --- | --- | --- |
| 底层返回语义 | 一次调用返回已确认停止、准入忙或失败 | Request仅登记，Pending后由另一个证据路径终结 |
| 与当前HAL的匹配 | 直接匹配HAL_PCD_EP_Abort/USB_EPStopXfer的同步轮询形式 | 当前HAL没有普通Bulk专用取消完成回调，需要新事件/状态接口 |
| ISR职责 | 只登记生命周期原因和封锁准入；Abort移至Task | 还要关联EPDISD与取消身份并产生取消完成通知 |
| 时间与调度 | 每次同步调用必须有经过核验的最坏执行预算；阶段间返回Task调度 | 调用短，但Pending记录、通知可靠性与中断存活是核心依赖 |
| 正常完成竞争 | 在同步尝试前后按Transfer身份作唯一判定 | 硬件与Task、正常完成与取消通知四者需共同线性化 |
| HAL前后取证 | 仍需观察旧IRQ、FIFO及PIO边界，但不等待不存在的取消回调 | 必须保证HAL清EPDISD前已保留带身份的取消事实 |
| Core同步DeInit | Task先Prepare，Core回调只做同步Commit | 也必须先Prepare，不能直接把Pending返回给Core |
| 主要风险 | ISR中使用轮询、预算不可靠、同步原语内部依赖被屏蔽IRQ、旧OUT停用错误 | 事件丢失/错配、关闭IRQ后永不完成、重复通知/资源退役、更多接口及RAM状态 |
| 当前适用结论 | **推荐的近期实现架构**，但须先修复/核实同步原语的完成证明和最终Stop契约 | 备选，只有证明A的时延预算不可接受或确有异步需求时再单独审查 |

推荐依据是当前实际HAL形式及既有缺口，不是断言当前Abort实现正确。现有OUT2取消失败、共享RX序列和BSP Stop过早关IRQ仍是后续实现阻塞；增加Task状态机不能自动消除这些问题。本轮不改任何接口或代码。

该选择针对当前单Class MSC/默认UVC。多个活动OUT之间可能互相触发共享RX guard，不能假设“轮流重试BUSY”一定会收敛；若形成依赖环，需受控失败并单独解决H-U2，不能清其他端点busy来强行推进。

### 推荐方案中Request/Pending/完成通知的落点

1. Task层Request登记Stop/目标并关闭提交准入；可返回accepted/Pending，表示排队或准备工作未完成。
2. 只有资源前置条件满足后，Task执行一次同步Abort。不能在ISR内、Core同步DeInit回调内或持有会阻止必要进展的长临界区里执行。
3. 同步Abort返回成功后，结合对应Transfer/PIO/事件屏障取得终结证明；Task发布逻辑cancel_complete。它是**Task确认后的通知**，不冒称HAL提供了硬件取消回调。
4. SYS_ERR_BUSY按来源记录为未满足的前置条件，不默认表示硬件取消Pending；HAL_ERROR按失败记录。详见硬件证据文档的D-01～D-06。
5. 不等待一条在当前HAL中不存在的取消完成IRQ来结束同步调用。同步原语的完成条件必须由硬件或原语自身受控的处理推进，不能依赖被它屏蔽的IRQ、其他Task或上层回调；无法证明这一点时A方案也不得实施。
6. 同步调用耗尽预算后，取消尝试Failed；后续迟到硬件证据仍可帮助旧Stop收尾，但不把错误改写成成功。异步备选B的“长时间Pending等取消事件”不能套在A的HAL_ERROR上。

## 2. 参与者、身份与引用

| 对象 | 职责 | 不得据此推断 |
| --- | --- | --- |
| Endpoint direction | `0x02`与`0x82`分别管理，EP0另有控制事务约束 | 同编号两个方向都已经停止 |
| Transfer record | 描述一次驱动传输及其PIO分包、buffer、长度、完成证据 | 单个包XFRC代表整次传输完成 |
| Cancel record | 针对确定Transfer或已验证Idle状态的一次取消尝试 | 同地址当前新Transfer就是取消目标 |
| Stop transaction | 聚合OUT/IN取消、回调排空、Class与PCD状态 | Cancel完成代表整个Stop完成 |
| Session record | 记录一次已接受的USB会话与端点incarnation | 新Session创建使旧引用消失 |
| Notification record | 持有事件身份和必要的buffer/context引用直到消费确认 | 通知已排队代表消费者已完成收尾 |

身份字段至少包含：`boot_scope`、`session_id`、完整端点地址、`endpoint_incarnation`、`transfer_id`、`cancel_id`、关联`stop_id`。`boot_scope`是本次初始化生命周期的作用域；API不得接受跨MCU重启保存的旧Token。不能用随机数“极少碰撞”代替同一作用域内的确定唯一性。

Session、Transfer、Cancel、Stop使用不复用的单调身份；0保留为无效/无Transfer。具体位宽待实现审查，允许单调64位计数，但CM7上的读写必须串行保护，不能假设64位原子。任何计数耗尽都拒绝新建；旧收尾仍须可执行，禁止清零绕回、USB Reset清seed或拿地址当身份。

驱动登记身份与硬件arm必须是一个受保护的操作：先建立引用和映射，再开放本轮传输。回调/事件在产生时固定旧身份，不得出队时套用“当前Session”。硬件IRQ本身不携带软件Token，详见硬件文档中的arm/retire屏障；没有该屏障就不能声称Token匹配已消除旧IRQ污染。

同一Transfer最多有一个进行中的取消尝试。同Token、同目标的重复Request返回同一记录；同Token、不同目标必须拒绝。不同Token对同一仍Pending目标返回冲突，并提供已有操作身份供Stop合并，不能发两次相互覆盖的硬件取消。

## 3. 状态及完整语义

以下状态属于Cancel尝试，不覆盖Transfer和Stop的独立状态。

| 状态 | 精确定义 | 允许的副作用/结果 | 所有权 |
| --- | --- | --- | --- |
| Request | 调用与准入阶段；核验身份、目标、调用上下文和记录容量，并原子封锁该目标的新arm/PIO续传 | 被拒绝时不产生Cancel；被接受后必须有可查询记录 | 保留全部既有引用 |
| Pending | 已登记并可能已提交硬件请求，但还没有完整停止或正常完成证明 | Request可返回accepted；查询仍Pending；不向业务发送虚假成功 | 驱动/事件/后端继续持有各自引用 |
| Cancelled | 取消获胜，已证明目标方向不再访问该Transfer buffer，旧硬件事件已归属并受arm屏障约束，驱动不会再续传 | 发布一次语义上的cancel_complete，含证据与已完成前缀计数 | 只退役硬件/驱动访问；排队事件、消费者及SD IDMA引用仍单独保留 |
| Normal Complete | 确定该Transfer已正常完成，取消没有终止它；必须有整次Transfer证据 | 发布一次该终结结果；Stop后仅作所有权收尾，不再启动新CBW/CSW/数据业务 | 同样要等待事件消费和上层归还 |
| Failed | 当前取消尝试无法安全完成或证明结果，例如硬件失败、超时、证据归属不明、事件证据丢失 | 保存首个错误、阶段、寄存器与原身份；Stop进入RECOVERY_REQUIRED | **Failed不等于已静止，更不等于已释放** |

被拒绝的Request与已接受后Failed必须区分。参数错误、foreign Token、无记录容量的Request没有硬件副作用；若目标已有在途事务，拒绝不改变其所有权。

### Idle和已经完成的目标

- 如果Request线性化时已存在Normal Complete记录，返回该事实，不伪造Cancelled。
- 空闲端点测试允许`Cancelled(reason=ALREADY_IDLE, transfer_id=0)`，但必须已有可靠Idle证据：没有硬件访问、PIO续传、旧原始标志和待交付旧引用。它是取消的空操作完成，不能编造EPDISD或声称取消了某个Transfer。
- 软件busy=false或EPENA=0单独都不是Idle证明；无法确认时保留Pending或Failed，不用“空闲推测”释放上下文。

### Failed之后的迟到证据

Failed结果不能被迟到IRQ改写成历史上“从未失败的成功”。旧Cancel记录及目标映射保留，迟到的精确匹配证据仍交付旧Stop做收尾；记录`late_quiescent_proof`和新的清理事实。是否继续旧Stop由生命周期中的恢复条件决定，不自动重启USB。

若需重新发起取消，使用新Cancel身份并显式关联旧Transfer/Stop；不得复用旧Token。旧事件只更新旧记录或其tombstone，不能转移给新尝试。新会话arm仍必须等该方向旧引用和事件屏障完成。

## 4. 正常完成与取消竞争

拟议线性化点：在同一个USB同步域中，将Transfer从可推进态原子转成“唯一终结决定”。IRQ与Task不能各自无锁判断后各发一次终结事件；保护范围必须覆盖arm映射、取消准入和终结登记，不在此保护中等待硬件或执行文件/SD操作。

| 可证明的事实 | 终结决定 | 业务与回调规则 |
| --- | --- | --- |
| 整次正常完成已登记，然后Request到达 | Normal Complete | 返回原Transfer结果，Stop已封锁业务则只归还旧引用 |
| Request已登记，但硬件正常完成先于停用生效且整次完成可证明 | Normal Complete | 取消输掉竞争，不再额外生成Cancelled |
| 有匹配的停用证明，尚无整次正常完成证明 | Cancelled | 仅报告已证明的完成前缀，不能补足未发送/未接收字节 |
| XFRC与EPDISD同时出现，能证明整次正常完成已发生 | Normal Complete | 停用作为同一Transfer的附加硬件收尾证据，不能重复释放 |
| 两种标志同时出现但仅是一个PIO包XFRC，后续包尚未发送 | Cancelled或Failed | 若停用证据完整则Cancelled；不能把包完成当整笔完成 |
| 只有旧标志/时间顺序不明/身份映射已丢失 | Failed(AMBIGUOUS_EVIDENCE) | 允许保存后来取得的静止证据，但不伪造正常成功 |

IRQ位同时存在只说明观察到一组事实，不提供精确的先后时间。不能给所有竞争设置一个无条件“取消优先”或“XFRC优先”规则。Normal Complete表示USB驱动层传输完成；它不证明PC已落盘，也不证明整个SCSI命令、文件系统或SDMMC同步完成。

## 5. 完成证明与所有权边界

取消完成证明需保存：目标身份、请求/观察序号、对应端点的原始及使能中断状态、EPENA/EPDIS等控制状态、是否已停止PIO续传、旧事件如何处理、字节计数依据，以及是否仍有驱动访问。

`EPDIS=1`仅表示停用请求；超时仅表示没按期拿到证明。目标已arm时采用匹配EPDISD、EPENA清除及驱动停用屏障的组合证明；具体适用条件、特别是EP0与全局NAK/RX处理，须通过硬件文档的未解决项核验。不得把此设计要求冒称为已经核实的所有硬件保证。

引用按实际所有者释放：

1. 硬件静止后才能撤回硬件访问权；驱动PIO续传必须已封锁。
2. 已排队的旧数据/取消通知保持自己的记录和buffer引用，直到消费者确认。查询结果不能提前释放这些引用。
3. Cancel通知只允许旧Stop推进收尾；不能为新Session递增传输计数、启动下一包或写CSW。
4. Class Release必须等旧数据通知、cancel_complete及其他调用栈引用排空。已发布取消结果不等于Class可立即Free。
5. SDMMC IDMA/Worker若还持有同一数据缓冲，其所有权独立保留。USB Cancelled、USB Reset或Token取消均不授予SD缓冲重用权；RAW租约不转FatFS。

## 6. 通知、查询与重复操作

拟议Task层Request返回值只说明准入；Task事务状态通过查询和独立cancel_complete通知一致表达。推荐A由Task在同步证明后发出该通知，备选B才由底层异步证据触发；两者不混用。这里不提供可被误编译的生产接口实现。

- 每个接受的Cancel至多作出一次终结决定。通知传输允许重试，但通知身份固定，消费幂等；“通知重试”不等于再次取消或再次释放。
- 通知必须携带完整目标及Stop身份、结果、完成前缀和证据索引，不能只传endpoint或当前epoch。
- 查询无副作用，不修改终结状态，不代替通知消费确认。通知队列满时保留可查询记录及待通知引用；若无法保留，进入受控故障，禁止静默丢事件并释放资源。
- 同Stop重复Request合并；同方向的新Stop不能夺走旧取消的收尾资格。Stop超时后仍接收旧完成证据。
- tombstone/终结记录仅在所有消费者确认、旧原始IRQ已隔离且不可能被误配后退役。退役后相同旧Token查询返回STALE/UNKNOWN，不能返回当前新事务状态。
- 终结记录容量、事件队列深度和身份计数溢出都属于显式准入限制。不得为腾空间强制清busy、丢旧回调或复用Token。

## 7. 未解决项与审查门槛

| ID | 状态 | 事项 |
| --- | --- | --- |
| C-U1 | UNRESOLVED | 推荐同步原语的时间/进展证明与HAL前后arm屏障如何实现，见H-U3 |
| C-U2 | UNRESOLVED | 事件队列、tombstone容量及消费确认接口，需要结合RAM预算定稿 |
| C-U3 | UNRESOLVED | Failed之后的显式清理继续策略及结果查询形式，不能混淆错误历史与后来静止事实 |
| C-U4 | NOT_TESTED | Core采用Prepare/同步Commit设计，见L-U1；EP0门控、调用预算及实现验证仍待完成 |
| C-U5 | NOT_TESTED | 全部竞争、重复、延迟和跨Session验收；当前无新增实现可供执行 |

设计冻结前必须证明同一Transfer不存在双终结/双释放，且每个释放动作都能追溯到硬件证据和消费者确认。上述未解决项是后续实现门槛，本轮不以空实现或假回调补齐。
