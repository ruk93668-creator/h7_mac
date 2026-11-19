# RM0399 修订与 Endpoint 停止时序核验记录

## P1-A Exit Review：九项操作与失败契约（2026-11-19）

本轮Git边界及最终五列矩阵以[HardwareEvidence最终审计](USB_EndpointLifecycle_HardwareEvidence.md)为准。以下RM页码全部为Rev5印刷页码；操作序列是手册要求或明确标注的设计推导，未执行寄存器写入。普通单端点禁用、Reset型Transfer Stop、ISO incomplete及EP0严格分开。

### 通用诊断与失败规则

**VERIFIED（源码）**：`CM7/BSP/bsp_usb.c:758`的`Usb_HalResult`将HAL_BUSY映射SYS_ERR_BUSY，HAL_TIMEOUT映射SYS_ERR_TIMEOUT，HAL_ERROR映射SYS_ERR_IO；`BSP_USB_AbortEp`在`:346`的共享RX保护可不执行HAL直接返回SYS_ERR_BUSY。`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1992`的Abort直返LL；`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:915`的EPENA轮询耗尽返回HAL_ERROR，不返回“已提交异步取消”，也不承诺未来取消callback。

**INFERRED（所有九项共用）**：每一等待阶段均需保存原因、阶段、原始HAL结果、Session/Transfer身份、寄存器证据和期限；SYS_ERR_BUSY区分软件前置拒绝与HAL锁竞争，不能等价Pending。状态已开始改变后失败/超时进入RECOVERY_REQUIRED并保留资源；未尝试的BUSY保持原有身份，Task可在原因消除后重试。超时不作硬件静止或Buffer归还证明。重复停止先Reconcile，不覆盖前次证据。没有RM规定数值的阶段不编造微秒上限；迭代计数不是墙钟预算，IRQ被饿死也不算硬件故障已证实。

### 1. Bulk IN停止

| 审核字段 | 最终记录 |
| -- | -- |
| 手册/寄存器 | VERIFIED：RM60.14.55/56，2840～2843；60.15.6，2917～2920。DIEPCTL.EPENA[31]/EPDIS[30]/SNAK[27]/CNAK[26]/NAKSTS[17]；DIEPINT.INEPNE[6]/EPDISD[1]/XFRC[0] |
| 前置条件 | VERIFIED：非零已使能IN端点，EPDIS只能在EPENA已置位时写；先停止AHB写入。INFERRED：发布提交/续包fence，记录PIO source身份；先裁决已经发生的正常完成 |
| 顺序 | VERIFIED：停止PIO写→SNAK→等INEPNE→EPDIS+SNAK→等EPDISD→定向TX Flush并等清零。普通流程不是Reset专用简化流程 |
| 完成条件 | VERIFIED：禁用中断与EPENA/EPDIS清零；INFERRED：还须匹配事件与PIO/FIFO/ledger均退休。XFRC先完成则走正常完成分支，不人为等待从未发起的禁用事件 |
| 超时/失败 | UNRESOLVED：RM未给普通取消的通用墙钟上界；应用有界期限待定。INFERRED：INEPNE/EPDISD/Flush任一失败均无可释放Proof，保留buffer |
| 当前源码/差距 | VERIFIED：LL `USB_EPStopXfer`，`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:915`无INEPNE/EPDISD等待；BSP `HAL_PCD_DataInStageCallback`，`CM7/BSP/bsp_usb.c:633`仍可在`:655`续包；Platform `USB_Device_AbortEp`，`CM7/Platform/usb_device.c:426`只依赖BSP结果清busy。UNRESOLVED：无完整取消证明 |

### 2. Bulk OUT停止

| 审核字段 | 最终记录 |
| -- | -- |
| 手册/寄存器 | VERIFIED：RM60.15.6，2905、2909～2912；60.14.65，2852。DCTL.SGONAK[9]/CGONAK[10]；GINTSTS.GONAKEFF[7]/RXFLVL[4]；DOEPCTL.EPENA[31]/EPDIS[30]/SNAK[27]；DOEPINT.EPDISD[1]/XFRC[0] |
| 前置条件 | VERIFIED：非零活跃OUT；共享RX须有空间容纳NAK状态项，正常消费者仍须工作，SETUP仍可接收。INFERRED：fence新接收提交，但保留旧buffer供RX排空 |
| 顺序 | VERIFIED：SGONAK→正常RX状态/负载按EPNUM消费→等GONAKEFF→目标EPDIS+SNAK→等EPDISD→按CGONAK退出全局NAK。INFERRED：退出前确认没有别的停止参与者仍持有全局NAK需求 |
| 完成条件 | VERIFIED：匹配禁用中断且EPENA/EPDIS清零；INFERRED：目标在途PIO写与旧状态均收尾。GONAKEFF只阻止后续普通OUT，并不等于EP0/RX全空 |
| 超时/失败 | INFERRED：保留RX buffer、记录哪个阶段失败；不得为了完成而抛弃其他OUT或SETUP。UNRESOLVED：全局NAK拥有者与HAL自动CGONAK的协调，普通取消期限 |
| 当前源码/差距 | VERIFIED：`BSP_USB_AbortEp`，`CM7/BSP/bsp_usb.c:337`缺全局NAK顺序；HAL RX消费`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1080`，OUT EPDISD路径`:1144`会自动CGONAK；Platform关IRQ覆盖整个调用。INFERRED：直接在原关IRQ区增加GONAKEFF等待可能自锁 |

### 3. Isochronous IN停止

