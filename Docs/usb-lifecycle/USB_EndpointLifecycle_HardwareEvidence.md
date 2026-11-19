# USB Endpoint Lifecycle 硬件证据与阻塞记录

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
