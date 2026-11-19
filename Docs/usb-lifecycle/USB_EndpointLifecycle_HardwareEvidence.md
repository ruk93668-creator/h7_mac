# USB Endpoint Lifecycle 硬件证据与阻塞记录

## P1-A Exit Review — 2026-11-19 最终审计

> VERIFIED＝注明来源的手册/源码/现有用户证据事实；INFERRED＝拟议实现或推导；UNRESOLVED＝尚无充分证明；NOT_TESTED＝没有执行对应验证。本节为当前结论，后文各轮状态保留为历史，不据历史“缺RM/未知REV”判断现状。

**VERIFIED（Git）**：本轮开始实时远端HEAD为`c60daae4067a2b0d414b7673d068c9b65500b062`；本地HEAD为`19b35eadef50284dc006a38f23c411c45cee9039`，仅领先上一轮四份文档提交，没有源码差异。保留该提交历史，在其上追加本次审计，最终正常fast-forward推送；没有reset/stash/clean。`Docs/references/RM0399_Rev5.pdf`为此前用户PDF，保留本机，不暂存。禁止工程目录未操作。

**VERIFIED（PDF/用户证据）**：RM0399 Rev5 / October2026 /3535页，SHA256=`61d07b67deb291aed8a5cf8019c94add0a1065318542e57b3287d2694b14bd60`，本轮重算一致。实际IDCODE=`0x20036450`，DEV_ID=`0x450`，REV_ID=`0x2003`，只定位U/V组；一次截图、两次刷新一致为用户陈述。USB2_OTG_FS为OTG_HS2别名（RM2747页图794），配置为FS/embedded PHY/USB DMA关闭（`CM7/BSP/bsp_usb.c:99`、`:105`、`:108`、`:111`）。

### 最终审核矩阵

表中“P1实施方案”全部为**INFERRED**的后续候选，本轮没有实现；源码事实为**VERIFIED**，当前完整安全性除特别说明外为**UNRESOLVED**，新实现/板级验证均**NOT_TESTED**。

| 功能 | 手册依据 | 当前实现 | 安全性结论 | P1实施方案 |
| -- | -- | -- | -- | -- |
| Bulk IN停止 | RM60.15.6，2917～2918；60.14.55/56，2840～2843 | LL `USB_EPStopXfer`只SNAK/EPDIS/EPENA轮询；BSP PIO续包 | VERIFIED：普通禁用需INEPNE、EPDISD及TX处理；UNRESOLVED：当前无完整Proof | fence写入→NAK有效→禁用→身份匹配终态→真实TX Flush；先处理正常完成竞争 |
| Bulk OUT停止 | RM60.15.6，2905、2909～2912；60.14.65，2852 | 无Global OUT NAK准备；单EP Abort可能Flush共享RX | VERIFIED：GONAKEFF依赖RX消费；UNRESOLVED：共享RX/EP0保护 | Task分步准备、保留单一RX消费者；普通单EP禁用不默认Flush整个RX |
| ISO IN停止 | RM60.15.6，2918、2921～2923 | PIO逐包/ISO incomplete回调，无身份Proof | VERIFIED：incomplete专用流程；UNRESOLVED：主动流停止/Frame Lease | 按原因选择普通IN或incomplete路径；fence帧提交/续包，禁用后定向Flush再归还引用 |
| EP0取消 | RM60.14.61，2846～2847；60.15.6，2903～2908 | 新SETUP Close/Open；EP0 Close调用通用HAL Abort | VERIFIED：EP0 OUT不可EPDIS；UNRESOLVED：当前控制事务取消证明 | 独立EP0控制/SETUP策略；Token资格与硬件引用分离，失败否决状态包；不套普通OUT流程 |
| TX FIFO Flush | RM60.14.5，2784；60.15.6，2918 | HAL wrapper与IRQ丢LL真实结果 | VERIFIED：HAL_OK不足；UNRESOLVED：时间/前置条件集成 | BSP保存LL真返回和NAK/AHB/PIO条件，定向TXFNUM，超时不发布成功Proof |
| RX FIFO Flush | RM60.14.5，2784；60.15.6，2905、2910～2911 | 共用RX；guard未覆盖所有EP0状态 | VERIFIED：全局破坏性scope；UNRESOLVED：并发SETUP安全 | 单EP优先排空而非Flush；Reset特殊流程单独设计，限次重试，不丢他人包 |
| EPDISD/XFRC退休 | RM60.14.45，2832；60.14.56/62，2842～2843、2848～2849 | HAL先清事件，普通Bulk无取消完成callback | VERIFIED：两种事件不同；UNRESOLVED：匹配身份与不重复释放 | HAL前/后保存必要证据，单终态裁决，旧Session只收尾旧资源 |
| IRQ/PIO并发 | RM60.14.3，2780；60.15.6，2905、2909、2918 | Platform关IRQ包Abort；BSP Stop先关IRQ | VERIFIED：mask不冻结core；UNRESOLVED：等待RX消费时的串行化 | 短临界区发布fence；Task多轮准备，保留RX/必要IRQ；不在ISR长轮询 |
| Buffer释放 | RM60.14.56/62，2843、2849；60.15.6，2918、2923 | busy/active布尔，HAL_OK后清引用 | VERIFIED：寄存器不能证明软件ledger；UNRESOLVED：完整所有权 | 硬件终态+PIO引用零+事件退休+FIFO条件+唯一身份；SD IDMA独立Proof |

