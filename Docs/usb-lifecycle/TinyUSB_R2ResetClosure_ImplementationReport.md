# TinyUSB R2-ResetClosure — 实施报告

**VERIFIED（软件）**：活动 Bulk IN/OUT 的 Reset 专用停止、共享 FIFO 退休、原生 Core/Class Reset 和重新接收 CBW 已在完整 ARMCC5 AXF 中闭环。**NOT_TESTED（实板）**：未烧录、未执行 USBPcap 或物理复位；本报告不是硬件验收通过声明。

## 基线与修改范围

- VERIFIED：基线 `5e640632e3d6f7d0a0a6c7e8dfe58b6fc798a1e3`；分支 `codex/usb-tinyusb-p1-replacement`；工作区 `D:/32/STM32H7/h7_mac_lifecycle`。
- VERIFIED：本轮生产逻辑仅修改 `ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c`、`src/device/usbd.c`、`src/device/dcd.h`。另更新上游差异说明、独立实验项目生成输出路径、直接对应测试/证据和本报告系列。
- VERIFIED：旧 UDLM、ST Core、HAL/LL、MSC 协议源码、RAM Backend、Camera/Media、SD/FatFS、Task、默认 Keil/CM4/Scatter 没有本轮变更。没有新增 Task、控制器拥有者、Registry、StopAttempt 或 Resource Ledger。
- VERIFIED：本地 RM PDF 保持未跟踪且 SHA256 不变；已存在的两项未跟踪 Keil/RTE 文件保留且不暂存。所有 output 和失败历史保留。

## Reset 专用实现

| 步骤 | 实现和依据 | 结论 |
| -- | -- | -- |
| 接收 USBRST | `ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1330`；检查没有普通取消/组关闭/外部 NAK owner，分配非回绕 Reset generation；发布 PIO/提交 fence | VERIFIED（源码/模型）；USBRST 本身不证明硬件静止 |
| 旧控制资格作废 | 原生 START 事件携带 generation；Core 增加原生 SETUP 序号，保留旧 Class/BUSY | VERIFIED；不提前 Class reset |
| Reset RX Flush | 仅在 USBRST 后且 ENUMDNE 尚未观察到的窗口，全 OUT 置 EPENA、AHBIDL、RMW RXFFLSH；最多重试一次 | VERIFIED（RM/源码）；时窗提前结束即 FAILED |
| Global OUT NAK | 使用原 DCD 的唯一 `out_nak_owner=-2`；必须由唯一 RX 消费者 pop NAK marker，再观察 GONAKEFF | VERIFIED（模型）；不另建 FIFO 消费者 |
| OUT 停止 | 非零 OUT 请求 EPDIS/SNAK，记录匹配 EPDISD 后 W1C，同时检查 EPENA 已清 | VERIFIED（模型）；OUT0 永不写 EPDIS |
| IN 停止 | Reset 型 EPDIS→EPDISD→无 PIO producer→AHBIDL→全 TX FIFO Flush | VERIFIED（RM/模型）；不把普通取消的 INEPNE 等待硬套到 Reset |
| FIFO/事件退休 | RX 状态由原 IRQ 路径丢弃并消费；所有旧 IN/非零 OUT 停止，TXFFLSH 实际清零后释放 NAK；不得任意清旧 Buffer | VERIFIED（模型） |
| 准备新 EP0 | `ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1281`；保留既有 FIFO 布局、地址置零、FS64 MPS、STUPCNT=3；重置数据资格，不 disable OUT0 | VERIFIED（源码/模型）；限本实验 FS64 配置 |
| 发布原生 Reset END | 在短锁内将带 generation 的 END 排入原队列，随后退出 DCD pending；Task 执行原生 Class Reset | VERIFIED（模型）；没有在 ISR 调 Class/Backend |
| 新配置与 CBW | 原 TinyUSB SETUP/MSC 路径；pending_io 原生保留，真正完成后才能再次复用 Backend 缓冲区 | VERIFIED（模型）；不修改 BOT/SCSI |