| 审核字段 | 最终记录 |
| -- | -- |
| 手册/寄存器 | VERIFIED：RM60.15.6，2918、2921～2923；60.14.55/56，2840～2843。DIEPCTL.SNAK/EPDIS/EPENA/EONUM，DIEPINT.EPDISD/XFRC/ITTXFE，GINTSTS.IISOIXFR，DIEPTSIZ.XFRSIZ/PKTCNT/MCNT |
| 前置条件 | VERIFIED：按单帧调度；先区分正常活跃停止或已发生incomplete，识别目标ISO端点。INFERRED：禁止新frame/PIO续包，保留frame lease；不是Buffer DMA取包流程 |
| 顺序 | VERIFIED：incomplete特定序列为停AHB写→SNAK+EPDIS→等禁用事件→Flush或下一帧覆盖。INFERRED：主动停止候选采用2918页普通IN流程；incomplete用2923页特定流程；Stop选择Flush而不是新帧覆盖 |
| 完成条件 | VERIFIED：NAK对ISO可发送ZLP，不证明有效payload发出；incomplete需EPDISD。INFERRED：完整身份/PIO/定向FIFO退休后才归还frame，不把取消记作成功帧 |
| 超时/失败 | INFERRED：期限内缺事件/Flush失败保留frame；正常XFRC/帧边界先完成须Reconcile。UNRESOLVED：主动停止候选对全部帧时序、迟到ISO IRQ和普通流程INEPNE可达性的证明；无条件通过被否决 |
| 当前源码/差距 | VERIFIED：`CM7/BSP/bsp_usb.c:633`的PIO完成回调与`:727`的ISO incomplete路径；HAL `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1234`的EPDISD会Flush并丢结果。UNRESOLVED：无Frame Lease/Transfer身份停止Proof；UVC未改 |

### 4. EP0控制传输取消

| 审核字段 | 最终记录 |
| -- | -- |
| 手册/寄存器 | VERIFIED：RM60.14.61，2846～2847；60.15.6，2903～2908。DOEPCTL0.EPDIS[30]不可用于禁用OUT0；NAK/STALL/全局NAK均不阻止SETUP；DOEPTSIZ0.STUPCNT、DOEPINT.STUP/XFRC，EP0 IN/OUT分立 |
| 前置条件 | VERIFIED：保存最新SETUP，按控制状态解释事件，Reset按2903页重新初始化EP0/FIFO/中断。INFERRED：Token立即失效只取消控制资格，不释放旧PIO引用 |
| 顺序 | VERIFIED：SETUP经RXFLVL读取payload/状态、STUP后处理最新请求；配置所需端点完成后才安排status IN。UNRESOLVED：任意主动EP0 IN/OUT取消没有一套已批准通用安全寄存器算法，不能以Close/Open或OUT EPDIS替代 |
| 完成条件 | INFERRED：旧控制身份失效、源/目的PIO引用退休、旧完成不会驱动新事务，状态阶段必须属于当前Token；状态包提交也不是主机ACK证明。UNRESOLVED：当前实现完整证据 |
| 超时/失败 | INFERRED：无法证明旧资源静止则保留，否决新成功status并进入受控恢复，不能无条件重启PCD或清busy |
| 当前源码/差距 | VERIFIED：Platform `Usb_Setup`，`CM7/Platform/usb_device.c:1321`可能Close/Open忙EP0；BSP `BSP_USB_CloseEp`，`CM7/BSP/bsp_usb.c:276`的EP0分支`:296`仍调用通用HAL Abort→LL EPDIS。INFERRED：与OUT0限制存在直接不兼容风险；本轮没有修改 |

### 5. TX FIFO Flush

| 审核字段 | 最终记录 |
| -- | -- |
| 手册/寄存器 | VERIFIED：RM60.14.5，2784；60.15.6，2918。GRSTCTL.AHBIDL[31]、TXFNUM[10:6]、TXFFLSH[5] |
| 前置条件/顺序 | VERIFIED：core不得正在FIFO读写；NAK effective保证读侧条件、AHBIDL保证master写侧；停CPU/PIO后选正确FIFO并置TXFFLSH，等待自清；期间不改变TXFNUM |
| 完成条件 | VERIFIED：Flush位清零；INFERRED：必须保存本次真实结果与前置条件；不能把事后位0当成历史调用未超时。FIFO空也不证明软件source引用零 |
| 超时/失败 | VERIFIED：手册标称8个较慢PHY/AHB时钟；未规定故障上界。INFERRED：期限与HAL锁失败分别记录，失败不归还资源；不先发布成功再检查用时 |
| 当前源码/差距 | VERIFIED：LL `USB_FlushTxFifo`，`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:452`返回真结果但迭代长；HAL `HAL_PCD_EP_Flush`，`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:2018`及IRQ`:1236`丢返回；BSP `CM7/BSP/bsp_usb.c:353`无法据wrapper证明成功。UNRESOLVED：不改HAL/LL的短有界封装及IRQ副作用处理 |

### 6. RX FIFO Flush

| 审核字段 | 最终记录 |
| -- | -- |
| 手册/寄存器 | VERIFIED：RM60.14.5，2784；60.15.6，2905、2909～2911。GRSTCTL.RXFFLSH[4]/AHBIDL[31]，GRXSTSP.EPNUM/PKTSTS/BCNT，RXFLVL/GONAKEFF |
| 前置条件/顺序 | VERIFIED：整个共享RX无读写事务才可Flush；普通OUT Disable不是默认Flush流程。Reset型Transfer Stop另列使能OUT、AHB idle/RX Flush、全局NAK、禁用active OUT；其all-EP措辞与OUT0限制必须协调，不能照抄 |
| 完成条件 | VERIFIED：RXFFLSH清零；INFERRED：全局scope内所有OUT/SETUP已被安全处理，才可作为Proof一部分。普通取消优先保持正常RX排空，禁止第二消费者随意pop |
| 超时/失败 | VERIFIED：Reset型流程要求Flush超时小于10ms，因EP0新包未清时最多重试一次；不是普通单EP取消的任意重试许可。INFERRED：重试仍失败则锁定，不能抛包/清active模拟成功 |
| 当前源码/差距 | VERIFIED：LL `USB_FlushRxFifo`，`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:489`；BSP `BSP_USB_FlushEp`，`CM7/BSP/bsp_usb.c:411`从EP1开始guard；HAL wrapper丢真结果。UNRESOLVED：共享RX与EP0新SETUP的隔离、真实期限与Reset重试集成 |

