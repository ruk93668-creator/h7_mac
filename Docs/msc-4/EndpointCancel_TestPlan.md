# EndpointCancel与USB Stop测试计划

日期：2026-11-19。设计审阅基线：`30f4dd007b126bc2f90bdc7bb1818abeb65c3229`。**本轮只交付计划，以下用例全部NOT_TESTED，不新增测试代码或实板操作。** 旧测试/快照仅作背景证据，不能计入新契约通过率。

依赖：[契约](EndpointCancel_Contract.md)、[硬件证据](EndpointCancel_HardwareEvidence.md)、[生命周期](UsbStop_Lifecycle.md)。UNRESOLVED项未解锁前，只能测试其保守失败规则，不能把假设写入模型后宣称真实硬件正确。

## 1. 验证层次与统一判定量

| 层次 | 目的 | 不能宣称 |
| --- | --- | --- |
| 软件事件竞争/故障注入 | 确认身份、唯一终结、引用、状态推进和重复操作 | HAL桩返回OK等于硅硬件停止 |
| 寄存器模型与HAL前后证据 | 验证W1C、延迟/同时事件、mask和共享RX的处理 | 未核实的寄存器模型代替RM0399或实板 |
| RAM MSC实板 | 排除SD依赖，验证实际重连、IRQ与端点状态 | 看到盘符等于协议和取消全部通过 |
| SD只读及默认UVC回归 | 验证独立IDMA/RAW租约与原Class无退化 | RAM通过等于SD/UVC通过 |

每一步都观察独立账本，不只检查最终返回码：

- `StopId / Session / EndpointIncarnation / TransferId / CancelId`、状态及首个错误。
- endpoint硬件活动、driver busy/PIO续包准入、buffer各类引用、通知发布/消费/确认次数。
- Class方向资源、context引用、释放调用次数及实际释放次数。
- NVIC、GINTMSK/端点mask、BSP/Platform started、PCD状态与硬件地址。
- SD Worker/IDMA实际活动与完成、RAW owner/client、FatFS准入。

基本oracle：每个Transfer最多一个终结决定，每个引用释放一次；无硬件/驱动访问时才可撤回其引用；Class Free晚于所有旧context消费者退役；PCD_STOP晚于必要收尾；未知身份不能落入新会话；超时不改变物理所有权。

测试oracle应独立于被测状态机，不复制其分支来制造全绿。旧断言保留；对当前基线运行同一历史用例，新增失败单独列出。模型的“安全锁定通过”和产品“恢复可用性通过”分开报告。

## 2. 软件取消与完成竞争矩阵

每行均须运行IN2/OUT2适用方向，至少覆盖IRQ先、Task先及同批事件；记录事件顺序而不是依赖固定sleep。主线按ADR-01的推荐A运行：Task协调、底层同步有界Abort；备选B如另行试验必须单独标记，不能混用通过率。