**INFERRED（组合证明）**：PIO 模式下 OUT0 无需也不能按普通 OUT 禁用。停止旧目标写入的证据来自：Reset fence 禁止旧 copy、Reset 窗口内共享 RX Flush 完成、其余 RX 状态唯一消费、非零 OUT 禁用、旧事件作废、fresh SETUP_RX/DONE 资格和 EP0 OUT 实际 arm 标记。它不等同于“OUT0 硬件停止”，也不适用于 USB DMA。

## IRQ、Task 与状态提交

- VERIFIED：`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1911` 只做单次命令发布/事件事实记录和最多 8 条 FIFO 状态消费；等待在 `ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1381` 中分阶段检查，没有 ISR 轮询硬件完成。
- VERIFIED：RXFFLSH 期间仅屏蔽 RXFLVL 消费；USB IRQ、Reset/ENUM、端点完成推进仍可工作。失败不关闭整个 OTG IRQ。
- VERIFIED：`reset_progress` 和跨 IRQ Reset 身份为 volatile；事件事实在 W1C 前记录。Task 的 MMIO 命令和最终 proof 发布使用既有 spin/BASEPRI 短窗口。
- VERIFIED：Core arm 的最终 Reset 资格检查、BUSY 发布、DCD 提交与拒绝后的清理处于同一短临界区。实际 ARM 抢占点测试在进入锁前插入真实 OTG Reset handler，恢复 Task 后提交被拒绝，旧 OUT 仍归原请求持有。
- VERIFIED：Claim/Release 先获取原 Mutex、再进入短锁；不在 USB IRQ 被屏蔽时等待 Mutex。Pending Clear HALT 不清 BUSY，Pending Abort 不伪造硬件失败或成功。
- VERIFIED：组关闭的 Class reset 回调移到短锁外；状态/控制序号及 Status 提交仍使用原短锁。Reset 的原生 Class reset 也在 Task 且 BASEPRI=0。RAM MSC open/configure 的已有有界同步回调保留；不宣称任意未来 Class 回调均适合锁内执行。

## 构建与交付物

VERIFIED：ARM Compiler 5.06 update 7 build 960；完整 Keil TinyUSB_RAM_CM7 重编 **0 Error、0 Warning**。Code=51338、RO=1086、RW=324、ZI=25836 字节。

- AXF/MAP/HEX/build.log：`D:\32\STM32H7\h7_mac_lifecycle\output\TinyUSBReplacement\R2ResetClosure-review-28b17dae3c26`。
- AXF SHA256：`28b17dae3c265752db075c2c93b6648d33414c14e6694685cd1d5c6433132c9b`。
- 精确文件哈希、最终符号/唯一 IRQ 向量：`Tests/TinyUSBReplacement/reports/R2ResetClosure_receipt.json`。
- 本轮生产 Diff：`Tests/TinyUSBReplacement/reports/R2ResetClosure_production.patch`。
- 相对固定 TinyUSB 上游的累计 Diff：`Tests/TinyUSBReplacement/reports/R2ResetClosure_upstream.patch`。
- 调用者：`Tests/TinyUSBReplacement/reports/R2ResetClosure_callers.json`。

VERIFIED：构建产物保留本地 output，不加入 Git；通过 receipt 精确关联本轮源码、测试和固件。提交后普通推送并使用 ls-remote 核对；完整 Git SHA 以最终交付消息为准，避免报告引用自身提交号。

## 验收结论

VERIFIED（软件）：Reset 专项 **31/31、574 断言**，R1B **33/33、313**，MSC **12/12、1679**，等待边界 **9/9**。旧 Closure 为 **24/25**，旧 R2 为 **17/19**；原断言没有修改，详见软件报告的契约分类，不能宣称“全历史测试全绿”。旧 R1 两项失败继续保留。

NOT_TESTED：硅片 Reset 窗口/UsbTask 调度、EPDISD/NAK/PIO 总线时序、Windows 枚举、ES0445 PIO ZLP 勘误仍需实板验证。完成软件交付后停止等待审核，不自动烧录。