### 退出判定

**VERIFIED（审计交付）**：九项均给出正文、寄存器/位、前置与顺序、完成/失败及真实源码差距，详[逐项停止契约](RM0399_Revision_and_EndpointTiming_Analysis.md)。RM原件、实例映射与数值修订组证据已齐备。

**UNRESOLVED（安全退出）**：EP0专用取消与Reset范围、主动ISO停止选择、RX消费者和HAL自动NAK/Flush副作用、有界时间与事件身份尚未形成可实施且经验证的完整契约。不能将“证据审计已交付”写成“所有停止算法安全/P1-A无条件PASS”。留人工审核决策，不进入P1-B。

**NOT_TESTED**：本轮不编译、不改测试、不做故障注入、不烧录；此前软件刻画测试不升级为硬件证明。无新板级重连/Stop/Buffer释放结果。

---

## 历史证据记录（以下非当前退出判定）

## 2026-11-19 RM0399正文补齐（最新状态）

**VERIFIED（本机PDF）**：已取得用户浏览器下载的RM0399 Rev5 / October2026 /3535页；指定路径`D:/32/STM32H7/h7_mac_lifecycle/Docs/references/RM0399_Rev5.pdf`，72,029,379字节，SHA256=`61d07b67deb291aed8a5cf8019c94add0a1065318542e57b3287d2694b14bd60`。所有页可解析且页脚匹配，EOF存在，解析器未修复。浏览器来源为用户陈述，未独立核对ST服务器哈希。

**VERIFIED（正文）**：2747页图794确认USB2 OTG_HS/OTG_HS2别名OTG_FS；普通Bulk IN的PIO停止、NAK生效与EPDISD在2917～2918页；普通Bulk OUT全局NAK/RX pop及禁用在2909～2910页；incomplete ISO IN在2923页；EP0 OUT禁止EPDIS在2846页；FIFO Flush前置条件及TXFNUM在2784页。手册正文缺失/实例映射已关闭，但不能把Reset型Transfer Stop与普通Endpoint Disable混用。

具体章节、页码、短原文、位定义、六份源码差距和七行Stop Matrix见[RM0399正文核验](RM0399_Revision_and_EndpointTiming_Analysis.md)的A～I节；完整178页提取件及validation.json留在`D:/32/STM32H7/h7_mac_lifecycle/output/MSC24/udlm-rm0399/`。

**UNRESOLVED**：IRQ/PIO串行化、共享RX/EP0隔离、HAL消费前事件证据、有界Flush、旧身份退休及主动ISO停止算法仍需设计/实现验证。**NOT_TESTED**：实板停止与重连。本轮只改文档，没有开始P1-B/P2。下面“缺RM正文”均为先前历史，不是当前结论。