| ID | 安排/注入 | 预期结果与关键断言 |
| --- | --- | --- |
| EC-01 | 可靠Idle后Request | Cancelled(ALREADY_IDLE)，不伪造EPDISD；无额外buffer释放 |
| EC-02 | 正常完成记录先于Request | Normal Complete；取消不再生成第二次终结 |
| EC-03 | Request后XFRC先完成整次Transfer | Normal Complete；停止准入仍关闭，不发送后续业务 |
| EC-04 | Request后停用证明先到，XFRC迟到 | Cancelled；迟到XFRC仅归属旧记录，零新Session副作用 |
| EC-05 | 同一观察批次XFRC+EPDISD，整次完成可证明 | 唯一Normal Complete；停用只追加收尾事实 |
| EC-06 | 同批仅一个PIO包XFRC+EPDISD，后续包未发 | 不得报整次正常完成；有完整停止证明才Cancelled |
| EC-07 | 并存标志无法可靠归属 | Failed(AMBIGUOUS_EVIDENCE)，保留引用及首个现场 |
| EC-08 | 正常完成通知已排队，Request到达但消费者未ack | 正常事实保留；Class/事件引用不得提前Free |
| EC-09 | IN2多包PIO完成一包后Stop | 新续包被封锁；原包计数真实，未发部分不计成功 |
| EC-10 | OUT2 CBW接收已arm但尚未收到数据时Stop | 保留64字节接收容量及记录直到取消/正常完成证明；不启动新命令 |
| EC-11 | OUT2 Data OUT只收到前缀时Stop | 报告可证明前缀；剩余数据不得当作新CBW或产生新Worker Submit |
| EC-12 | Cancel请求被底层拒绝/未登记 | 明确区分拒绝与Pending，目标引用不变，无虚假通知 |
| EC-13 | 取消已登记、同步尝试的EPDISD/静止证明被延迟 | 预算内保留Pending/调用中所有权；同步预算耗尽后为Failed，不无限等待一条不存在的取消IRQ；未静止不释放 |
| EC-14 | Request计时超时后EPDISD迟到 | Failed历史不改写；旧Token仍可完成旧Stop收尾，业务继续封锁 |
| EC-15 | PIO driver idle但SD IDMA仍引用同一buffer | USB静止不能释放/重用SD引用，不转交FatFS |

每个竞争用例检查通知发布的语义次数、传输结果、引用归还、Class引用和Stop后业务提交计数；不能只检查“没有崩溃”。

## 3. 身份、旧Session与重复事件

| ID | 安排/注入 | 预期结果 |
| --- | --- | --- |
| ID-01 | 同Token/同目标重复Request | 返回同记录，不重复硬件取消或释放 |
| ID-02 | 同Token但端点方向、Transfer或Session不符 | 拒绝，不写新硬件请求、不改旧所有权 |
| ID-03 | 同Transfer已有Pending，另一Token到达 | 显式冲突/合并Stop，不覆盖原取消记录 |
| ID-04 | 当前Session已失效，旧cancel_complete到达 | 精确路由旧Stop，允许收尾，不因epoch不等丢弃 |
| ID-05 | 尝试在旧引用未排空时arm新Transfer | 准入拒绝；不得先复用端点再依赖Token过滤 |
| ID-06 | 旧屏障完成后新Session arm，再注入重复旧通知 | tombstone/幂等消费，无新busy/计数/业务变化，无旧context解引用 |
| ID-07 | arm前已有旧原始XFRC/EPDISD | 不把它贴上新Token；屏障未满足时禁止arm |
| ID-08 | Token、Stop或Session计数耗尽 | 拒绝新建；旧Pending仍可收尾；不清seed或绕回 |
| ID-09 | 通知丢失但记录仍保留，消费者查询/重试 | 结果一致，通知可幂等补交，确认前不释放记录引用 |
| ID-10 | 通知/记录容量耗尽 | 限制新准入或RECOVERY_REQUIRED；不丢证据后重用内存 |
| ID-11 | 重复Stop、重复Disconnect、Reset与Stop交错 | 同一个清理事务维护原因集合，无并行Free/PCD Stop |
| ID-12 | 旧失败尝试后新Cancel重试，旧通知再次到达 | 旧通知只作用于旧记录及共享目标收尾账本，不能终结新Token两次 |

身份测试必须模拟硬件没有Token这一现实：不能给寄存器事件天然附上正确软件Token，再据此声称解决ABA。要测试arm映射何时建立、旧flag何时被消费，以及HAL回调中重入是否被禁止。

## 4. 端点停止、共享RX与HAL边界

这些模型的物理语义须标记来源；H-U1/H-U2未核实的分支只作假设探索，不能计为硬件通过。