### 7. EPDISD / XFRC事件退休

| 审核字段 | 最终记录 |
| -- | -- |
| 手册/寄存器 | VERIFIED：RM60.14.45，2832；60.14.56/62，2842～2843、2848～2849；60.15.6，2918。DAINT为汇总；DIEPINT/DOEPINT中EPDISD[1]、XFRC[0]独立，EPENA在通知前可清零 |
| 前置条件/顺序 | INFERRED：提交禁用前记录方向/Session/Generation及已有事件；HAL消费前保存必要原始事件；按正常完成或取消证据唯一裁决，再W1C已归属事件，保留软件退休记录 |
| 完成条件 | INFERRED：最终结果只发布一次，处理器/队列/PIO引用均已释放，新transfer不得在旧无标签硬件事件仍可误归因时启动。硬件中断不携软件Generation，不能仅凭收到事件时的当前generation贴标签 |
| 超时/失败 | INFERRED：丢失归因证据、EPDISD迟到或W1C后仍有来源不明事件，保持RECOVERY_REQUIRED；不能把Event Epoch变化当丢弃旧资源收尾的理由 |
| 当前源码/差距 | VERIFIED：HAL `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1144`、`:1234`消费EPDISD，Bulk无普通取消callback；BSP `CM7/BSP/bsp_usb.c:362`会清XFRC/EPDISD/TOC，无旧身份账本。UNRESOLVED：完整事件退休协议 |

### 8. IRQ与PIO并发

| 审核字段 | 最终记录 |
| -- | -- |
| 手册/寄存器 | VERIFIED：RM60.14.3，2780：GAHBCFG.GINTMSK[0]不阻止core更新状态；60.14.50，2836：DIEPEMPMSK；60.15.6，2905/2909/2918：RX消费/NAK生效/先停PIO |
| 前置条件/顺序 | INFERRED：Task短临界区发布fence/身份→退出临界区保留必要IRQ/RX服务→分阶段等待硬件证据→短临界区最终裁决。HAL TXFE写入、BSP回调续包、Task新提交均须受同一停止契约约束 |
| 完成条件 | INFERRED：无执行中PIO访问、不会继续写旧buffer/FIFO、旧中断不再重新arm；不以NVIC disabled或TXFE mask代替硬件停止 |
| 超时/失败 | INFERRED：恢复本次确实改变且允许恢复的mask状态，保留endpoint fence/资源；不得为避免超时永久关IRQ。UNRESOLVED：现有HAL EPDISD自动Flush可能长轮询的可安全隔离方式；不能声称短Task Abort已解决全部ISR时延 |
| 当前源码/差距 | VERIFIED：Platform `USB_Device_AbortEp`，`CM7/Platform/usb_device.c:426`关IRQ覆盖HAL；BSP `BSP_USB_Stop`，`CM7/BSP/bsp_usb.c:188`先关NVIC；`OTG_FS_IRQHandler`在`:619`，PIO续包`:655`；HAL RX读取`:1080`。UNRESOLVED：短临界区+必要硬件服务的完整串行化 |

### 9. Endpoint Buffer释放条件

| 审核字段 | 最终记录 |
| -- | -- |
| 手册/寄存器 | VERIFIED：RM60.14.56/62，2843/2849：XFRC表示编程transfer的USB/AHB完成；60.15.6，2918/2923：禁用后FIFO处理与PIO停写。RM没有Session、worker、frame lease或C指针所有权定义 |
| 前置条件/顺序 | INFERRED：禁止新提交→唯一正常完成/取消终态→所需硬件/FIFO证据→PIO/IRQ/队列引用归零→由持有者恰好一次归还buffer。IDMA/Storage Worker仍持有时USB Proof不得释放共用数据 |
| 完成条件 | INFERRED：`USB_release_eligible = identity_matches && terminal_evidence && pio_refs_zero && irq_events_retired && fifo_scope_safe && usb_ledger_refs_zero`；整buffer可复用还须外部owner（例如SD IDMA）独立归还。单纯busy=false、EPENA=0、DMA关闭、Token失效均不足 |
| 超时/失败 | INFERRED：保留Buffer与RAW租约；复核硬件/引用并完成旧资源收尾才可恢复。不强制清busy/maxpacket/xfer_buff或重启硬件掩盖未知引用 |
| 当前源码/差距 | VERIFIED：BSP `CM7/BSP/bsp_usb.c:365`清TX元数据，`:368`清RX active，`:328`Close清buffer指针；Platform `CM7/Platform/usb_device.c:445`清busy；`CM7/Platform/usb_device.h:49`只有布尔快照。UNRESOLVED：不存在完整Proof/引用账本；USB DMA关闭不排除CPU PIO仍访问应用buffer |

### P1-A边界与后续门禁

