# TinyUSB R1B 实施报告

日期：2026-11-21。基线：`5ba9e0e6444bf6134e7fdb3d4cb89b3b2d69e129`。
分支：`codex/usb-tinyusb-p1-replacement`。

**VERIFIED（软件）：非零线性 Bulk IN/OUT 取消已有真实 SUCCESS 路径，故障可返回 FAILED，PENDING 不释放源或提前提交 MSC Reset Status。整体生产启用仍 BLOCKED，不能替代实板验收。**

## 修改边界与证据

VERIFIED：生产修改仅限 TinyUSB 的八个文件：`src/device/{dcd.h,usbd.c,usbd_pvt.h}`、`src/class/msc/msc_device.{c,h}`、`src/portable/synopsys/dwc2/{dcd_dwc2.c,dwc2_common.c,dwc2_common.h}`。路径前缀均为 `ThirdParty/tinyusb/`。另更新 UPSTREAM.json 的本地补丁说明。未引入旧 UDLM、额外 Task、动态生命周期管理器、StopAttempt 或 Ledger；复用现有 DCD IRQ、事件队列和 tud_task。

- VERIFIED：[相对 R1 基线的实际八文件生产 Diff](../../Tests/TinyUSBReplacement/reports/R1B_production.patch)。
- VERIFIED：[相对固定上游的累计 Diff](../../Tests/TinyUSBReplacement/reports/R1B_upstream.patch)，包括既有 video.h 单字段兼容补丁；该字段本轮未修改。
- VERIFIED：[源码哈希与旧测试保留清单](../../Tests/TinyUSBReplacement/reports/R1B_receipt.json)。固定上游仍为 `da5b55a5803a3b9ca788ccc82569db699c01aa74`，52 个选取文件的原始 Git blob 已重新核对。
- VERIFIED：CM7、CM4、HAL/LL/ST Core、Keil、SD/FatFS、旧 UDLM 生产模块没有 Diff。参考 PDF 未改动、未暂存，SHA256 仍为 `61d07b67deb291aed8a5cf8019c94add0a1065318542e57b3287d2694b14bd60`。

## DCD 取消契约

源码依据：`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:511` 的 `dcd_edpt_abort_safe()`。

| 结果 | 已实施语义 | Core 行为 |
| --- | --- | --- |
| UNSUPPORTED | VERIFIED：未发起取消；未支持的类型/模式无寄存器写入 | 仅这一结果可使用原兼容回退，并检查回退后的真实 fault |
| PENDING | VERIFIED：取消已开始，保存方向、generation、原 source 与阶段 | 保留 BUSY/CLAIMED，不允许重 Arm；可在原任务继续调用 |
| SUCCESS | VERIFIED：所需硬件事实已被当前取消路径消费 | 才清 BUSY/CLAIMED、归还旧 source |
| FAILED | VERIFIED：缺证据、时基失效、Reset/Disconnect 或硬件超时 | 锁存首个故障、保留阶段/身份/source，禁止回退和新会话 |

VERIFIED：PENDING 枚举追加在旧三值之后，不改变旧三值。弱缺省 DCD 查询返回 false/0，其他 DCD 的原布尔 abort 由弱兼容包装处理。新结构改变 TinyUSB 内部事件布局，全部相关对象须重编；不支持混用旧二进制。

VERIFIED：当前成功资格限定 STM32H747 rhport0、Full-Speed Slave/PIO、非零 Bulk、线性 buffer。FIFO-ring 接口失败保留；DMA/非 Bulk 没有获准的异步成功路径。Transfer generation 不随正常 DeInit/Init 重置，溢出拒绝新提交。底层事件在 Task 和允许的 ISR 回调释放状态前均核验 generation。

### Bulk OUT

VERIFIED（源码/ARM 模型）：提交 Fence → 保存旧身份/source → 唯一 Global OUT NAK owner → SGONAK → 唯一 RXFLVL pop 路径消费 Global NAK marker，结合 BOUTNAKEFF → SNAK/EPDIS → 在 W1C 前记录 EPDISD/XFRC → 目标 EPENA=0、RXFLVL 空、PIO 引用归还 → CGONAK → 确认 GONSTS/BOUTNAKEFF 解除 → 退休。

VERIFIED：旧 RX payload 经真实 PIO 消费路径丢弃，不再写旧 buffer；GRXSTSP 每条仅 pop 一次，copy 前检查剩余长度。取消不 Flush 共享 RX FIFO。已有外部 NAK 或未解除 NAK 不被假冒为本事务所有权。

### Bulk IN

VERIFIED（源码/ARM 模型）：关闭目标 TXFE 续包 → SNAK → INEPNE 或已观测的正常 XFRC 且 EPENA=0 → EPDIS → EPDISD/EPENA 证据 → 目标 TXFFLSH → 硬件位清除 → 退休。`epin_write_tx_fifo()`（同文件1242行）和 IN IRQ 均阻止目标旧 source 续包。