| ID | 安排/注入 | 预期结果 |
| --- | --- | --- |
| HW-01 | EPDIS写入但EPENA不清，EPDISD未到 | Pending/Failed；不清busy、不释放引用 |
| HW-02 | EPENA=0但只有单包XFRC，PIO还可续传 | 不判取消完成；driver准入与整次Transfer证据仍必要 |
| HW-03 | EPDISD被mask，raw flag已置位 | 记录raw与mask差异；不能既忽略所有权又声称完成 |
| HW-04 | HAL前EPDISD为1，HAL后已应答为0 | 用保留的旧身份/证据收尾；不依赖事后再次看到同一flag |
| HW-05 | HAL处理期间新XFRC到达或Setup/Reset改变Session | 不把新旧事实拼成假完整证明；无丢Setup/双终结 |
| HW-06 | 对目标应答旧EPDISD，同时存在XFRC/STUP/其他端点事件 | 精确应答；禁止全1或W1C读改写误清新事件 |
| HW-07 | OUT2取消时RX首项是EP0 Setup，后面是NAK状态 | Setup不被丢弃/当NAK弹出；不能安全处理时锁定 |
| HW-08 | RX先有其他OUT数据，后有目标OUT/NAK条目 | 保留其他端点数据与次序，不全局flush冒充目标取消 |
| HW-09 | Global OUT NAK请求已写但尚未生效 | Pending；请求位不等于生效状态，不清他人拥有的NAK |
| HW-10 | 目标静止后、释放NAK前后新Setup到达 | 不被随后共享RX Flush丢弃，不污染新控制事务 |
| HW-11 | USBRST与XFRC/EPDISD同次IRQ出现 | 保留旧收尾身份；HAL清位后不补造未捕获的时序 |
| HW-12 | HAL取消返回错误且EPENA仍1/EPDIS仍1 | 保留现场和引用，不将HAL READY/UNLOCKED当成功 |

原始MMIO读数需要时间/读取顺序，不能把逐寄存器读出的组合假定为硬件原子快照。模型测试还要覆盖有读副作用的RX状态弹出、W1C、只读/命令位区别；禁止用软件写EPDISD来伪造真实硅硬件事件。

## 5. Stop、Class与PCD故障注入

| ID | 安排/注入 | 预期状态、所有权和IRQ |
| --- | --- | --- |
| ST-01 | OUT2/IN2均Idle时主动Stop | 状态逐步前进，无多余终结，Class Release先于最终PCD Stop |
| ST-02 | OUT2正在接收CBW，IN2空闲时Stop | 无新CBW业务，等待OUT证明，IRQ不因Pending被无条件关闭 |
| ST-03 | OUT2 Data OUT活跃时Stop | 排空旧前缀/取消通知；不向新会话提交剩余数据 |
| ST-04 | IN2 Data IN活跃时Stop | 保留源buffer，封锁续包，完成/取消竞争唯一决定 |
| ST-05 | OUT取消失败但IN成功 | 真实部分进度保留，OUT资源不释放；不因IN成功宣告全Stop成功 |
| ST-06 | Cancel完成通知延迟，Task仍推进 | 保持DRAIN_CALLBACKS/RECOVERY_REQUIRED；通知到后可收尾旧Stop |
| ST-07 | Class Release部分失败 | 无重复Close/Free已完成步骤，保留失败方向和原context所需引用 |
| ST-08 | Class已释放，PCD Stop返回失败 | 报告“Class已释放、PCD未停止”两事实；不报STOPPED，不复活旧Class业务 |
| ST-09 | PCD调用前发现仍有事件/IDMA引用 | 禁止进入最终PCD Stop，不依靠关IRQ掩盖未排空 |
| ST-10 | Stop期间Data/Cancel回调迟到 | 只收尾旧目标，不发送下一包/CSW，不引用新context |
| ST-11 | Reset与Disconnect、主动Stop交错 | 原因合并，旧Token失效但旧取消证明仍有效，退出策略明确 |
| ST-12 | 重复Stop/释放及恢复后旧回调 | Free计数不增加；新Session业务和buffer不变 |
| ST-13 | 超时后迟到证明满足恢复条件 | 保留Failed历史，可继续清理；不得自动Start或开放FatFS |
| ST-14 | 底层第一次Stop失败，再次查询/重试 | 失败现场未被重试日志覆盖，IRQ、PCD、Class状态均如实可查询 |