**VERIFIED（证据交付）**：本轮只更新四份文档。普通IN/OUT、ISO incomplete、EP0禁止条件、FIFO/IRQ基本语义已有手册依据。**UNRESOLVED**：本节明确保留的EP0完整取消、Reset与OUT0例外、主动ISO流停止、HAL IRQ副作用隔离、有界预算/退休账本，需要独立设计和后续验证；不擅自补完未知时序。

**NOT_TESTED**：没有编译或新故障注入/实板Stop操作；没有证明任何生产路径现已满足上列契约。不进入P1-B，等待人工审核。

---

## 前次正文取得与访问历史

## 2026-11-19 正文取得后的核验结论（替代下方缺件状态）

**VERIFIED（本机PDF）**：用户报告浏览器保存成功；实际文件在`C:/Users/29062/Downloads/RM0399_Rev5.pdf`。保留下载原件并逐字节复制到指定的`D:/32/STM32H7/h7_mac_lifecycle/Docs/references/RM0399_Rev5.pdf`。代理未能控制浏览器，因此浏览器URL/下载会话属于用户提供的来源陈述，没有独立HTTP响应或ST发布哈希可比对。未使用curl重新下载。

| 完整性项目 | 结果与来源 |
| -- | -- |
| 首页 | VERIFIED：RM0399，STM32H745/755 and STM32H747/757，Rev5，October2026；已渲染查看 |
| 大小 / magic | VERIFIED：72,029,379字节 / `%PDF-1.3` |
| 总页数 | VERIFIED：3535；PDF索引页号与印刷页号一致 |
| SHA256 | `61d07b67deb291aed8a5cf8019c94add0a1065318542e57b3287d2694b14bd60` |
| 全文结构 | VERIFIED：PyMuPDF逐页加载/提取3535页成功，0空文本页，3535个页脚编号全部匹配；末页3535/3535可读；尾部有EOF；is_repaired=false，无密码 |
| 完整性边界 | INFERRED：未发现截断/页数不完整；NOT_TESTED：与ST服务器独立文件哈希比对、数字签名真实性验证 |
| 指定提取 | VERIFIED：2747～2924页，含首尾，共178页；`D:/32/STM32H7/h7_mac_lifecycle/output/MSC24/udlm-rm0399/RM0399_pages2747-2924.pdf` |
| 提取件SHA256 | `e1994256464560ab077fbe23a30ba4562c17be92d53f887864a25fc8f8f2195e` |
| 原始核验记录 | `D:/32/STM32H7/h7_mac_lifecycle/output/MSC24/udlm-rm0399/validation.json`；同目录含逐页文本与关键页图像 |

**VERIFIED（范围）**：指定页段已完整提取；另读取2745～2746页确定章节上下文和实例配置。正文证据主要来自60.14寄存器与60.15.6设备编程模型，不能将同页段内的Host channel算法混入Device endpoint算法。已视觉复核首页、2747页实例图、2910页OUT、2918页IN和2923页ISO流程；不是全部3535页逐页视觉审校。

### A. USB2 OTG_FS实例与模式

**VERIFIED（RM）**：60.3/60.4.1，2746页表538列出两个OTG_HS实例；2747页图794明确USB2 OTG_HS为OTG_HS2并有别名OTG_FS，不能通过引脚使用HS，具有集成FS PHY。因而此前“OTG_HS章节是否适用于USB2_OTG_FS”的名称映射缺口已关闭。2780页60.14.3的GAHBCFG.DMAEN[5]=0为slave模式。

**VERIFIED（源码）**：`CM7/BSP/bsp_usb.c:99`、`:105`、`:108`、`:111`分别配置USB2_OTG_FS、FULL speed、USB DMA关闭、embedded PHY。**INFERRED**：当前须采用slave/PIO的软件排空模型，不能套用Host channel或Buffer DMA专用停止算法。ES0445 2.24.2的DMA条件及2.24.5包含slave条件仍保留，参见SiliconRevision文档；实例映射已不再是缺件项，但实际触发情况仍NOT_TESTED。

### B. 普通Bulk IN停止及正常完成

**VERIFIED（RM）**：60.15.6，2917～2918页普通IN禁用要求：先停止对该端点的AHB/PIO写入；置SNAK；等待DIEPINT.INEPNE；对已使能端点置EPDIS与SNAK；等待DIEPINT.EPDISD，届时EPENA/EPDIS清零；再处理对应TX FIFO，等待GRSTCTL.TXFFLSH清零。2918页对periodic IN另要求读取DIEPTSIZ确定发送量。

**VERIFIED（RM）**：60.14.55，2840页EPDIS[30]只应在EPENA[31]已置位时设置；必须等禁用中断。60.14.56，2843页INEPNE[6]表示NAK已在core生效，不保证总线上实际发送了NAK；EPDISD[1]与XFRC[0]是不同事件。2919～2920页正常非周期IN在XFRSIZ/PKTCNT都到零后生成XFRC并清EPENA。

原文短摘录（2918页第1步）：“stop writing data on the AHB”。这是停止CPU/PIO续包的前置条件，不是仅等待USB DMA。

**INFERRED（契约）**：正常XFRC先赢得同一Transfer的终态时可走正常完成收尾；取消先赢时要求匹配的禁用证据。EPENA=0可能来自正常完成，不能单独充当取消成功；也不能无条件等待一个从未发起禁用的EPDISD。FIFO与软件引用、迟到事件仍需另证。

### C. 普通Bulk OUT停止与共享RX

**VERIFIED（RM）**：60.15.6，2909页SGONAK后仍可能接收有效OUT，直到GONAKEFF。GONAKEFF要在应用从共享RX FIFO弹出相应Global OUT NAK状态项后才生效；SETUP仍可进入FIFO。2910页普通OUT禁用：进入Global OUT NAK、等待GONAKEFF、对目标置EPDIS/SNAK、等DOEPINT.EPDISD，确认EPENA/EPDIS清零，最后按DCTL.CGONAK命令退出全局NAK，让其他OUT继续。

