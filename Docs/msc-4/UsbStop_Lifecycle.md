# USB停止与会话恢复生命周期设计

日期：2026-11-19。审计基线：`30f4dd007b126bc2f90bdc7bb1818abeb65c3229`。本文件仅设计，不修改生产代码，不授予实现或MSC-5权限。VERIFIED为指定源码/既有证据，INFERRED为推断，UNRESOLVED为尚未解决，NOT_TESTED为尚未验证。

依赖：[EndpointCancel契约](EndpointCancel_Contract.md)、[硬件证据](EndpointCancel_HardwareEvidence.md)、[测试计划](EndpointCancel_TestPlan.md)。本文的状态机为目标行为，**不是对当前实现的描述**。

## 1. 当前实现的差距

VERIFIED：`CM7/Platform/usb_device.c:1729 Usb_StopDevice`先调用 `CM7/BSP/bsp_usb.c:188 BSP_USB_Stop`，底层195行先关闭NVIC，再Abort各方向，最后才HAL_PCD_Stop；Platform在BSP Stop成功后才释放Class。当前顺序不满足“先排空、Class Release、最后PCD Stop”的目标。

VERIFIED：`Usb_Disconnect:1543 -> USBD_LL_DevDisconnected:995 -> Usb_ManagedClassDeInit:1855 -> Msc_DeInit:84`可在IRQ链内发生。MSC先禁止配置请求和BOT推进，再分别关闭OUT2/IN2；失败方向保留资源。当前Bulk取消是同步轮询，没有独立取消完成事件。

VERIFIED：Platform `Usb_DataOut:1410 / Usb_DataIn:1430`在非零端点Class非ACTIVE时直接过滤业务路径；新的收尾不能只依靠这条路径，因为旧取消事件必须仍能完成旧Stop。现有ManagedClassDeInit失败保持RELEASE_FAILED及外部绑定，不表示资源已经释放。

上述事实决定了后续实现须有新的底层完成契约和Core调用适配；本轮不能仅新增状态枚举就宣称整改完成。

## 2. 事务、会话与共同准入点

每个Stop事务保留：`stop_id`、旧Session、受影响端点incarnation/Transfer、各Cancel身份、原因集合、首个错误、Class释放进度、PCD停止进度、IRQ提交状态、通知引用及后端排空状态。采用[ADR-01](EndpointCancel_Contract.md)推荐A：底层同步有界Abort，Task分阶段协调；逻辑cancel_complete由Task在取得证明后发布，不新增对不存在的HAL取消回调的等待。

单一Task串行推进；ISR只记录事实、关闭新提交准入并通知Task。StopRequested的线性化点必须覆盖：

- 禁止新CBW解释为业务命令；已arm的CBW buffer继续保留，等待取消或正常完成证据。
- 禁止新Data IN/OUT、CSW、BSP自动PIO续包和Worker Submit。封锁业务不等于删除已在途引用。
- 取消旧EP0 Deferred Status资格；Token失效不释放端点、Worker或DMA。
- 建立旧会话的收尾记录，才允许推进“当前会话已失效”的Session标记。不能先递增epoch后把旧事件全部丢弃。

重复Stop合并到现有事务并返回其身份/状态。原因集合可增加；不能启动第二个并行Class释放或PCD Stop。Stop记录不可因USB重枚举或调用超时而丢失。

## 3. 统一流程及明确的退出策略

统一的是事务、准入、端点静止、回调排空、Class释放和恢复判据。**是否最终关闭PCD是明确的策略，不由一次物理事件盲目决定。**

```mermaid
flowchart TD
    R[RUNNING] --> S[STOP_REQUESTED]
    S --> Q[QUIESCE_ENDPOINTS]
    Q --> D[DRAIN_CALLBACKS]
    D --> C[CLASS_RELEASE]
    C --> T{退出目标}
    T -->|主动停机或已批准的完整停止| P[PCD_STOP]
    P --> X[STOPPED]
    T -->|会话边界恢复| B[SESSION_REBUILD]
    B --> W[等待新会话配置]
    W --> R
    Q --> F[RECOVERY_REQUIRED]
    D --> F
    C --> F
    P --> F
    B --> F
```