对所有失败分支检查：资源是否保留、是否仍有可服务旧收尾的路径、是否无重复释放。若为硬件安全选择锁定，可记“安全保持通过、恢复可用性未通过”，不能合成一个PASS。

## 6. RAM MSC优先的实板流程

前置：固定电脑、USB端口、线缆；SD卡在板卡全部断电后取出。记录固件ELF SHA256、源码提交、诊断开关、实际RAM容量。禁止初始化或格式化；不依赖F:是否存在判断枚举。先确认旧地址探针禁用，新镜像按其DWARF取证。

每次连接前启动USBPcap，确认捕获在运行。捕获中断/忘记启动的该次时序项目标NOT_TESTED，不能借旧抓包补齐。暂停调试的时刻单独记录，避免将Keil Break造成的超时误判为设备自然故障。

| ID | 板级操作 | 必须保存和核对 |
| --- | --- | --- |
| BR-01 | 冷启动首次枚举 | Device Descriptor、可观测的地址/配置事务、PnP、Core/PCD/硬件地址及端点状态 |
| BR-02 | 不复位正常断开重连 | 连接前后的控制请求及Stop/会话事件、Class方向状态、取消证据；成功后执行INQUIRY/READ CAPACITY/READ(10) |
| BR-03 | OUT2等待CBW时断开 | 断线时确实已arm的证据、取消结果、通知排空、重连后命令恢复 |
| BR-04 | OUT2 Data OUT活跃时断开 | 仅无SD卡的RAM LUN受控协议测试，证明实际活跃窗口及完成前缀，不进行文件格式化 |
| BR-05 | IN2 Data IN活跃时断开 | 使用受控异步读或重复读与事件证据关联；不能仅凭“运行2秒后拔线”宣称命中活跃窗口 |
| BR-06 | 主机USB Reset | Reset前后旧目标、EP0/地址及新Session，不能用MCU Reset替代 |
| BR-07 | 延迟/迟到端点事件 | 需要真实来源及时间线；若只能做软件注入则板级该项仍NOT_TESTED |
| BR-08 | 连续10次不复位重连 | 每次独立捕获/状态日志、重新执行三条命令，逐次检查Tag/CSW/Residue和所有权 |

十次序列不得中途靠MCU复位、重烧或强制PCD重启续算。失败保留本次文件、时间、状态及失败序号；修复后重新开始计数，旧失败不删除。单纯主机重试后出现设备与“本次状态机全程正确”分开评价。

USBPcap处于Windows栈层，可能包含注入描述符，且不一定暴露所有地址0/SET_ADDRESS底层事务。缺少事务时明确记录采集能力限制；用硬件DCFG与Core/PCD快照作独立证据，但不能写成抓包已经看到该事务。

## 7. SD只读与默认UVC回归

- SD组：全部断电后装回原卡，明确预处理BOOT_MSC=1、SD_TEST=1、WRITE_ENABLE=0；确认RAW Claim、FatFS无访问、512字节容量、专用IDMA区未变。重复枚举、读取和重连矩阵，另外证明Worker/IDMA完成及RAW租约保持。不得发送真实SD WRITE、格式化、修复或创建文件。
- UVC组：保护真实卡，按原硬件/摄像头条件测试默认模式的首次枚举、重连与视频流程；不改Camera/Media路径。若因已存在硬件初始化问题无法启动，记录阻塞，不改旧断言或绕过启动条件。
- USB取消完成与IDMA完成必须分别采集。旧读得到同样哈希只能证明重复一致；没有独立原件时不标原始数据完整性通过。
- 每次保存原始捕获、ELF/hash、协议元数据、Core/PCD/端点/IRQ状态、Stop/Session/Cancel事件和资源账本。整Hub抓包与卡数据保留本地；Git只放授权的脱敏证据/报告，不上传无关设备内容。

## 8. 方案未解决项与通过标准