**VERIFIED（RM）**：60.14.65，2852页约束EPDIS只对已使能端点；60.14.62，2849页EPDISD[1]表示应用请求禁用完成，XFRC[0]表示该编程传输的AHB和USB完成。2911～2912页正常OUT完成涉及RX状态项的消费，不能仅凭USB线上的ACK判断目标内存已可归还。

**INFERRED（并发）**：整个等待期间完全关闭负责RXFLVL的IRQ，可能阻碍GONAKEFF的形成。需要保留正常RX消费能力，同时阻止新业务提交与旧buffer释放。不能新建第二个随意弹GRXSTSP的“清理器”；必须保留按EPNUM/PKTSTS分发数据、SETUP与状态的单一消费契约。

### D. ISO IN：正常、未完成与停止

**VERIFIED（RM）**：60.15.6，2921～2922页限定periodic IN按单帧调度；ISO FIFO数据不足可发ZLP，不能由此断言帧负载发送成功。正常ISO完成还须结合XFRC、ITTXFE及DIEPTSIZ的XFRSIZ/PKTCNT；IISOIXFR代表至少一个ISO端点该帧未完成，不等于buffer已退休。

**VERIFIED（RM）**：2923页针对incomplete ISO IN，要求识别受影响端点、尽快停止该端点AHB写入、置SNAK/EPDIS、等禁用中断，随后Flush相关TX FIFO，或在下一microframe新transfer中覆盖FIFO。此特定流程未列普通IN流程的独立INEPNE等待，不能声称二者算法完全相同。

**INFERRED（本项目）**：全生命周期停止应选择可审计Flush与事件退休，不采用“启动下一帧覆盖”作为停止证明。UVC frame lease须待PIO不再引用且旧事件完成收尾后归还；ISO取消不等于成功发送帧。**UNRESOLVED**：未完成ISO算法之外，正常活跃流主动停止时采用哪条RM流程及有界预算，仍须P1设计/测试明确，不能由本次文字核验直接签发生产Proof。

### E. EP0与控制事务边界

**VERIFIED（RM）**：60.14.61，2846页DOEPCTL0.EPDIS[30]规定：“cannot disable control OUT endpoint 0”。2847页EP0 USBAEP始终有效，SNAK/STALL不阻止SETUP。60.15.6，2906～2908页说明slave模式SETUP可在NAK/EPENA条件外接收；应用处理最新SETUP，不能把旧控制事务资格带到新SETUP。2903页规定Reset后的EP0中断、FIFO、STUPCNT=3初始化；2904页配置/接口变更须完成所需端点配置后才安排状态IN。

**INFERRED**：EP0 OUT不能套普通OUT EPDIS取消证明。全局OUT NAK也不是SETUP屏障；Deferred Status Token失效、控制事务路由和实际FIFO/PIO排空必须分别处理。软件Token变化不代表硬件已停，也不代表状态包已获主机确认。**UNRESOLVED**：EP0 IN的专用取消策略与新SETUP竞争尚无本项目实现证明；2840页通用寄存器介绍明确排除EP0，不能仅因地址范围含0便套用非零EP算法。

### F. FIFO、NAK、IRQ退休的寄存器依据

| 资源 | RM0399 Rev5位置/位 | 已核验要求与工程含义 |
| -- | -- | -- |
| GRSTCTL | 60.14.5，2783～2785页；AHBIDL[31]、TXFNUM[10:6]、TXFFLSH[5]、RXFFLSH[4] | VERIFIED：字段名是TXFNUM。Flush前要证明没有相应FIFO读写；TX要NAK effective及AHB idle条件；等待Flush位自清，标称8个较慢的PHY/AHB时钟。不把标称周期当故障超时上限 |
| DCTL | 60.14.41，2827～2828页；SGONAK[9]、CGONAK[10]、GONSTS[3]、SDIS[1] | VERIFIED：全局OUT NAK除SETUP；退出使用CGONAK写命令。SDIS是总线断开呈现，不是资源自动释放 |
| GINTSTS | 60.14.6，2785～2789页；GONAKEFF[7]、RXFLVL[4] | VERIFIED：状态与mask不同；GONAKEFF对应工程BOUTNAKEFF别名，不能把masked当retired |
| GAHBCFG | 60.14.3，2780页；GINTMSK[0] | VERIFIED：屏蔽中断线不阻止core更新状态，不等于暂停硬件传输 |
| GRXSTSP | 60.14.10/11，2794～2796页；60.15.6，2905页 | VERIFIED：读会pop；必须有RXFLVL，消费对应字节数并保留SETUP/其他OUT。版面中host/device文字与EPNUM/CHNUM布局存在交叉，采用设备编程模型明确的EPNUM/PKTSTS，不照标题误套host布局 |
| DIEPCTL/DOEPCTL | 60.14.55，2840～2841页；60.14.65，2851～2853页 | VERIFIED：EPENA[31]、EPDIS[30]、SNAK[27]、CNAK[26]、NAKSTS[17]；非零端点定义与EP0分开 |
| DIEPINT/DOEPINT | 60.14.56，2842～2843页；60.14.62，2848～2849页 | VERIFIED：EPDISD[1]、XFRC[0]；IN另有INEPNE[6]。有W1C和只读位，不能整寄存器无差别清除 |
| DAINT/DAINTMSK | 60.14.45/46，2832～2833页；DIEPEMPMSK 60.14.50，2836页 | VERIFIED：端点汇总来自具体端点事件；退休须识别源端点/方向。屏蔽TXFE只能防ISR续写的一部分，不能替代所有调用入口fence |

