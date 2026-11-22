# R2-EP0PreemptClosure — 设计审计与实施前门禁

**VERIFIED（范围）**：设计基线 `cc0ad3571f0bdaa5ac29b9c18849eb03ef9360a9`；新文档分支 `codex/usb-tinyusb-ep0-preempt-design`。本轮只新增本文件，不修改生产/测试代码，不构建新固件、不烧录、不合并。上一轮部分通过结论保持；不宣称R2-ResetClosure完整结束。

**结论：INFERRED（候选方案）+ UNRESOLVED/BLOCKED（硬件分支）**。软件事务资格与硬件资源退休必须分离。原DCD/USBD/UsbTask足以承载专用异步进度，但当前复核证据尚不能无条件批准对活动EP0 IN套用普通Bulk IN停止序列。下面是审查方案，不是已批准实现。

## 1. 当前真实调用链

VERIFIED：OTG IRQ → handle_rxflvl_irq 的唯一GRXSTSP pop → SETUP_RX读取8字节，调用dcd_event_setup_fence推进原生32bit序号、禁止旧OUT写入 → SETUP_DONE读取并W1C SETUP标志 → 检查IN0.EPENA → edpt_disable(0x80,false,true)。位置：`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1540`、`ThirdParty/tinyusb/src/device/usbd.c:1731`、`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:614`。

VERIFIED：活动IN分支写SNAK，然后dwc2_wait_bits等待INEPNE/EPDISD，最后同步FIFO Flush。`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dwc2_common.c:23`先做一次寄存器匹配；若不匹配且在ISR中，就锁存wait_failed并返回false。**不是每次都必然失败**：立即匹配的条件仍可能通过；不能把这些偶然立即完成视为可靠异步关闭。当前门禁发生在等待证据不足时，旧source没有因此合法退休。

VERIFIED：成功路径随后走dcd_event_setup_try原生非阻塞队列；Task检查原32bit序号和DCD tag，再清EP0 Core状态并运行请求。相关：`ThirdParty/tinyusb/src/device/usbd.c:1742`、`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1474`、`ThirdParty/tinyusb/src/device/usbd.c:780`。因此必须在新SETUP被发布给Core前完成旧硬件证明，否则Core可能清旧BUSY/复用控制缓冲区。

VERIFIED：IN源既有初次同步FIFO写入，也有TXFE/PIO续包及EP0多包续传。至少同时门控`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1498`、`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1692`、`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1838`、edpt_schedule_packets、dcd_edpt_xfer及Core EP0提交；只关TXFE不能阻止XFRC触发下一包，也不能撤回已进FIFO的数据。

## 2. RM0399核验与不能混用的规则

VERIFIED（资料）：本机RM0399 Rev5，3535页，SHA256 `61d07b67deb291aed8a5cf8019c94add0a1065318542e57b3287d2694b14bd60`；引用依据为已保留的逐页正文`output/MSC24/udlm-rm0399/pages2747-2924.txt`。页码为印刷页。本轮复核指定USB正文，不声称全3535页无遗漏。USB2 OTG_FS与OTG_HS2的实例映射沿用已确认的pp2746–2747；范围限FS、Slave PIO。

| 事实/问题 | 手册依据 | 结论 |
| -- | -- | -- |
| SETUP接收不依赖OUT NAK/EPENA | 60.15.6 p2906，STUPCNT=3、保留FIFO空间、PIO读取两个数据word | VERIFIED；不能关闭整个USB IRQ/RX消费来保护旧IN |
| SETUP使控制IN/OUT内部NAK置位 | 60.15.6 p2907 Internal data flow第1项 | VERIFIED；NAK状态不是旧TX FIFO为空，也不是Buffer可释放 |
| SETUP_DONE由RX状态pop生成STUP | p2907第5项；p2908最新SETUP处理 | VERIFIED；必须保留唯一破坏性pop/W1C所有者，允许最新请求覆盖旧待交接请求 |
| 自动清EPENA的明确对象 | p2907第6项写control OUT endpoints | VERIFIED；不能把这句话扩大成“SETUP自动disable IN0并清空旧TX” |
| INEPNE的含义 | 60.14.56 p2843；60.15.6 p2917 | VERIFIED：core已采样NAK；不表示线上已发NAK。SNAK到INEPNE间仍可能发旧数据 |
| 普通IN禁用 | pp2917–2918：停AHB写、SNAK、INEPNE、EPDIS/SNAK、EPDISD、TX Flush | VERIFIED（通用正文）；EP0抢占适用性尚需独立闭合，不能直接调用Bulk abort |
| 寄存器措辞矛盾 | 60.14.55 p2840范围x=0..8，却称other than endpoint0；EPDIS只在EPENA已置位时设置 | VERIFIED（文字）；UNRESOLVED：EP0 IN专用位语义/停止适用边界需明确依据，不能仅据CMSIS宏断言 |
| EPDISD和XFRC不同 | 60.14.56 p2843 | VERIFIED：前者对应应用禁用请求，后者编程传输完成；不能互相伪装或双重释放 |
| Reset型IN停止 | p2918独立Transfer Stop段：EPDIS→EPDISD→TX Flush | VERIFIED：不能直接用于普通SETUP抢占；Reset有不同总线前置条件 |
| TX Flush | 60.14.5 p2784：无读写、AHBIDL、真实TXFFLSH清零、TXFNUM保持 | VERIFIED：仅目标TX FIFO候选；不能Flush共享RX或用HAL_OK代替证据 |
| OUT0例外 | 60.14.61 pp2846–2847禁止EPDIS，SETUP仍可接收 | VERIFIED：只针对OUT0，既不能用来禁止所有IN0操作，也不能反向推出IN0可照搬Bulk |