| ID | 状态 | 问题 |
| --- | --- | --- |
| T-U1 | UNRESOLVED | RAM分阶段Data OUT/活跃IN取消的受控主机工具尚未选定；普通同步SPTD无法证明任意时刻命中 |
| T-U2 | UNRESOLVED | 不伪造硬件位的板级延迟EPDISD/迟到IRQ方法，需要合法诊断机制及独立证明 |
| T-U3 | UNRESOLVED | 寄存器模型取决于H-U1/H-U2手册与共享RX序列核验 |
| T-U4 | UNRESOLVED | Core Prepare/同步Commit方案已确定；其EP0门控、等待窗口及最终PCD停止原子化的可执行接口/预算待核验 |

本轮文档审查通过标准：四份文档齐全且相互一致；每个关键硬件论断链接源码/手册或明确标未解决；没有把计划写成已运行测试；不修改生产代码。

后续实现通过标准：全部必需软件矩阵、对应硬件证据、10次重连、三组回归分别通过，所有失败历史保留。仅四份文档完成不构成代码实现、实板验收或MSC-5批准。

## 9. 推荐同步架构及诊断来源测试

| ID | 注入 | 必须核对的区别 |
| --- | --- | --- |
| AR-01 | BSP共享RX guard返回BUSY | HAL调用次数=0、hardware_attempted=false，Pending依赖等待；不是硬件Cancel Pending |
| AR-02 | 本地LL预算耗尽返回HAL_ERROR | 工程码-9、phase=ABORT、证据未知/失败；不能改记成-5或原始HAL_TIMEOUT |
| AR-03 | HAL Abort成功、Flush失败 | 保留“停止阶段成功、收尾失败”两事实，不提前归还buffer |
| AR-04 | Class释放汇总-9但前一方向曾BUSY | 首个guard来源记录仍存在，不由汇总码伪造原始HAL_ERROR |
| AR-05 | Task单次同步Abort执行期间 | 无SD等待、无上层回调依赖、无等待被自身屏蔽IRQ的死锁；预算及进展来源可证明 |
| AR-06 | 同步Abort已证明成功，但Task逻辑完成通知延迟 | 不等待HAL取消回调；保留通知引用，Task通知消费后旧Stop可继续 |
| AR-07 | 当前正常数据回调与Task同步取消竞争 | 按Transfer唯一终结；不为等待一个已被取消取代的正常XFRC而永久卡住Drain |

AR-05必须测量实际最坏时延和中断影响；10000次循环不是时间单位。不能因同步方案比异步方案简单，就省略硬件进展证明或承诺当前旧HAL能够可靠取消OUT2。

## 10. Core Prepare / 同步Commit协调测试

| ID | 场景 | 预期 |
| --- | --- | --- |
| CO-01 | Reset时旧OUT2活跃 | ISR只登记并封锁，尚无Ready Ticket时Core DeInit调用数=0；Task Prepare后同步Commit一次 |
| CO-02 | Disconnect后Prepare等待Worker/回调 | 不在ISR/DeInit里等待，IRQ/收尾路径可用，Class引用保持 |
| CO-03 | 没有Ticket或Ticket属于旧Class generation却调用Commit | 拒绝并锁定；不得返回假USBD_OK或释放新对象 |
| CO-04 | SET_CONFIGURATION(0)在Configured中到达 | Core入口先门控；无Prepare证明不改配置、不提交成功状态包；Ready后只分发一次 |
| CO-05 | Core忽略ClrClassConfig失败并尝试CtlSendStatus | Platform按失败cookie阻止真实成功提交，保留失败事实；不能用Core返回OK报通过 |
| CO-06 | Prepare期间新Setup替换旧请求 | 旧回复资格作废但旧资源继续清理；不能把旧状态包交给新Setup |
| CO-07 | Reset在Prepare或Commit边界再次到来 | 合并原因，按最新Reset边界重建；旧资源只Free一次、旧完成仍可归还 |
| CO-08 | Core Reset把dev_config清0后传0给DeInit | 仍按PreparedTicket的Class实例清理，不能凭config0跳过旧资源 |
| CO-09 | 正常重复SET_CONFIGURATION(1)且无停止事务 | 不触发无谓Stop/DeInit；现有Class工作保持 |
| CO-10 | UVC活跃流SET_INTERFACE Alt0 | 仅Stream Prepare；同步Setup Commit后Alt0，Class不Free、PCD不Stop |
| CO-11 | 标准请求误走现有Class Deferred Status API | bmRequest非0x21应拒绝；不得全局放宽BUSY匹配来让测试通过 |
| CO-12 | Prepare超过主机/生命周期等待窗口 | 明确控制请求/恢复失败，资源不伪释放；不得无限NAK后仍宣称枚举成功 |
| CO-13 | 直接Core Stop/DeInit/Unregister旁路、首次Init部分失败 | 仍验证独立Ready证明；不能在Core回调中开始等待，不借旧Ticket释放新对象 |
| CO-14 | EP0旧回复仍引用即将释放的Class | 即使数据端点已Close也不得Free；当前新Setup仍保留独立cookie及准入 |

