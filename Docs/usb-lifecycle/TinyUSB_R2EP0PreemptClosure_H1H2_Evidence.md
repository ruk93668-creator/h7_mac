# R2-EP0PreemptClosure — H1/H2硬件证据专项

**审计结论：H1、H2继续UNRESOLVED / BLOCKED。生产实施仍未获批准。** 本轮只新增本报告，读取并渲染原PDF，不修改生产/测试代码、不构建固件、不烧录、不合并main。文档基线`edb633d423363b66f79c935e2bdd10e3f6608bbb`，生产基线仍为`cc0ad3571f0bdaa5ac29b9c18849eb03ef9360a9`。

## 1. 核验范围与证据完整性

VERIFIED：本机RM0399 Rev5、3535页，源SHA256 `61d07b67deb291aed8a5cf8019c94add0a1065318542e57b3287d2694b14bd60`。本轮从原PDF直接提取pp2745–2931共187页，检索EP0/control IN/DIEPCTL0/EPDIS；保存98条命中记录。未将“未搜到专用条文”当作整个文档不存在相关规则的证明。

VERIFIED：实际查看了p2840、2843、2907、2918、2920的整页图像，确认标题、正文、表图和页码；p2840的矛盾措辞不是文本抽取串列造成。其余相关页正文核对包括p2784、2906、2908、2917、2919。原PDF未修改、未跟踪。渲染/提取记录在`output/TinyUSBReplacement/EP0-H1H2-audit/`，含`evidence-receipt.json`、187页正文和8张渲染图。输出仅作本地证据，不作为生产代码或实板结果。

INFERRED：ST官网“V5.0”的版本标记本身不解决某个寄存器序列的含义。本轮硬件判断以具有上述SHA的正文为准，没有把其他DWC2平台的实现或模型响应当作H747保证。

## 2. H1：EP0 IN停止序列

| 已核对内容 | RM依据 | 判定 |
| -- | -- | -- |
| SETUP使control IN/OUT内部NAK置位 | 60.15.6 p2907 Internal data flow第1项 | VERIFIED；只能得出NAK被设置，不能据此释放source |
| SETUP_DONE pop产生STUP | 同页第5项 | VERIFIED；RX FIFO仍需唯一消费者，ISR中不能等待旧IN结束阻塞SETUP消费 |
| SETUP后自动清使能的明确对象 | 同页第6项control OUT endpoints | VERIFIED；不能扩大为IN0自动禁用、FIFO自动清空 |
| DIEPCTLx地址包含x=0..8 | 60.14.55 p2840 | VERIFIED；同页正文却排除endpoint0，语义适用范围未闭合 |
| EPDIS应仅在EPENA=1时请求，等EPDISD才可视为禁用 | p2840；60.14.56 p2843 | VERIFIED（该通用描述）；不能绕过EP0适用性缺口 |
| INEPNE表示core采样NAK，可能来自应用或core；不保证线上已发NAK | p2843 | VERIFIED：属于状态证明，不是带transfer ID的取消完成事件 |
| INEPNE通过CNAK解除NAK时清除 | p2843、60.15.6 p2917 | VERIFIED：不能假设向DIEPINT位6写1就能建立新的事件边界 |
| SNAK到INEPNE之间仍可能发送旧IN数据 | p2917 | VERIFIED：软件fence不等于已经终止线上旧包 |
| 普通IN disable流程与Reset型transfer stop分列 | pp2917–2918 | VERIFIED；不能把Reset的EPDIS直达步骤用于普通SETUP抢占 |

**H1 BLOCKED**：现有证据不能无条件确认control IN0在新SETUP下应如何请求禁用、何时必然产生EPDISD，以及NAK状态已由SETUP建立时应走哪条分支。需要ST对RM0399上述EP0适用性及特殊分支的明确条文/澄清，或已映射到H747实际IP版本的权威资料。CMSIS位宏、HAL代码、普通Bulk成功、其他芯片示例均不足以独立关闭H1。

INFERRED（必要修正）：不能为了“清旧INEPNE”先CNAK再SNAK，这可能暂时重新允许旧IN发送。也不能要求一个在NAK持续有效时未必重新产生的“新边沿”。将来必须依据已确认的EP0规则，证明当前NAK状态连续有效、没有CNAK/重新arm，并与旧owner和硬件快照组合；目前不实施该候选步骤。

## 3. H2：EPENA=0原因与FIFO退休

VERIFIED：60.14.56 p2843把XFRC定义为编程传输在AHB及USB上完成，把EPDISD定义为应用禁用请求对应的完成。60.15.6 p2920说明transfer size及packet count为0时产生XFRC并清EPENA。p2919的ZLP仍需packet count=1，且使用内部ZLP标记；不能将size=0或FIFO字数为0当作ZLP已完成。