## 2026-11-19 本轮证据补充（优先于下方历史记录）

**VERIFIED（用户截图/陈述）**：DBGMCU_IDCODE完整值`0x20036450`，DEV_ID=`0x450`，REV_ID=`0x2003`；一次截图加两次刷新相同的用户确认。ES0445 Rev7第1页表2映射到U/V组，不能唯一确定U或V。探针为DAPLink；精确Keil版本、CPU状态、完整实物型号仍UNRESOLVED。用户报告曾自动Reset/Download，不能宣称本次满足全程无复位/无下载要求。代理没有操作目标。

**UNRESOLVED**：RM0399本机搜索0匹配，指定官方请求HTTP567/0字节，未得到PDF原件。没有PDF页数或SHA256，不提供未经正文核验的停止步骤；P1-A整体未完成，P1-B/P2未开始。

详见[修订号证据及过程限制](STM32H747_SiliconRevision_Identification.md)、[RM0399搜索与停止时序矩阵](RM0399_Revision_and_EndpointTiming_Analysis.md)。下方“未实读REV_ID/未做实板读取”仅描述先前P1交付，不再代表本次用户提供证据后的最新状态。此前软件测试结论保留，不扩大为实板停止证明。

> P1 起点：`9d38e56966f8ea5da9ee63285b037747613c47d5`，分支 `codex/usb-device-lifecycle-manager`，2026-11-19。
> VERIFIED 分别注明手册/源码/软件测试；INFERRED 是设计推导；UNRESOLVED 是缺证据；NOT_TESTED 是未做实板验证。
> **P1-A 尚未完成。不能据本文实施未核验的寄存器停止时序，也不能宣布 P1 完整通过。**

## 1. 文档与器件身份

| 项目 | 本次证据 | 状态与限制 |
| -- | -- | -- |
| 官方 RM0399 | ST 官方文档目录检索显示 Rev 5 / October 2026；最新入口 DM00176879；正文未取得 | VERIFIED：仅版本索引；UNRESOLVED：版本原件、哈希、USB 正文章节/页码 |
| 较旧 RM0399 | 官方历史入口/公开预览标 Rev 4 / June 2023 / 3556页；预览能定位 USB 章节目录 | VERIFIED：看到版本与目录；不是停止时序正文的证明，更不能代替 Rev5 差异核验 |
| 官方 ES0445 | 实际打开官方 PDF 为 Rev 7 / October 2026 /60页，非搜索缓存中的 Rev6 | VERIFIED：官方 PDF 正文，适用性表1/表2，第1页；USB 摘要表3第5页；2.24节第43–44页 |
| 工程目标型号 | `MDK-ARM/h7_mac.uvprojx:3098` 为 STM32H747XIHx:CM7，CM4目标同系列 | VERIFIED：工程配置；UNRESOLVED：实物完整丝印/订货后缀，不能把配置等同板上型号 |
| DEV_ID | CMSIS `Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:25057` 定义0x450 | VERIFIED：预期系列定义，未实读 |
| REV_ID | 未获板上 DBGMCU_IDCODE 原值，且本轮禁止实板烧录/物理测试 | NOT_TESTED：未连接调试器读取；UNRESOLVED：实物修订适用性 |
| 当前 USB | BSP 为 USB2_OTG_FS、embedded PHY、FULL speed、dma_enable DISABLE | VERIFIED：`CM7/BSP/bsp_usb.c:99`、`CM7/BSP/bsp_usb.c:105`、`CM7/BSP/bsp_usb.c:108`、`CM7/BSP/bsp_usb.c:111`；具体 OTG_HS Errata 到此实例的硬件映射仍需 RM 正文 |