VERIFIED：共享 GRSTCTL 命令在途时，其他 IN PIO producer 也暂停 TX FIFO 写入并保留 source；TXFE 仅在命令完成后恢复。专项实际提交 EP0 IN，证明 Flush 在途没有 FIFO0 写入，完成后实际 IRQ 恢复 PIO。Reset/enum/unplug 在 Poll 及提交退休前再次检查，不把被 Reset 清掉的 Flush 位当成本取消完成。

### 有界行为与时基

VERIFIED：异步取消每次 Poll 仅推进有限步骤，不在 ISR 等待；要求调试/平台已启用自由运行的 DWT CYCCNT 和有效稳定 SystemCoreClock。取消期限为50ms CPU cycles，保存起点并使用无符号差处理回绕。缺少、停止或变化的时基直接 FAILED，不命名为硬件超时，也不自行启用 DWT。

VERIFIED：`dwc2_common.c:23` 的 `dwc2_wait_bits()` 替代旧同步无界等待：Task 最多2ms DWT deadline，ISR 条件未立即满足则拒绝；所有受影响 Device 调用者检查结果。CSRST/AHBIDL 与 TX/RX Flush 均返回真实 bool；H7 Core Reset 后还核验至少三个48MHz FS PHY clocks 的延迟。模拟器 instruction budget 只作测试 watchdog，不是物理超时证明。

UNRESOLVED：DWT 在实际系统中持续运行、SystemCoreClock 正确、任务按期推进仍需独立集成/硬件证明。若 Task 永不再调用，软件无法主动上报50ms期限；Fence 和 Buffer 所有权仍保留。

## Core / MSC 集成和全部调用者

VERIFIED：`usbd.c:1951` 的 abort 仅在 SUCCESS 退休，PENDING/FAILED 均不回退。提交与 generation 发布在原 TinyUSB spin lock 内串行化；PENDING 会使已排队旧 completion 失去释放资格。Clear HALT、Release、DeInit/Init/connect 均有对应门禁；丢失完成事件时保留 fault 而非清 BUSY。

VERIFIED：`msc_device.c:440` 的 `proc_bot_reset()` 及479行 `mscd_task()` 在现有 `tud_task_ext()` 内继续推进。保存原 SETUP 序号，请求被新 SETUP/Reset 替代后不能补发旧 Status。完成 DCD 取消才重新提交 CBW；仅原控制事务仍合格时提交 Status ZLP。Status 提交不代表主机 ACK。

VERIFIED：[全部选取 TinyUSB 源码引用表](../../Tests/TinyUSBReplacement/reports/R1B_callers.json)包括声明、定义、实际引用及禁用的 Host 引用。

| 调用路径 | 本轮处理 | 限制 |
| --- | --- | --- |
| usbd_edpt_abort → dcd_edpt_abort_safe | VERIFIED：四态、保留资源、仅 UNSUPPORTED 回退 | SUCCESS 限上述 Bulk 资格 |
| dcd_edpt_stall / edpt_disable | VERIFIED：同步等待有界、结果传给 Core fault 门禁 | 非零 OUT legacy void stall 保守拒绝，未实现共享 RX 组退休 |
| dcd_edpt_close_all / SET_CONFIGURATION 变更 | VERIFIED：保守 fault，Core 不继续配置重置 | BLOCKED：不能宣称取消配置或换配置正常成功 |
| ISO activate / close | VERIFIED：返回/消费停止失败，不继续激活 | NOT_TESTED：活跃 ISO 正常恢复；未移植 UVC |
| EP0 新 SETUP 禁用活跃 IN | VERIFIED：ISR 不长等，无法证明即保留故障 | BLOCKED：未实现完整异步 EP0 恢复；OUT0 不走普通 OUT Disable |
| dcd_deinit → dwc2_core_deinit → reset_core | VERIFIED：真实 Reset 失败不清旧引用；PENDING 不先关 IRQ | 非取消中 DeInit 失败可能已断开 pullup/关闭IRQ，保留终态不假恢复 |
| MSC BOT Reset | VERIFIED：既有 Task 推进，不提前 Status；失败保留 source | 与 Bus Reset 区分；SD IDMA 未接入，不能据 USB SUCCESS 释放 SD buffer |
| Reset/Disconnect 与正在取消竞争 | VERIFIED：锁存失败，不重新开放会话 | NOT_TESTED：实板重枚举、无故障自动恢复 |
| Host hcd_deinit → dwc2_core_deinit | VERIFIED：文本检索发现仍忽略新 bool，Host 未启用 | BLOCKED：启用 Host 前必须另行补齐传播，不声称全部 TinyUSB 平台合格 |

## 交付判定

VERIFIED：本轮 Bulk 软件重点达到正向 SUCCESS、异常有界 FAILED、PENDING 保留 Buffer/Status 的目标。三配置组件编译有0 Error、57历史 Warning；不等于完整 CM7 固件构建。NOT_TESTED：全部实板、USBPcap、SD/UVC完整移植。整体生产启用仍 BLOCKED，剩余项见 RemainingRisks；本轮不烧录、不合并 main，停止等待人工审核。