| 原因 | ISR/入口动作 | Task共同收尾后的策略 |
| --- | --- | --- |
| 主动Stop | Task登记Stop，立即封锁新提交 | Class释放后最终PCD Stop，成功才STOPPED；后续Start必须显式请求 |
| USB Disconnect回调 | 失效控制资格，记录旧Session和断线事实，封锁提交 | 退役旧Class/端点；若需自动重连，保留明确的控制器监听策略，不无条件关PCD后假设会自动收到Connect |
| USB Reset | 记录Reset边界、失效旧控制请求并封锁旧业务 | 完成旧收尾后建立地址0/新EP0边界，等待主机配置；正常Reset不自动变成断开再强制Start |
| 致命错误/恢复失败 | 锁定准入，保存首个失败 | RECOVERY_REQUIRED，保留未证明静止的引用和RAW租约 |

VERIFIED：当前VBUS sensing关闭，SOF/Suspend/Disconnect是不同入口。设计不把SOF缺失或Suspend当作安全断开，也不假设PCD已关闭时仍有可靠的Connect唤醒来源。自动重连所需的实际检测/监听路径属于L-U2。

如果人工选择“Disconnect后完整关闭PCD”，它走PCD_STOP并需要独立、已验证的后续启动来源；缺少该来源时保持STOPPED，不能以轮询强制重启掩盖问题。

## 4. 状态的进入动作与完成证明

| 状态 | 允许动作 | 进入下一状态的必要条件 |
| --- | --- | --- |
| RUNNING | 仅当前Session可arm/提交业务 | 收到确定的Stop/会话边界，准入关闭 |
| STOP_REQUESTED | 固定旧目标、原因与Stop身份，失效旧EP0资格；保持收尾IRQ可用 | 所有提交生产者都已看到封锁，目标快照完整 |
| QUIESCE_ENDPOINTS | 分方向登记取消；维持有效事件入口；正常完成也可成为终结事实 | 每个目标取得匹配的Cancelled、Normal Complete或可靠Idle证明；超时不满足 |
| DRAIN_CALLBACKS | 消费旧数据/取消通知，只做计数与所有权收尾；等待后端实际排空 | 硬件和驱动无访问，旧事件引用/调用栈归还，SD Worker/IDMA独立静止 |
| CLASS_RELEASE | 在安全方向上Close及一次性释放；记录部分成功，失败方向不伪装释放 | Class与端点真实释放完成，无待使用旧context的事件 |
| PCD_STOP | 确认无端点/通知依赖IRQ后请求最终控制器停止 | 硬件与HAL结果一致、停止已确认，才提交started=false及最终IRQ禁用/清pending |
| STOPPED | 不接收新业务；保留可查询的终结及错误历史 | 显式Start且完整启动前置条件满足 |
| SESSION_REBUILD | 保持PCD监听策略，完成Reset/会话的Core和EP0同步 | 旧端点arm屏障已完成，新会话地址/EP0一致，允许主机重新配置 |
| RECOVERY_REQUIRED | 保存故障、继续服务有身份的旧收尾证据；不得重开业务 | 满足第8节恢复条件，允许继续原清理；不自动Start |

短时间屏蔽IRQ保护元数据与把IRQ长期关闭是不同操作。准入更新临界区必须有界并恢复原使能状态；不能在其中等待取消事件、SD操作、队列消费者或长硬件超时。最终PCD切换的原子化方案尚待L-U4核验，不能重复当前“先无条件关NVIC，再等待Abort”的路径。

## 5. ISR与Task职责

| ISR/BSP事实层 | Task/生命周期层 |
| --- | --- |
| 在HAL前捕获必要原始状态并绑定旧arm身份 | 创建/合并Stop，串行推进状态 |
| 记录Reset/Disconnect、XFRC、EPDISD等事实 | 发起取消、检查证据、处理恢复条件 |
| 立刻封锁新业务和PIO续包入口 | 排空数据/取消通知及消费者引用 |
| 把旧完成交给旧Stop；队列满保留记录/故障 | 等待Worker和SDMMC实际quiescent，不占用ISR |
| 有界、无文件操作、无长轮询、无Class Free | Class释放和最终PCD停止/重建 |

不允许在ISR中阻塞等待Task消费队列。Task等待取消完成时必须保留实际可产生该证据的IRQ/查询路径；如果硬件已停止但通知尚在队列，继续排队收尾，不把它误判成“取消未提交”。

## 6. 迟到事件、Session与资源账本

事件首先查找其Transfer/Cancel/Stop记录，而不是先比较全局当前epoch。路由规则：