| EPENA=0的证据来源 | 可以得出的事实 | 仍须满足的条件 | 当前结论 |
| -- | -- | -- | -- |
| 匹配旧编程transfer的XFRC | 该编程transfer完成 | 禁止后续EP0包及旧Status、PIO refs归零、FIFO/状态证明、旧队列事件退休 | 正常完成候选，INFERRED；非完整EP0事务完成 |
| 已记录旧owner的EPDIS请求，匹配EPDISD | 已请求的禁用完成 | H1适用性、PIO refs归零、真实目标TX Flush及事件退休 | 主动停止候选，H1未闭合所以BLOCKED |
| 只有EPENA=0，无可归属终态 | 只有使能位观测 | 原因和FIFO读侧状态不明 | BLOCKED，保留旧source/身份，不猜测完成 |
| XFRC/EPDISD同轮 | 有多个硬件事实 | 请求历史、W1C前快照、单一终态，不能双释放 | H3需条件化裁决，未实施 |

VERIFIED：p2784要求TX Flush前确认FIFO无读写，写侧用AHBIDL，读侧需NAK有效等明确依据；TXFNUM在Flush中不得改，必须等真实TXFFLSH清零。p2918的Flush顺序本身不证明EP0的所有前置条件已成立。TXFE可能表示半空，DTXFSTS空闲字数也不证明线上包或ZLP内部状态结束。

**H2 BLOCKED**：已明确EPENA=0不能独立作为退休条件；带XFRC及带EPDISD的条件分支可建模，但EP0特有的FIFO读侧静止/禁用适用性未取得充分证据。不能输出一条声称已可实施的最终寄存器序列。

## 4. H3：事件身份及当前软件缺口（条件设计）

VERIFIED（源码）：`ThirdParty/tinyusb/src/device/usbd.c:1731`推进原生SETUP序号，但没有同时撤销EP0 completion generation；Core XFER_COMPLETE仍按endpoint_generation过滤，`ThirdParty/tinyusb/src/device/usbd.c:1122`的旧Status回调本身不直接比对SETUP序号。因此“推进SETUP序号”不能单独当作旧Status/XFRC已经隔离的证明。当前受控fault门禁不能被移除，直到未来有专用EP0 fence覆盖这些入口。

INFERRED（后续必要契约）：固定旧transfer generation、旧source及已提交包范围；新SETUP只更新最新请求的完整身份。旧IRQ事实可帮助旧资源收尾，但不能进入新Core事务或续写下一包。W1C前保存状态和请求历史；硬件中断位不携带软件generation，必须通过禁止重arm、原始提交边界和独占事件所有权建立归属。模糊归属保持fault。

INFERRED：正常XFRC先赢可终止旧编程包，但不得触发旧EP0多包续传；取消先赢则按已批准硬件序列收尾。收到不曾请求的EPDISD、或丢失历史的残留位，都不能自动绑定当前owner。

## 5. 连续SETUP的RX/DONE配对表

VERIFIED（RM）：pp2906–2908要求读取两个SETUP数据word、消费setup-stage-done word、维护STUPCNT并处理最后收到的SETUP。Slave模式还存在RX FIFO满时丢包条件，不能用软件布尔标记重建硬件从未交付的数据。DMA的B2BSTUP/DOEPDMA算法不适用于本次PIO。

INFERRED（必须明示的前提）：只在同一Reset/session、唯一RX消费者、完整8字节RX且原生序号未耗尽的范围讨论配对。DONE不携带软件序号；以下依赖已确认的硬件FIFO状态顺序，不是对任意重排的承诺。

| FIFO观测顺序 | 条件处理 | 禁止的假设 |
| -- | -- | -- |
| RX(A)→DONE | 完整A+seq/tag+观测的DONE形成候选；尚需旧IN退休 | DONE立刻允许释放旧IN |
| RX(A)→RX(B)→DONE | 按手册最新SETUP语义，B成为候选；保存A/B接收序号及DONE边界、STUPCNT快照供核验 | 仅两个bool就已经证明所有交错 |
| RX(A)→DONE→RX(B) | B撤销A协议资格，B的完成状态重置；A已入队时也必须变为不可消费 | 把A的DONE带到B |
| RX(A)→DONE→RX(B)→DONE | 仅最新完整B有交接资格，旧硬件停止owner不变 | 连续SETUP重置旧stop deadline/旧owner |
| DONE没有本session RX、RX不足8字节、Reset跨越RX/DONE | 无候选，保留/锁存异常；不得运行旧缓存请求 | 根据最新缓存猜测DONE归属 |
| RX(A)→RX(B)→DONE→DONE | 第二个DONE没有新RX时不能再次发布B；需核实硬件可产生的状态/quirk | 第二DONE代表新的事务 |

UNRESOLVED：STUPCNT的读取时点、硬件收到B但CPU尚未pop其RX时的可观察顺序、合成RX_COMPLETE与DONE交错，必须在H1/H2明确后形成精确可允许序列，并用实际DCD FIFO唯一pop测试。不能宣称任意RX/DONE排列都可正确匹配。现有`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1540`中两个bool不足以构成这种一般证明；这里不修改它们。