来源：[ST RM0399 官方入口](https://www.st.com/resource/en/reference_manual/dm00176879.pdf)、[ST ES0445 Rev7 官方PDF](https://www.st.com/resource/en/errata_sheet/es0445-stm32h745xig-stm32h755xi-stm32h747xig-stm32h757xi-device-errata-stmicroelectronics.pdf)。引用是目标来源链接，不表示 RM 正文已取得。

**VERIFIED（访问记录）**：大 PDF 经浏览工具读取返回文件大小超限（旧入口约22,321,253字节；其他入口超过10MiB读取上限）；本机官方域请求返回HTTP567或超时，部分镜像出现证书链验证失败。尝试使用已有系统受信根仍未取得正文，没有关闭 TLS 校验。官方中文资料详情转登录，未尝试绕过登录。用户明确回复本机没有 RM0399。公开手册预览仅能看到 USB 目录，不能作为正文时序证据。

**UNRESOLVED**：没有 RM0399 原文件，不能记录其 PDF SHA256 或编造相应停止步骤的页码。本轮没有提交第三方转载 PDF，也没有用其他 STM32 系列的手册补位。

## 2. Revision 与 Errata 核对结果

**VERIFIED（手册，ES0445 Rev7 表2，第1页）**：U/V 对应 REV_ID 0x2003；T/4/5/6 对应0x2007。表3覆盖这些修订。**UNRESOLVED**：尚不知道板卡属于哪一组；HAL头文件中 REV_ID_V 常量不是实读依据。

**VERIFIED（源码）**：DBGMCU 地址为0x5C001000（`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:2542`），DEV_ID 是低12位、REV_ID 是高16位（同文件 `Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:25060`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:25063`）；`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c:466` 返回右移16位的值。未来只读采集时保存完整32位原值、解码、型号/丝印和工具信息，不能只记“V1板”。

以下是 **VERIFIED（手册事实）** 的简要风险索引，应用到当前板卡仍 **UNRESOLVED**：

| ES0445项目 | 正文位置 | 对本任务的意义（INFERRED） |
| -- | -- | -- |
| 2.24.2 ISO IN disable期间仍有DMA取包 | 第43页 | 适用条件是Buffer DMA；当前USB PIO不能直接照搬该DMA workaround，也不能据此推导通用PIO disable顺序 |
| 2.24.3 隐藏charger状态中断掩码问题 | 第43页 | IRQ退休不能仅按直觉清无关位；需核对实际使能与控制器模式 |
| 2.24.4 Reset后的虚假CIDSCHG | 第43–44页 | 不能将每个连接相关事件都当成真实连接变化 |
| 2.24.5 零长度IN包可能异常 | 第44页 | 文档同时涵盖DMA与slave模式，EP0状态ZLP也需专门核对；当前源码无明确引用该勘误的实现证明 |
| 2.24.1/6/7 host/hub/chirp/ULPI相关项 | 第43–44页 | 记录但不能套用到当前device/embedded-PHY故障作为根因 |

**NOT_TESTED**：无勘误触发/规避实板实验，无实物修订读取；没有更改 ZLP、HAL、LL 或 MSC/UVC 协议。

## 3. 寄存器位定义证据（不是停止时序证明）

全部为 **VERIFIED（源码定义）**，依据 `Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h`：

| 寄存器/位 | 位号 | 定义位置 |
| -- | -- | -- |
| DIEPCTL EPENA / EPDIS | 31 /30 | `Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29349`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29346` |
| DOEPCTL EPENA / EPDIS |31 /30| `Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29638`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29635` |
| IN NAKSTS / CNAK / SNAK |17 /26 /27| `Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29314`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29334`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29337` |
| OUT NAKSTS / CNAK / SNAK |17 /26 /27| `Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29609`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29629`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29632` |
| IN XFRC / EPDISD / INEPNE |0 /1 /6| `Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29476`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29479`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29494` |
| OUT XFRC / EPDISD |0 /1| `Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29643`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29646` |
| DCTL SGONAK / CGONAK |9 /10| `Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:28456`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:28459` |
| GINTSTS BOUTNAKEFF / RXFLVL / USBRST |7 /4 /12| `Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:28742`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:28733`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:28751` |
| GRSTCTL RXFFLSH / TXFFLSH / AHBIDL |4 /5 /31| `Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:28600`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:28603`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:28618` |

**VERIFIED（源码）**：HAL 使用 DAINT/掩码选择端点、读取 DIEPINT/DOEPINT，RXFLVL读取 GRXSTSP 弹出共享状态并向 HAL OUT buffer 写入（`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1080`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1117`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1187`）。诊断不应额外读取 GRXSTSP“看一下”。

## 4. 四类端点停止条件的当前结论

| 类型 | 已确认源码事实（VERIFIED） | 必须补齐的硬件条件（UNRESOLVED） | 本轮实施决定 |
| -- | -- | -- | -- |
| Bulk IN | BSP持有原source并在IRQ逐包复制/续传；LL Abort只等EPENA | NAK effective、EPDIS/EPDISD与XFRC正常完成竞争；TX FIFO和pending IRQ退休 | 不写未经RM核验的新停止序列；不能只用HAL_OK/free source |
| Bulk OUT | HAL RXFLVL直接写OUT buffer；RX FIFO共享，当前guard只扫描非零OUT | global OUT NAK的生效与释放、EP0和其他OUT隔离、RX数据/事件退休 | 不弹出GRXSTSP、不随意共享RX Flush |
| ISO IN | BSP一次只提交至多一包；incomplete标failed，再由Task处理 | PIO模式的专用disable/NAK/flush顺序、frame parity、迟到XFRC/EPDISD | DMA勘误不代替PIO证明；不修改UVC Payload |
| EP0 | 当前新SETUP对busy EP0 Close/Open；Core控制状态有独立Token | EP0 IN/OUT特殊位语义、SETUP抢占、status ZLP、RX FIFO共用及BusReset | 不纳入通用nonzero Abort；不改变Core标准请求/协议 |

**UNRESOLVED**：上述硬件结论的 RM 正文章节和页码暂时不能填写。Rev4公开目录给出60章相关寄存器位置，但没有正文不能升级为VERIFIED。本文不宣称EPENA0、EPDISD或XFRC任一单独事实足够释放缓冲。

## 5. 公开LL FIFO接口是否可用

**VERIFIED（源码）**：`USB_FlushRxFifo`/`USB_FlushTxFifo` 在 `Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_usb.h:506`、`Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_usb.h:507` 是公开接口；实现位于 `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:452`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:489`，返回真实HAL_TIMEOUT/OK。HAL wrapper在 `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:2018` 丢弃返回值。

**VERIFIED（软件测试）**：`Tests/EndpointLifecycle/reports/hal_contract.json` 的18项刻画测试/108条断言重现了wrapper隐藏失败、LL真实返回、EPENA零但无EPDISD仍返回OK、旧INT未退休等事实。测试中仅把HAL_USB_TIMEOUT设为4以便故障模拟，生产值不变；这不是时间标定。

**INFERRED**：BSP将来可直接调用公开LL取得真实返回，不能因此声称整个停止契约已成立。必须另行证明：PCD锁/IRQ和PIO排他、共享RX安全scope、硬件停止先于Flush、有限时间预算和W1C事件处理。

**UNRESOLVED**：生产LL使用0xF000000迭代上限（`Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_usb.h:41`），不是经测量的短微秒预算。不修改LL源码时，该同步调用不能被外层deadline中断；直接调用后再检查时间不是“超时前一定返回”。未经RM核验也不能复制一套新的FIFO寄存器轮询。因此真实返回接口可用，但本轮仍无法证明可安全接入有界停止。

## 6. 本轮硬件证据门禁

**UNRESOLVED / 阻塞 P1 完整验收**：RM0399正文/修订差异、实际硅片型号/REV_ID、四类端点停止条件、公共LL调用的正确锁/IRQ范围及时间预算。缺这些前置条件，不能实现并签发EndpointQuiescenceProof，不能修正生产停止路径后宣称安全。

**NOT_TESTED（所有板级项目）**：未烧录、未读实板IDCODE、未插拔、未抓新USBPcap、未验证真实EPDISD/FIFO/重连/SD IDMA。USB端点证明与SD IDMA证明继续独立，RAW租约/FatFS规则没有变化。