**VERIFIED（源码映射）**：`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:28607`为TXFNUM位6；`:28618`为AHBIDL位31；`:28742`为BOUTNAKEFF位7；`:28456`/`:28459`为SGONAK/CGONAK；`:29494`为INEPNE位6；`:29479`/`:29646`为IN/OUT EPDISD位1。名称差异已映射，不改变工程头文件。

**UNRESOLVED（手册内差异）**：2910～2911及2918页Transfer Stop文本把DIEPINT/DOEPINT中的禁用中断写为EPDIS；寄存器位表名为EPDISD，须按寄存器核对。2910页Reset型OUT Transfer Stop还含“使能所有OUT、RX Flush、等待全局NAK、禁用active OUT”，不能据此对EP0执行被2846页明令禁止的EPDIS。Reset特有范围/EP0豁免及Flush时EP0新包的处理须单独设计，不照抄一套all-EP循环。

### G. Normal Disable与Reset型Transfer Stop不可混用

**VERIFIED（RM）**：2910～2911页Reset型OUT Transfer Stop在RX Flush阶段要求AHBIDL、read-modify-write RXFFLSH，轮询超时小于10ms；若EP0包使位仍未清，可回到Flush步骤重试一次。2918页Reset型IN Transfer Stop另列EPDIS、等禁用中断、各TX Flush，不等同上半页普通单端点禁用流程。

**INFERRED**：这些是指定上下文流程，不授权对普通OUT取消Flush全局RX，也不意味着Disconnect/Stop/BusReset共用无条件硬件清零。小于10ms约束来自该Reset流程，不能直接作为每个Endpoint取消通用预算。实际Task/IRQ串行化、同一Transfer的XFRC/EPDISD归因、迟到事件和Buffer ledger仍须实现验证。

### H. 六份真实源码对照矩阵（当前基线c60daae）

| 文件/真实位置 | 当前源码事实（VERIFIED） | 与正文差距（INFERRED/UNRESOLVED） |
| -- | -- | -- |
| `CM7/BSP/bsp_usb.c:188`、`:337`、`:411`、`:655` | Stop先关IRQ；Abort直接HAL；OUT共享RX guard；PIO在完成回调续包 | 普通OUT缺全局NAK/接收排空证明；普通IN缺独立INEPNE证明；Flush真返回丢失；IRQ关闭与RX pop依赖冲突；续包需身份fence |
| `CM7/BSP/bsp_usb.h:27` | 有数据/reset/ISO通知，无普通Bulk取消完成证据接口 | 需要具名Proof/事件退休契约，不能等待不存在的通用取消回调 |
| `CM7/Platform/usb_device.c:426`、`:706`、`:1321` | 关USB IRQ包住Abort；BSP成功后清busy；新SETUP可能Close/Open EP0 | HAL_OK不是手册完整停止证明；EP0需专门策略；等待GONAKEFF期间不应切断唯一RX消费者 |
| `CM7/Platform/usb_device.h:49` | opened/busy/stalled/type/MPS/result | 不能表述匹配Transfer identity、PIO引用、NAK/FIFO/事件证据；当前不是QuiescenceProof |
| `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1080`、`:1144`、`:1234`、`:1992`、`:2018` | IRQ读取GRXSTSP；OUT EPDISD可清全局NAK；IN EPDISD调用Flush并丢结果；Bulk无取消callback；Abort直返LL | HAL可能先消费/W1C硬件证据，BSP pre/post需保存；全局NAK退出要协调scope；wrapper HAL_OK不能证明Flush成功 |
| `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:452`、`:489`、`:915` | Flush有真返回/长迭代；Abort写SNAK/EPDIS只轮询EPENA | 未完整执行普通IN/OUT手册序列；HAL_ERROR是轮询失败而非已提交异步取消；LL Flush不自动证明前置NAK/PIO/RX共享安全 |

### I. 更新后的Hardware Stop Matrix与判定

| 类型 | 正文依据 | 已关闭的证据缺口 | 仍未证明 |
| -- | -- | -- | -- |
| Bulk IN | 2917～2918、2840、2843、2920页 | VERIFIED：普通停止、INEPNE/EPDISD/XFRC语义 | UNRESOLVED：PIO串行化、竞态归因、旧事件身份、有界时间和FIFO真返回集成 |
| Bulk OUT | 2909～2912、2849、2852页 | VERIFIED：全局NAK、RX消费、EPDISD | UNRESOLVED：共享RX/EP0保护与Task/IRQ协作 |
| ISO IN | 2921～2923页 | VERIFIED：periodic、incomplete专用序列 | UNRESOLVED：主动流停止算法选择、frame lease、迟到IRQ和SOF竞态 |
| EP0 | 2846～2847、2903～2908页 | VERIFIED：OUT不可EPDIS、SETUP例外、重新初始化 | UNRESOLVED：IN/OUT控制事务专用Proof和状态阶段否决集成 |
| TX Flush | 2784、2918页 | VERIFIED：前置条件、TXFNUM与自清 | UNRESOLVED：时间上界、锁、真实错误保存 |
| RX Flush | 2784、2910页 | VERIFIED：全局共享及Reset特有重试 | UNRESOLVED：正常单端点取消不能破坏他人/EP0的可实施方案 |
| IRQ退休 | 2780、2832、2842～2843、2848～2849页 | VERIFIED：mask不冻结core、源事件区分 | UNRESOLVED：HAL消费前证据、Session/Transfer退休账本 |