## 6. H4/H5：确切交接顺序（仍是待审批契约）

INFERRED：目标TX0 Flush必须与其他IN PIO写入、已有Bulk取消、Reset和关闭的GRSTCTL命令统一串行化；只观察TXFFLSH清零不证明它属于本次操作。不能清共享RX FIFO解决EP0 IN抢占，不能影响OUT0接收SETUP。

以下候选以“H1/H2 proof已完整”为**前提**，不构成生产实现批准：

1. ISR在完整RX时撤销旧控制资格，发布EP0-specific fence；保留固定旧owner及source。最新请求保存8字节、32bit序号、tag、session及RX/DONE关联信息。
2. 原UsbTask分阶段取得硬件/FIFO/PIO/旧事件全部proof。此时仍保留source、pending和新Core提交门控；硬件准备与引用退休仍分开。
3. 进入短USB IRQ排斥窗口（当前FreeRTOS BASEPRI策略），复核旧身份、最新完整请求、生命周期raw状态、proof和共享命令所有权。
4. 在这个窗口内向**原队列**执行零等待try-send；不运行event hook或Class回调。失败立即返回，保持owner、source、pending和fence；使用R1既有有界队列策略，不能无限等待同Task消费。
5. 只有入队成功，且仍在同一窗口内，才提交旧DCD引用退休及新请求可消费资格。清除必须仅作用于已证明的旧owner，不能覆盖更新的session/fence。
6. 退出窗口后才通知event hook，再由原UsbTask消费队列。hook或解锁后新的SETUP若抢占，先推进序号撤销已入队旧请求；旧资源已合法退休，新请求重新取得资格，不能回滚序号。

INFERRED（为何消费者不能提前运行）：唯一消费者就是执行步骤3–5的UsbTask；步骤4没有回调/重入调用；零等待内核send不会等待或启动第二个消费者；USB相关ISR被短锁串行化。还须保证更高优先级ISR不操作这些USB资源、trace hook不重入USBD、其他Task不消费该私有队列。缺任何假设即无此证明。

VERIFIED（现有API差距）：dcd_event_setup_try在内部锁退出后调用event hook。未来不能把现有完整函数直接置于上述外层短锁中并声称“hook在锁外”；需要最小的无通知try-publish/提交/锁外notify安排，或等价的原生事件可消费门控，并复核pending查询语义。只加一个ep0_pending标志并先清它去通过入队检查，不符合契约。

| 可抢占点 | 应有结果（INFERRED） |
| -- | -- |
| proof之前 | 保留旧source；旧完成仅记录，不能向Core提交新请求 |
| proof之后、短锁之前 | 新SETUP替换目标；Reset/Disconnect使旧提交失效；重新检查 |
| 入队失败 | 无引用退休、无Class reset/旧Status，无假SUCCESS |
| 入队成功到提交之间 | 受同一短锁及唯一消费者约束，无USBD消费窗口；硬件raw生命周期事件仍须复核 |
| 解锁后hook期间 | 新序号使旧消息过期；不复活旧owner、不双重释放 |
| 入队已成功但提交发现raw Reset/Disconnect | 不回滚身份或假装退休；消息保持不可消费，保留旧资源并走已证明协调或受控fault |

INFERRED：BASEPRI不会冻结USB硬件。raw事件可在最后一次读取后到来；旧资源可退休必须独立建立在此前已完成的静止proof上，而新消息的消费还须由IRQ代际撤销及Core入口的序号/raw资格检查保护。不能只凭软件短锁宣称硬件事件不存在。

UNRESOLVED：完整实现还须明确消息可消费门控字段与当前Core检查、raw事件最终复核点、FIFO0操作所有者及多包Status路径。H1/H2未闭合，H3–H5因此仍为条件化设计，而非“最终可实施序列”。

## 7. 继续关闭门禁所需的最小证据

- H1：ST明确control IN0的EPDIS适用性、SETUP自动NAK与INEPNE关系，以及EPENA已清时允许的停止操作。
- H2：上述不同终态下目标TX0可Flush/已空的充分前置条件；ZLP内部标记/包计数及多包续传边界不能省略。
- H6：H747实例的实际GSNPSID和ES0445 Rev7 2.24.5适用性；本轮不启动调试会话、不读取板卡。
- 后续软件验证：真实完整AXF，补正常/异常状态表、连续RX/DONE、事件迟到/双终态、队列满及步骤3–6每个抢占点，保留原断言与FAIL；不能用模型自动清EPENA/FIFO填补证据。

**停止结论**：H1/H2证据不足，保持BLOCKED，不实施任何候选寄存器操作、不烧录。仅交付本轮审计文档和本地原文/图像证据。未运行软件测试或新固件构建，旧R1软件成绩不重标为硬件通过。

VERIFIED：本轮本地证据receipt SHA256 `73248bb958ade7e3722490c4052da4194f49388a8a9a8dc897acd2189f8f3a37`，记录正文/图像哈希及实际视觉复核页。