1. 匹配旧Stop的取消通知：更新旧方向的证明、驱动/事件引用和排空计数。允许旧Stop推进，禁止新Session业务副作用。
2. 匹配旧Transfer的正常完成：记录真实完成前缀/整次完成；Stop准入已关闭时只归还引用，不发送下一笔CBW、Data或CSW。
3. 重复终结事件：识别相同事件身份并幂等消费，不再Free或增加完成次数。
4. 已安全退役后的旧事件：仅落在tombstone/异常记录，不得解引用已释放context，也不得被包装成新Session的当前Token。
5. 无法归属的原始IRQ：不向业务投递，封锁相关新arm并进入证据不足的恢复状态；不能靠epoch不等直接丢弃后释放旧buffer。

每次检查使用独立事实：`hardware_quiescent`、`driver_access_released`、`events_consumed`、`class_release_done`、`pcd_stop_done`。软件busy只是账本投影；不能先清busy再用其证明硬件静止。

缓冲引用计入硬件、BSP PIO分包、排队事件、正在执行的回调、Class和后端。对SD LUN，SDMMC IDMA及Worker可能仍持有同一MSC数据缓冲；必须等待其自己的完成证明。RAW租约在本生命周期里保持原所有者，任何Stop、Disconnect、Reset、取消或PCD停止结果都不自动授权FatFS。

## 7. Core同步DeInit与Task协调：Prepare / 同步Commit

拟议完整停止的顺序为：取消证明 → 旧通知/后端排空 → Class/端点释放 → PCD Stop → 提交STOPPED。Class Release前必须有零旧context消费者；不能为了通过Core DeInit而强制清端点标记、释放内存或返回假成功。

Class部分释放失败时记录每个方向/步骤。下一次只能重试尚未完成且明确可重试的步骤；外部Class DeInit是否可重复调用必须逐Class核实。不能假设UVC等所有Class与MSC静态资源同样幂等。

PCD Stop失败时可能出现“Class已经真实释放、PCD尚未停止”。必须分别报告两个事实，不重新伪造Class仍ACTIVE，也不声称PCD已停止。不得把这些状态压成一个成功bool。取消/回调尚未排空时禁止进入PCD_STOP；失败保留真实控制器状态和错误，后续只能继续安全收尾或显式恢复。

### 7.1 设计决定：延后Core入口，不延长DeInit回调

VERIFIED：Core `USBD_LL_Reset:771`、`USBD_LL_DevDisconnected:995`、`USBD_ClrClassConfig:505`都按同步方式调用DeInit，没有Pending续接接口。`usbd_ctlreq.c`的Configured→SET_CONFIGURATION(0)路径忽略ClrClassConfig失败并尝试发送状态包。故不能让DeInit返回BUSY后期望Core自行等待，更不能提前返回OK。

**设计采用Task两阶段协调，Core的DeInit保持同步完成语义。** 等待发生在进入Core之前，硬件Abort及后端等待不发生在Core回调中：

1. Platform生命周期入口创建事务并封锁提交，保留旧Core/Class对象；会触发DeInit的Core入口尚不调用。
2. Task Prepare：分别完成Class数据端点OUT/IN同步Abort、旧通知排空、Worker/IDMA或UVC帧租约归还，以及数据端点实际Close。还须退役可能引用旧Class对象的EP0回复/数据；EP0本身由独立控制门控管理，不能为清Class数据端点而无条件把当前Setup/控制端点一起关闭。SYS_ERR_BUSY按依赖原因等待；HAL_ERROR进入受控失败，不假装Pending。Prepare不得调用Class Free或宣告DeInit成功。
3. 达到所有Ready谓词后生成一次性`PreparedTicket`，绑定stop/session/Class实例及内存generation、端点Close证明、零外部消费者引用和提交封锁generation。协调者自己的保留引用在同步Commit中原子消费，不在Class释放后继续解引用旧对象。若是控制请求，还绑定本次Setup cookie。
4. Task在有界Core串行化窗口执行相应Core入口。ManagedDeInit验证Ticket，实际调用Class同步DeInit/Free并记录完成；已经Close的端点不再开始硬件取消，已经归还的帧不再重复Return。回调不得等待硬件、Task或回调队列。
5. Core返回后分别检查DeInit提交结果、端点/EP0实际结果及Control/Reset generation；仅检查Core返回OK不足以提交成功。Ticket单次消费；后续重复DeInit只能返回已完成事实，不再次Free。

