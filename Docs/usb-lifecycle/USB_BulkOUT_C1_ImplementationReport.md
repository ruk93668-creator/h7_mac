# P1-C1 — Bulk OUT 实施报告

VERIFIED：起点 `3305d4a729e965247b33c95e5558eac576f1c6ef`，分支 `codex/usb-device-lifecycle-manager`，工作区 `D:/32/STM32H7/h7_mac_lifecycle`。开始前本地和实时远端一致，仅已批准的 RM0399 PDF 未跟踪；未操作原 MSC 工作区。仅修改6个授权生产文件及直接对应测试/文档，不修改 LL、Core、Class、Mode、Task、SDMMC、FatFS、Keil 等禁止模块。

## 交付与调用链

| 模块 | 本轮改动 | 保留内容 |
| --- | --- | --- |
| HAL PCD .c/.h | 静态完整 Hook 表绑定；真实 RX pop/copy 检查；终止 W1C 前后事实；NAK/ISO委托；Reset/Disconnect 前失效 | 原函数/API、LL、DMA及未绑定分支；不复制或重写 Core |
| BSP .c/.h | 独立 OUT ledger/identity、Global NAK lease、Begin/Poll/Commit/Reconcile；Buffer范围检查；deferred普通完成；失败保留 | 原 IN 机制和同步接口；新 OUT owner 禁止旧接口绕过；不自动控制 Storage |
| Platform .c/.h | 显式 Enable；类型化停止事务；提交结果与 proof 分离；Commit后清busy；故障否决控制资格与破坏性回收 | 原单Class注册、Task、协议；没有自动开启 C1 或迁移 BOT |

VERIFIED：软件调用链为 `USB_Device_EnableBulkOutLifecycle → BSP_USB_EnableBulkOutLifecycle → HAL_PCD_SetLifecycleHooks`；生产应用尚未调用 Enable。已显式启用的真实集成夹具通过 `USB_Device_Receive → BSP_USB_Receive → Usb_OutArm → HAL_PCD_EP_Receive` 提交，IRQ 通过原 HAL/LL copy 和新 pre/post Hooks 更新 proof。停止为 `USB_Device_GetBulkOutTransfer → Begin → 多轮 Poll → Commit`。独立 HAL Hook 测试不借用 BSP 停止算法证明自己。

VERIFIED：`CM7/Platform/usb_device.c:2222` 与 `CM7/Platform/usb_device.c:2236` 持真实 transfer/attempt，旧句柄不得影响新 Arm。`CM7/Platform/usb_device.c:2256` 只失效 EP0 Token、置停止要求、通知状态，不释放源或 Worker。新增私有状态均为静态有界，无新 RTOS Task 或动态内存。

VERIFIED：三个重要补强为：仅新 Init 首次 Start 前绑定；XFRC 回调等真实 RX 消费完成而不是 W1C 后直接归还；单端点 OUT Flush 在成套模式始终拒绝，即便没有普通 owner，也不破坏 HAL SETUP 接收。

## 证据与边界

- VERIFIED：硬件依据、寄存器副作用与 HAL 写者映射见 [HAL Hook 契约](USB_BulkOUT_C1_HALHook_Contract.md)。
- VERIFIED：NAK 唯一 owner、RX 屏障、IRQ/PIO 串行化、StopAttempt 与 Reconcile 见 [Stop Proof 契约](USB_BulkOUT_C1_StopProof_Contract.md)。
- VERIFIED（软件）：原76、原C0 24、独立Hook及新增停止测试、三配置及17历史入口明细见 [软件验收](USB_BulkOUT_C1_SoftwareVerification.md)。以该报告及其 final05 JSON 为最终源码证据，早期 final01/02/03/04 是保留的中间检查。
- UNRESOLVED / NOT_TESTED：通用 EP0 恢复、真实时序、core ID、全部 ZLP 和类迁移门禁见 [剩余条件](USB_BulkOUT_C1_RemainingRisks.md)。本报告不批准其后续实施。

VERIFIED：只暂存明确路径，原 C0 报告保持基线字节；误在工作区运行过一次旧 C0 入口生成的中间报告另存 `c0-intermediate-unbound.json` 后还原其本轮改动，原断言未改。原始测试历史、首次 Hook/停止夹具失败、后续检查均保留，未使用 clean/reset/stash。PDF及 output不提交。

NOT_TESTED：本轮未烧录、未插拔、未执行 Windows USBPcap 或真实 SD 写入；不宣称 MSC 整机重连通过。软件机制交付与板级验收分开；完成后停在 C1 等待人工审核。