观察Core调用栈、DeInit调用/Free次数、Ticket消费、8字节Setup副本/cookie、Reset generation、真实EP0提交及完成，不能只检查最终dev_config。

## 11. BOT Reset Recovery专项

VERIFIED（现状源码）：`usb_msc_class.c:133`严格匹配bmRequest0x21、bRequest0xFF、wValue0、wIndex0、wLength0，登记Deferred Status；`usb_msc_bot.c:263`进入BOT_RESET_DRAIN，Task排空后提交状态、等待真实完成、再等待双方向Clear HALT。现状BotReset仍直接尝试Abort，Task Drain也循环调用；新设计要求硬件同步Abort只在Task执行，ISR不得据此阻塞。

BOT Class Reset不是USB Bus Reset：目标保持地址/配置和Class实例，不走全设备DeInit/PCD Stop。Reset控制请求以EP0状态阶段响应，不是生成CSW。各专项全部NOT_TESTED。

| ID | 场景 | 预期及证据 |
| --- | --- | --- |
| BRR-01 | 合法Class Reset，端点/后端空闲 | 严格匹配的BUSY可延迟状态；Task证明排空后才提交EP0状态，不提前响应 |
| BRR-02 | 无效方向/接口/Value/Length或未Configured | 合法拒绝/STALL；没有Token或取消副作用，不能因全局BUSY放宽而通过 |
| BRR-03 | READ Data IN/WRITE Data OUT中Reset | 仅RAM允许受控WRITE；旧Transfer被终结、无新CBW，保留后端/PIO引用直到证明完整 |
| BRR-04 | 同步Abort返回BUSY、HAL_ERROR、Flush失败 | 分阶段原因正确；BUSY等待依赖，HAL_ERROR不冒充Pending，失败不提交成功状态 |
| BRR-05 | Storage Worker/IDMA仍未drained | EP0状态保持未完成；USB Token取消或Reset不代表SD sync/Buffer释放 |
| BRR-06 | 状态包提交成功但IN完成尚未到 | 保持等待完成，不计Reset Recovery完成，不因CtlSendStatus的OK伪造主机确认 |
| BRR-07 | 提交失败、确认丢失或超时 | 故障锁定，Token及资源状态真实；迟到事件不能自动重启数据链 |
| BRR-08 | 新Setup/Bus Reset在状态ACK前替换请求 | 旧Token失效，不续接旧状态；原资源清理仍可完成 |
| BRR-09 | 状态已真实完成，随后合法Clear HALT成为新Setup | 历史完成事实仍可用于恢复，不能因新Setup取消当前资格而抹掉既有ACK事实 |
| BRR-10 | Clear HALT两方向按任意顺序到达 | 只在Reset状态完成后记录0x02/0x82各自证明；两者都满足才重新接收CBW |
| BRR-11 | 缺少一方向、重复同方向或过早Clear HALT | 不提前进入BOT_CBW；取消/Abort本身不能代替主机Clear HALT |
| BRR-12 | Class Reset后第一个新CBW及迟到旧数据 | 旧通知不成为新CBW/数据；Tag/CSW/Residue及INQUIRY/READ CAPACITY/READ(10)正确 |
| BRR-13 | Class Reset与Bus Reset/Disconnect交错 | 旧控制请求失效与旧资源收尾分开；按最新会话边界处理，不假定原配置仍有效 |