PreparedTicket不是“准备请求已登记”，而是所有前置证明已完成。没有有效Ticket的破坏性Core调用属于准入错误：拒绝/锁定，不在该回调里临时启动长清理。

所有会触发DeInit的入口都需经过此协调，包括直接USBD_Stop/DeInit/UnRegisterClass/ClrClassConfig调用，不能只拦Reset而留下旁路。已证明没有Class实例、端点和旧引用的空清理可用显式No-Class Ready证明；首次Init失败的部分清理须独立验证，不能借用旧Session Ticket或因为没有正常枚举就假定没有资源。

### 7.2 入口协调矩阵

| 入口 | Prepare之前 | Ready后的同步Commit | 后继 |
| --- | --- | --- | --- |
| 主动Stop | Task封锁新提交，固定旧目标 | 受控Class清配置/DeInit入口；不先调用会停止PCD的完整USBD_DeInit | Class Release成功后进入最终PCD_STOP |
| Disconnect回调 | ISR只登记断线及旧Session；暂不调用USBD_LL_DevDisconnected | Task调用原Core DevDisconnected，Ticket使DeInit同步完成 | 按既定监听/停止策略，不强制Start |
| Reset回调 | ISR记录新Reset generation，失效旧EP0资格；保留旧资源账本，不立即让Core DeInit进入等待 | Task调用原Core Reset；核验EP0两方向Open，按已确认Reset边界同步软件/硬件地址0并检查结果 | 新会话等待主机配置，不强制PCD重启 |
| SET_CONFIGURATION(0)或合法更换配置 | Platform在USBD_LL_SetupStage之前拦住本条破坏性标准请求，保留8字节副本/cookie；不让Core先改dev_config并调用DeInit | Prepare成功且cookie仍当前时，Task向原Core分发这条Setup一次 | 真实DeInit成功才允许当前请求的状态阶段 |
| UVC VS SET_INTERFACE Alt0 | 只Prepare视频端点与旧帧归还，禁止新SOF触发提交；不是全设备Stop | Task向原Core/Class分发该条Setup；Alt0真实关闭后提交状态 | Class仍保留，PCD不停止；后续Alt1单独准入 |

当前只有一个配置；合法重复SET_CONFIGURATION(1)不应平白触发DeInit。清理未完成时的新配置请求不能仅凭旧Core仍显示Configured就走“重复配置成功”分支；应受生命周期准入门控。

### 7.3 EP0生命周期门控与Setup替换

门控是拟议Platform/BSP边界契约，**当前尚未实现/验证，不使用固定延迟伪装等待**：

- Prepare期间不把新的SET_ADDRESS/SET_CONFIGURATION交给仍带旧配置的Core；旧EP0数据/状态也必须先取得自己的停止证明，不能泄漏给地址0的新请求。
- 控制器保持能记录新的Setup及Reset的路径，IN响应维持受控未就绪/NAK状态；不得发送假状态成功、任意旧描述符片段或把Suspend当Disconnect。EP0门控需要硬件证明，不能仅设一个软件bool就声称已实现NAK。
- 8字节Setup副本拥有独立cookie和Reset generation，不引用会被HAL覆盖的Setup缓冲。尚未分发的旧请求可被更新Setup取代，但其资格作废不释放已在途EP0/其他资源。
- 新Setup、Reset、Disconnect或主动Stop撤销旧cookie的响应资格；资源清理继续服务原Stop。不能因为原SET_CONFIGURATION(0)被主机放弃就自动恢复已部分关闭的旧流。
- 只重放当前、合法且未经分发的请求一次；绝不向当前新请求补交旧状态包。Reset再次到来时合并清理，按最新Reset边界重建，旧Transfer收尾身份仍保留。
- 主机等待上限与Task调度/Abort最坏预算必须验收；超过窗口时如实进入恢复失败，不能无期限保留请求或宣称Windows必会重试成功。

VERIFIED：现有 `USB_Device_DeferControlStatus:1025`仅接受bmRequest=0x21、Configured、wLength=0的匹配Class请求；**不能直接拿它给标准SET_CONFIGURATION(0)或UVC标准SET_INTERFACE登记**。新标准请求门控是在调用Core前的排队/准入，不全局放宽Class BUSY规则，也不把Core标准请求返回BUSY当作已支持延迟。

### 7.4 同步Commit异常与Core忽略返回值