**VERIFIED（本轮范围）**：手册原件缺失与USB2实例映射两个缺口已关闭；未修改任何生产/测试/Keil/Core/HAL/LL，也未操作实板。PDF保留本机，Git仅提交文档。下方旧“无RM正文”内容作为c60daae之前的访问历史保留。

**UNRESOLVED（验收）**：本次是正文证据审查完成，不等于P1停止实现/软件验收完成。不能宣称已有EndpointQuiescenceProof或重连修复；P1-B/P2未开始。后续先完成上述设计中的并发/EP0/Reset及边界处理，再按既定授权范围评审和实施，不直接改变ST Core/HAL。


> 本轮基线：`3a765feeee8d4cb3c1cc553337caf8cf1b63f6b4`；分支：`codex/usb-device-lifecycle-manager`；日期：2026-11-19（Asia/Shanghai）。
> 仅硬件证据与文档，不修改生产/测试/Keil/Core/HAL/LL，不实施P1-B/P2。
> VERIFIED须注明来源；INFERRED为推断；UNRESOLVED为缺证据；NOT_TESTED为未执行验证。

## 1. 当前结论

**UNRESOLVED**：未取得RM0399 Rev5完整正文。指定官方下载返回HTTP567、0字节，无PDF候选文件；浏览器获取受10MiB以上大小限制阻断。依据任务“官方错误/不完整则停止并报告”的要求，停止依赖正文的四类停止时序审计，未使用其他STM32系列手册、目录或转载预览替代。

**VERIFIED（过程）**：完成本机指定位置搜索及安装目录补搜，匹配文件总数0。没有可填写的RM PDF路径/页数/内容SHA256，不能用错误页或下载日志的SHA冒充手册SHA。本次没有实施任何停止寄存器序列。

## 2. 本机搜索清单

采用文件名 `RM0399*.pdf` / `DM00176879*.pdf`，大小写不敏感。搜索隐藏项目，跳过.git、symlink/reparse point；不进入禁止工程目录。访问错误0。未下载的云占位/跳过链接不构成已取得正文。

| 根目录 | 状态 | 匹配数 |
| -- | -- | -- |
| `D:/32/STM32H7` | VERIFIED：遍历允许的子树 | 0 |
| `C:/Users/29062/Downloads` | VERIFIED：实际已知文件夹路径一致 | 0 |
| `C:/Users/29062/Documents` | VERIFIED：实际已知文件夹路径一致 | 0 |
| `C:/Users/29062/Desktop` | VERIFIED：实际已知文件夹路径一致 | 0 |
| `C:/ST` | VERIFIED：不存在 | 不适用 |
| `C:/Program Files (x86)/STMicroelectronics` | VERIFIED：已搜索 | 0 |
| `C:/Users/29062/STM32Cube` | VERIFIED：已搜索 | 0 |
| `C:/32` | VERIFIED：安装注册记录指向，但目录不存在 | 不适用 |
| `D:/32` | VERIFIED：依据CubeMX安装位置补搜；已搜STM32H7子树不重复 | 0 |
| `D:/Users/29062/AppData/Local/Programs/STM32CubeMX` | VERIFIED：依据CubeMX安装记录补搜 | 0 |

**VERIFIED**：第一轮遍历140404个目录，0个访问错误。补搜`rg` exit1表示无匹配，stderr为空。安装注册记录为CubeMX6.12.0的C:/32、D:/32入口，以及6.13.0的D:/Users/...入口；注册信息不证明对应版本当前正在运行。没有发现其他可搜索的STM32CubeIDE/Programmer安装位置。

**VERIFIED（排除）**：始终排除 `D:/32/STM32H7/h7_mac_sd`、`D:/32/STM32H7/h7_mac`、`D:/32/STM32H7/h7_mac_wk`；没有操作原始工作区。0匹配意味着没有路径/大小/修改时间/SHA256行可列，不能虚构文件。

本机搜索原始清单：`D:/32/STM32H7/h7_mac_lifecycle/output/MSC24/udlm-hardware-evidence/local_search.json` 与 `installation_search.json`。

## 3. 指定官方地址获取结果