实板证据必须包括类请求、EP0状态完成、两个ClearFeature端点地址、恢复后的新CBW/CSW及内部状态。只见设备重新枚举、Clear HALT或下一次读成功，均不足以单独宣告BOT Reset Recovery完整通过。SD组禁止协议WRITE负向注入。

## 12. UVC活跃流专项

前置证据必须是真实Alt1、有效Commit、ISO EP1已打开，且有正在发送的帧/包及被持有的frame lease。只做UVC Alt0枚举不能算活跃流回归。保持原描述符、FS带宽及SOF节拍，不修改Camera/Media数据路径；A/B保护卡内容，UVC测试时断电取出真实SD。

VERIFIED：当前 `usb_uvc_class.c:512 UVC_DeInit`经Alt0关闭EP1并释放帧后才Free context；`UVC_SetStreamAlt:1003`在Close失败时保留方向标记，`UVC_ReleaseFrame:1105`处理Payload取消。新设计的Prepare需先满足这些同步释放前提，不能仅凭“USB空闲”推断Bridge/帧租约已归还。

| ID | 活跃流场景 | 必须保持的事实 |
| --- | --- | --- |
| UV-01 | 连续ISO发送中主动Stop | 先封锁SOF/新帧/续包，取消证明及租约排空后Class Release，再最终PCD Stop |
| UV-02 | Alt1正在发包时SET_INTERFACE Alt0 | 两阶段Stream Prepare/同步Commit，无假成功状态；Class仍存在，PCD不重启 |
| UV-03 | 帧构包/源数据访问中取消 | 已有CPU/source访问退出前不归还源buffer；不重用给下一帧 |
| UV-04 | 最后一个ISO包正常完成与取消竞争 | 只按真实整帧发送事实统计EOF；取消不虚构完成帧，也不重复Return |
| UV-05 | ISO incomplete与取消同时/先后出现 | 不把Incomplete当正常完成或Bulk取消确认；帧丢弃/归还各最多一次 |
| UV-06 | Close/Abort/Flush失败 | Alt/ep1_opened/close_pending反映真实部分状态；保留帧和context所需引用 |
| UV-07 | Class DeInit Commit意外失败 | 无Core假状态成功、无双Free，进入受控恢复；不为通过验收强清标记 |
| UV-08 | 活跃流Bus Reset或Disconnect | 旧帧取消收尾可达，旧SOF/IN/Incomplete不得推进新会话统计或访问新context |
| UV-09 | Bridge/上层帧归还确认被延迟 | 继续Drain，不先Free context或回收共享源buffer；延迟确认不因USB epoch变化被丢弃 |
| UV-10 | Alt0→Alt1、重新Commit后开始新流 | 旧事件不会改变新FID/包序列/lease，旧帧不会被当新流EOF |
| UV-11 | 重复Alt0、Stop、Disconnect及迟到回调 | 每帧归还一次、Class释放一次；不改变已完成事实 |
| UV-12 | 10次活跃流断开重连 | 每次保存控制/ISO捕获及状态/帧引用记录；不能以空闲枚举10次替代 |

ISO硬件发送完成与PC端应用成功解码是两种证据。需要真实PC视频接收/帧验证时单独记录，不能只凭MCU计数推导PC已收到完整帧。若摄像头初始化已有问题而不能启动Alt1，专项标阻塞/NOT_TESTED，不修改Camera或旧断言来制造通过。