UNRESOLVED：上述范围内没有找到足以消除p2840措辞差异、并完整定义SETUP抢占时IN0自动状态转换/所有停止分支的专用条文。这不是声称整本手册不存在依据。需要补充明确的ST官方EP0条文/澄清，或可核验的该IP版本资料，并与H747实例对应；其他系列示例、寄存器宏、HAL实现或模型自动清EPENA都不能单独关闭此门禁。

## 3. 待审批的最小异步方案（不实施）

INFERRED：只在DCD内增加一个EP0 IN专用进度，复用`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1051`、原IRQ及R1发布接口。它记录旧transfer generation、旧source/剩余长度/PIO引用、最新SETUP序号与tag、已请求/已观察的事件、阶段期限和首次故障；不引入通用Manager、参与者注册表、资源Ledger、StopAttempt框架、新Task或第二个控制器拥有者。

```text
SETUP_RX
  -> 立即撤销旧控制资格，保存最新SETUP，保留旧source
  -> IN0 PIO/续包/新提交fence
SETUP_DONE + 旧硬件快照
  -> [硬件规则已确认才允许]
     NAK有效证明 / 或已确认的正常完成分支
     -> 必要的IN0禁用请求及匹配完成证明
     -> 目标TX FIFO真实Flush完成、旧IRQ退休、PIO引用归零
  -> 原队列try发布最新SETUP
     false：保持pending、source和资格；同Task有限推进或故障
     true：原子复核身份与raw Reset/Disconnect，提交旧引用退休
  -> Core只处理最新SETUP，恢复正常控制传输与MSC
```

INFERRED：fence应在SETUP_RX已确认时尽早发布，而不是等SETUP_DONE后再停止PIO。旧在途硬件包不能靠软件撤销；fence只禁止继续从旧source填FIFO、提交下一包及旧Status/完成通知。IRQ正在执行的短copy先合法归还引用，禁止以mask位代替引用归零。

### 3.1 旧transfer身份与最新请求分离

- INFERRED：旧停止身份从fence起固定，后续SETUP只替换“最新待交接请求”，不能重置停止deadline或抹掉旧硬件事实。
- INFERRED：原生32bit序号在每个合法SETUP_RX推进，8bit tag不能独立决定资格；耗尽保持故障。旧XFRC可收尾旧硬件，但不能完成新控制事务或发送旧Status。
- INFERRED：保存最新SETUP必须覆盖被抢占的IN数据响应和IN Status ZLP；不能复用仍由旧IN持有的_ctrl_epbuf作为新接收目标。

### 3.2 各硬件分支必须独立获证

| 入口快照 | 候选处理 | 实施前门禁 |
| -- | -- | -- |
| EPENA=1且仍有旧数据 | 若EP0规则确认，按专用NAK/disable步骤异步等待 | INEPNE能否可靠覆盖control0、EPDISD产生规则必须核实 |
| XFRC先发生 | 记录旧generation正常完成，停止后续EP0多包续传，仍核验FIFO/引用/旧状态 | XFRC不等于整个控制事务完成，也不保证可立即复用控制缓冲区 |
| EPENA=0但无可归属终态 | 保留旧source，不能等待一个从未请求的EPDISD或直接宣告成功 | 自动清零原因、FIFO读侧静止证明UNRESOLVED |
| INEPNE与EPDISD/XFRC同轮 | IRQ在W1C前保存事实，单一终态裁决 | IN0事件来源、请求身份、已请求disable后XFRC先赢时的收尾依据需核实 |
| 新SETUP连续到达 | FIFO唯一消费者持续工作，保留最新完整RX/DONE对 | 不重启旧取消、不清共享RX、不把旧DONE匹配到新RX |

INFERRED：候选目标是停止旧0x80事务，而非关闭整个EP0接口；OUT0、其他OUT的RX和其他IN仍须服务。不能照抄Bulk abort目前拒绝EP0的接口，也不能采用Reset专用全组RX Flush、全TX Flush或controller reset。

## 4. 并发、共享命令与发布契约

INFERRED：短BASEPRI窗口只保护fence/快照/命令提交/最终裁决；不跨Task轮次等待INEPNE、EPDISD、Flush或队列。IRQ只记录当前停止身份的事实，不长轮询。共享DIEPMSK位、GRSTCTL命令和TX producer必须与现有Bulk取消/Reset/组关闭协调；不得误清其他IN事件，不能把外部Flush的清零当作本次完成。

