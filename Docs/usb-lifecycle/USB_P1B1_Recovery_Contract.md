# P1-B.1 StopAttempt 受控 Reconcile

## 失败信息与不可自动消失的引用

VERIFIED：`CM7/BSP/bsp_usb.c:138` 的 Usb_BulkFault 保存首次 first_error / failed_stage，递增饱和 failure_count，同时保存最近 error。`BspUsbInStopProof_t` 新增 reconcile_count / fifo_conflicted。不会重新创建 Transfer，不重置原 StopAttempt，不释放 source/pool，不恢复 DAINTMSK/DIEPEMPMSK。

VERIFIED：失败后普通 Poll 只返回事实，不把时间经过当完成。Reconcile（bsp_usb.c:373）仅 Task，同 Controller/Session/Endpoint/Transfer/Attempt、无 session 变更、无 PIO 引用、无 pending Reset/OTG 事件、原始和当前错误均 TIMEOUT、无 FIFO taint，且只允许一次。其他失败继续 RECOVERY_REQUIRED。

## 唯一可接受的新增硬件证据

| 原失败阶段 | 复核条件 | 可继续到 | 仍禁止的动作 |
| --- | --- | --- | --- |
| DISABLE timeout | EPENA=0、EPDIS=0、AHBIDL=1、无TX/RX Flush，原端点 EPDISD 实际存在，FIFO owner=0 | AHB，由原 Poll 发真实目标 Flush | 不提前归还 Buffer，不恢复 mask |
| FIFO timeout | owner仍为本端点、无 IRQ taint，TXFNUM仍是本端点，TXFFLSH/RXFFLSH=0，AHBIDL=1，EPENA/EPDIS=0 | RETIRE，记录本命令真实完成后安全释放 FIFO owner | 不凭 HAL_OK 或别的端点命令清位成功 |

VERIFIED：Reconcile 成功只给尚未完成的收尾一个有界预算，first_error、failed_stage、failure_count、StopAttempt、Transfer身份保留；不分配新 session。继续 Poll→PROVEN→Commit，Commit 仍完整核对事件退休和掩码重新暴露条件，才允许引用归还。重复 Reconcile 明确拒绝，重复 Commit 幂等。

## 真实生产调用链

VERIFIED：Platform `USB_Device_ReconcileBulkInStop` 核对实际支持的 IN endpoint 和原 Platform sequence；`Usb_DrainBulkInStops` 使用其已经保存的原端点事务进行复核，不会现场重建身份来绕过错误。必须有后续显式 Stop 推进；Service ERROR 本身不后台重启，RequestReconnect 才可建立再次推进意图。aggregate transaction_id 和端点 StopAttempt 均延续，测试验证最终仅归还一次。

VERIFIED：BUSY 复核返回不代表“取消已提交”，Reconcile 拒绝或证据尚不足时上层仍得到原故障；没有伪造 Service Pending。旧 Session 迟到事件只能使旧事务失去资格或保留证据，不能取得新 Transfer 身份。

## 无法复核的单一未闭合条件

| 故障 | 未闭合条件 / 后续要求 |
| --- | --- |
| NAK timeout、未知 HAL 提交失败 | 无本 Attempt 可归属的终止事件；不能仅因 EPENA=0归还 |
| Reset/Disconnect 或 session 不同 | 原会话停止/端点重建事件归属已改变；需要后续生命周期阶段的完整重建证明 |
| FIFO IRQ taint / TXFNUM改变 | 命令身份连续性丢失；不得擅自清 owner |
| PIO 引用尚在 | 软件续包仍可能访问原 Buffer；先取得独立引用归还事实 |
| 已复核后再次失败 | 本次受限恢复预算已经用尽；保持现场等待人工审核 |
| 原 HAL/PCD Stop 或其他端点失败 | 不属于 Bulk IN Reconcile 能证明的资源；旧失败状态必须保留 |

UNRESOLVED：未实现 Bulk OUT、UVC frame lease、全部 Controller Stop 的统一恢复；BSP 旧全设备 Stop 在非 Bulk 阶段的 IRQ/Flush 失败契约仍需后续工作。本轮不直接重启 PCD，不清零硬件/引用，不将 RAW 租约交回 FatFS。NOT_TESTED：真实迟到 IRQ、硬件静止时间和板级恢复。