目标为RM0399 Rev5、October2026：[用户指定的ST官方URL](https://www.st.com/content/ccc/resource/technical/document/reference_manual/group0/82/40/1a/07/c9/16/40/35/DM00176879/files/DM00176879.pdf/jcr%3Acontent/translations/en.DM00176879.pdf)。

| 检查项 | 结果 |
| -- | -- |
| 下载记录时间 | VERIFIED：2026-11-19 17:28:51.835188 +08:00 |
| 工具 | VERIFIED：Windows curl，保留TLS校验，未绕过登录/证书警告 |
| HTTP / curl exit | VERIFIED：HTTP567 / exit22 |
| 实际下载量 | VERIFIED：0字节 |
| 候选文件 | VERIFIED：未创建；不是可解析PDF |
| PDF magic/首页RM编号 | NOT_TESTED：无文件 |
| Rev5/发布日期/页数/目录/末页与截断核验 | NOT_TESTED：无正文 |
| RM本机路径及SHA256 | UNRESOLVED：未取得，不能填写 |
| 下载记录本机路径 | `D:/32/STM32H7/h7_mac_lifecycle/output/MSC24/udlm-hardware-evidence/official_download_result.json` |

**VERIFIED（过程限制）**：本轮官方下载尝试与尚未结束的本机搜索发生了重叠；该尝试只留下失败记录，并未据此提前认定本机没有手册。最终“0个匹配”来自完成后的搜索与安装目录补搜。官方失败后未继续更换芯片手册或编写寄存器时序。

## 4. RM正文证据目录

以下全部 **UNRESOLVED**；不是对硬件语义的断言。取得原件后才可填具体章节、印刷页码、寄存器位定义及器件/实例映射。

| 待核验资源 | 所需证据 | 本轮状态 |
| -- | -- | -- |
| OTG_FS/OTG_HS与实例、寄存器映射 | USB2_OTG_FS的硬件身份、PIO/FS资源和寄存器有效范围 | 无RM正文，未核验 |
| DIEPCTLx、DOEPCTLx、DIEPINTx、DOEPINTx | EPENA、EPDIS、EPDISD、SNAK、CNAK、NAKSTS、XFRC及EP0特殊条件 | 无RM正文，未核验 |
| DCTL、DSTS、GINTSTS、GINTMSK、DAINT | global OUT NAK、有效中断/掩码、Reset、frame状态和退休 | 无RM正文，未核验 |
| GRSTCTL、GRXSTSP | FIFO选择字段FTXFNUM/TXFNUM命名及Flush前置条件/Pop语义 | 无RM正文，未核验，不擅定字段名 |
| GRXFSIZ、DIEPTXF、GNPTXSTS | 共享RX/各TX FIFO资源、剩余状态、与EP0共用的影响 | 无RM正文，未核验 |

**VERIFIED（源码事实，不是RM证明）**：当前使用USB2_OTG_FS/embedded PHY/FULL speed，USB DMA关闭（`CM7/BSP/bsp_usb.c:99`、`CM7/BSP/bsp_usb.c:105`、`CM7/BSP/bsp_usb.c:108`、`CM7/BSP/bsp_usb.c:111`）。HAL在RXFLVL路径读取GRXSTSP并写OUT buffer（`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1080`）；该源码行为不能代替手册规定的Pop/Flush安全条件。

## 5. Endpoint Hardware Stop Matrix

RM列不能编造页码；当前源码列为本基线的 **VERIFIED（源码）**，差距和实施结论为 **INFERRED / UNRESOLVED**。

| 项目 | RM0399依据 | 当前源码 | 差距 | 实施结论 |
| -- | -- | -- | -- | -- |
| Bulk IN | UNRESOLVED：章节/页码/位语义未核验 | `USB_EPStopXfer`：`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:915`；PIO续包`CM7/BSP/bsp_usb.c:655` | 写SNAK/EPDIS后只轮询EPENA，未形成完整事件/FIFO/PIO证明 | 不以EPENA0或HAL_OK释放source；暂不能批准停止算法 |
| Bulk OUT | UNRESOLVED：global OUT NAK与共享RX条件 | 同一LL函数；guard `CM7/BSP/bsp_usb.c:346` | 当前OUT与IN基本同形；other-RX guard不覆盖完整EP0资源 | 不假定IN/OUT同流程，不随意Flush共享RX |
| ISO IN | UNRESOLVED：PIO专用停止/帧时序 | `CM7/BSP/bsp_usb.c:520`、`CM7/BSP/bsp_usb.c:727` | 不完整通知/failed不能替代取消静止；DMA勘误不是PIO算法 | 不修改UVC，不据DMA限制生成PIO顺序 |
| EP0 | UNRESOLVED：SETUP抢占、状态包、特殊控制位 | `CM7/Platform/usb_device.c:1321`，`CM7/BSP/bsp_usb.c:287` | Close/Open与旧事件退休及共享RX需要完整硬件证明 | 不合并到普通非零EP Abort |
| TX FIFO Flush | UNRESOLVED：选择字段/前置条件/完成时序 | LL `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:452`；HAL wrapper `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:2018` | LL真返回被HAL丢弃；长迭代上限不等于短时间预算 | 接口层面可由BSP直接取LL返回，安全集成尚未证明 |
| RX FIFO Flush | UNRESOLVED：global NAK/共享RX/EP0与Pop | LL `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:489`；BSP `CM7/BSP/bsp_usb.c:411` | 共享scope和新SETUP保护未闭合 | 不额外读取GRXSTSP来“清理”或诊断 |
| IRQ Retirement | UNRESOLVED：匹配事件语义、W1C及Reset | HAL EPDISD `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1144`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1234`；Stop `CM7/BSP/bsp_usb.c:188` | HAL无通用Bulk取消完成回调；BSP先关IRQ，失败直接返回 | 需要具名事件/PIO/所有权证明，不能仅清busy |

**VERIFIED（源码）**：HAL_PCD_EP_Abort直接返回LL结果（`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1992`）；LL轮询耗尽返回HAL_ERROR，不承诺稍后回调；EPDISD分支仅在ISO incomplete条件回调。HAL_EP_Flush丢弃底层返回。Platform在BSP成功后清busy（`CM7/Platform/usb_device.c:426`），现有结构仅opened/busy等字段（`CM7/Platform/usb_device.h:49`），不是完整静止证明。

## 6. 实施边界结论

- **UNRESOLVED**：本轮不能认证任一新的Endpoint停止操作已经安全，也不能提供硬件认可的Buffer释放条件。
- **INFERRED**：停止证明必须覆盖匹配身份、硬件静止、PIO引用、旧事件和所需FIFO处理；USB静止不等于SD IDMA静止。
- **VERIFIED（源码）**：公共LL接口确实返回Flush结果，源码上存在不改HAL/LL即可取得真返回的入口；不代表已经证明锁、scope、时序和时间上界。
- **UNRESOLVED**：BSP的取消/Close/Flush/IRQ顺序是候选修复边界；现阶段不能断言全部问题一定可不改HAL/LL解决，更不能先修改禁止模块。
- **UNRESOLVED（硬阻塞）**：RM0399完整正文仍缺。**尚不具备进入P1-B生产实现的硬件证据条件**。本轮没有开始P1-B/P2。