INFERRED：复用原SETUP result-bearing队列接口，但需要复核它当前拒绝pending端点的条件。不能为了发布新SETUP而先撤除旧PIO fence。最终发布必须满足：硬件proof完整、旧引用未释放、仅原Task消费队列、try-send结果真实、消息不能在本次提交前被Core处理；成功后再次原子复核最新序号/旧身份/raw生命周期事件，失败保留。R1已有event hook在锁外，仍要覆盖hook期间新SETUP/Reset的抢占。

INFERRED：建议保留“硬件旧引用退休”与“新请求Core可消费”的明确提交顺序。若消息已入队后资格再失效，必须确保原Core序号/代际检查拒绝它，不能简单回滚序号。入队成功不是主机ACK，更不是旧Status完成。

INFERRED：Bus Reset/Disconnect/Group Close与EP0停止冲突时，先撤销控制资格、保留原owner及硬件事实；只有明确可组合的专用交接才能接管。未知竞争继续受控fault，不强清busy/fault或重新初始化PCD。后台MSC IO的所有权独立于控制事务，不得随EP0停止清除pending_io。

## 5. 实施前必须关闭的审批表

| ID | 需要的证据/决策 | 当前状态 |
| -- | -- | -- |
| H1 | control IN0在新SETUP下SNAK/INEPNE/EPDIS/EPDISD的确切适用顺序，澄清p2840 | UNRESOLVED / BLOCKED |
| H2 | IN0 EPENA因SETUP/正常完成变0时，无新disable事件的TX读侧静止与Flush条件 | UNRESOLVED / BLOCKED |
| H3 | IN0的XFRC/EPDISD与多包、Status ZLP的旧事件归属；单一退休证明 | INFERRED；待H1/H2及源码细化 |
| H4 | 目标TX0 Flush与其他IN producer、Reset/关闭GRSTCTL的互斥 | INFERRED；须逐调用者闭合 |
| H5 | 新请求入队与旧引用退休的原子交接，包括满队列和hook重入/抢占 | INFERRED；沿用R1机制但需EP0专用验证 |
| H6 | ES0445 Rev7 2.24.5 PIO ZLP适用性及板上GSNPSID | UNRESOLVED / NOT_TESTED；不未经核验改时序 |

未获H1/H2明确证据前，不批准把候选寄存器序列写入生产代码。证据充分后仍需人工批准实施；本报告本身不是实施授权。

## 6. 后续实施验收计划（本轮未执行）

NOT_TESTED（计划）：必须使用修改后的真实完整RAM MSC AXF，不能让Fake DCD、模型收到SETUP就清IN0.EPENA或FIFO制造成功。

- 正常Data IN单包/多包/ZLP、Status IN被SETUP抢占；只发一次最新请求，继续描述符、SET_ADDRESS、SET_CONFIGURATION、CBW/INQUIRY/READ。
- XFRC先到、取消先到、XFRC/EPDISD同轮、旧TXFE及延迟事件；确保无旧Status、无重复释放和无旧source续写。
- 连续SETUP、RX/DONE错位、tag回绕、序号耗尽、队列31/32与32/32、发布重试；最新者优先且旧停止身份不变。
- INEPNE/EPDISD缺失、EPENA异常、TX Flush不完成、DWT失效/回绕、共享命令占用；有界故障并冻结证据，不清旧引用。
- Reset/Disconnect/Group Close在fence、disable、Flush、入队和Class处理各点抢占；共享RX持续处理其他OUT及SETUP。
- 保持R1 29、R1B33、MSC12、等待9、原Reset31/Closure25/R2及历史FAIL；旧断言迁移单独分类审批，不修改旧预期制造全绿。
- 完整RAM MSC重编0 Error/0 Warning、唯一OTG owner、无新Task，提交代码Diff和AXF/MAP/哈希。软件通过仍不能替代后续人工批准的实板验收。

## 7. 本轮交付与完整性

VERIFIED：本轮没有重跑构建/软件测试；没有新生产版本或实板结果。最后已验收的软件数据仍是cc0ad35对应报告，不把它重新标为整轮完成。远端查询本轮首次发生连接重置，推送结果以最终交付核对为准。

源码读取SHA256（仅审计，不更改）：

- `ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c`：`9825f06c6d938706339fdaeb33cbb8c9355cce9628a5ac15a069859d7e306f94`。
- `ThirdParty/tinyusb/src/device/usbd.c`：`7985412cbc05669ce643cb968ab94adf6d5a998e3c0917459ef9395bdf9ea9f8`。
- `ThirdParty/tinyusb/src/portable/synopsys/dwc2/dwc2_common.c`：`655929ad5fe7dfbca0f3fb4cfed0f4ddf0ef5295c86ed6f7de23b3e65702658a`。
- `ThirdParty/tinyusb/src/device/dcd.h`：`1e394abc93b7316701f26ef11bae81d5b347f07424e44e788c1b05ae9d438229`。