Prepare必须把可能等待/失败的取消与后端收尾提前完成，Commit只做已准备好的方向标记提交和Class释放。UVC当前DeInit会调用Alt0释放帧，因此其Ticket必须先证明EP1不持有PIO包、Payload/Bridge旧租约可归还且零外部context消费者；不能假设USB端点空闲就等于帧归还完成。

如果Commit仍失败：

1. 保存实际部分释放状态与首个错误，禁止第二次Free，进入RECOVERY_REQUIRED。
2. 对Setup入口，失败的cookie在Platform状态包提交路径被否决；即使Core因忽略ClrClassConfig返回值而继续CtlSendStatus，也不得实际发出成功状态包。需要定向STALL/失败处理，不能仅看Core函数返回值。
3. 该否决只作用于绑定的旧cookie；新的Setup不能继承它，也不能让新Setup“复活”旧回复。
4. 对Reset/Disconnect入口，Core已经更改Default/config等逻辑字段也不代表Class已释放。没有Release证据就不开放新Class Init/新CBW；保留旧引用而非回滚成一个假的ACTIVE对象。

Core串行化窗口只在Ready且无需等待收尾后进入；可使用短临界区保护一次有界提交，不能持有IRQ屏蔽等待后端。最终HAL/PCD Stop另在Task阶段执行，其串行化和IRQ提交仍需L-U4核验。

Reset还有一个身份细节：Core Reset先把dev_config清零再调用DeInit，所以回调收到config=0并不代表没有旧Class。PreparedTicket绑定实际旧Class实例/内存generation及旧Session，不能把配置参数当作Class身份或凭config=0跳过释放。

该方案在设计上解决同步Core回调与Task等待的职责冲突：**Core只看到一次已准备好的同步释放，Task承担所有等待；门控和失败否决位于Platform，不改ST Core。** EP0硬件门控与调度预算是实现验证项，不在本轮冒充已验证事实。

## 8. RECOVERY_REQUIRED恢复条件

超时只记录“按期未证明”，不改变硬件/缓冲所有权。状态需要继续接收旧收尾事件；延迟取消完成必须能帮助原Stop收尾，不能因当前Session变化被过滤。

继续清理必须同时满足：

- 原Stop/Transfer/Cancel身份可追溯，首个错误与后续证据都保留；没有Token复用或事件证据溢出。
- 各方向取得实际静止证明，驱动续传已封锁；新arm与旧原始事件已分离。
- 已排队/在栈的旧回调、取消通知和后端引用已经排空，或有确定且有界的可继续排空路径。
- SD Worker/IDMA独立确认，不通过USB事件代替；RAW租约保持。
- 对失败步骤重试不会重复Close/Free已完成资源；对PCD/IRQ状态有明确账本，不凭READY/UNLOCKED猜测。

迟到证明可以让旧清理继续至STOPPED或安全的会话重建门槛，但不能擦掉之前Failed结果、自动开放新Session或把历史失败报告改成全程成功。是否从RECOVERY_REQUIRED继续清理及之后是否允许Start是两个决定；证据不足时一直封锁业务。

控制器必须复位才能恢复、需要改未授权模块、或者共享RX归属不明时，保存现场并停止后续实现/验收；不能把自动MCU复位、强制PCD重启或开启FatFS当作恢复条件。

## 9. 实现前未解决项

| ID | 状态 | 待核验内容 |
| --- | --- | --- |
| L-U1 | NOT_TESTED | 两阶段Prepare/同步Commit方案已确定；需执行Core协调测试，证明不存在假DeInit成功、双Free或旧回复 |
| L-U2 | UNRESOLVED | VBUS sensing关闭时Disconnect后的可靠监听/重连来源，不能依赖SOF缺失或关闭PCD后的假Connect |
| L-U3 | UNRESOLVED | 各Class部分释放重试的幂等性及零消费者证明，特别是默认UVC |
| L-U4 | UNRESOLVED | 最终HAL/PCD停止的有界串行化、临时IRQ保护与失败恢复，使其不妨碍必要收尾 |
| L-U5 | NOT_TESTED | 所有故障注入、RAM/SD只读/UVC实板矩阵和10次重连 |
| L-U6 | UNRESOLVED | EP0门控的真实NAK/旧传输隔离机制，以及Task Prepare/重放满足主机等待窗口的实测预算 |

本轮交付的是可审查的事务与条件设计。所有未解决项保留为实现门槛；没有编译、烧录或运行新的状态机。
