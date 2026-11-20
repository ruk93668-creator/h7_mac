> VERIFIED：2026-11-20 P1-C1 修订补充见 [C1实施报告](USB_BulkOUT_C1_ImplementationReport.md)、[当前HAL与硬件映射](USB_BulkOUT_C1_HALHook_Contract.md)、[当前停止/NAK/RX契约](USB_BulkOUT_C1_StopProof_Contract.md)、[最终软件验证](USB_BulkOUT_C1_SoftwareVerification.md)。以下 C0 正文保留原基线、行号和当时阻塞结论，不代表已实施 C1 后的现状；尚未证明条件以 [C1清单](USB_BulkOUT_C1_RemainingRisks.md) 为准。

# P1-C — Global OUT NAK Ownership Contract

## 状态与边界

UNRESOLVED / C1门禁：这是待审批契约，没有新增生产lease，也没有通过清mask绕过HAL。逻辑停止Endpoint 2只禁止该方向新Receive；全局NAK会短暂停止所有非SETUP OUT、丢弃ISO OUT包，是控制器共享协调资源，二者不能合并成一个busy。

INFERRED设计：全局静态单槽lease包含controller/session、单调不回绕lease_id、请求者集合、目标Endpoint/StopAttempt、phase、NAK marker消费序号、GONAKEFF事件证据、首次/当前错误、已保存的相关mask位、PIO引用。所有实际SGONAK/CGONAK写入者必须经过该槽。Token耗尽拒绝，失败保留，不建伪新身份恢复。

| phase | 进入条件 | 许可动作/完成条件 |
| --- | --- | --- |
| FREE | 无旧lease；硬件无不可归属GONAKEFF/GONSTS；无未闭合命令 | 原子claim非零身份，关闭目标新接收提交入口 |
| REQUESTED | lease有效、原会话不变 | 一次SGONAK；保持HAL RX消费者可运行，不在ISR等待 |
| WAIT_MARKER | 正常数据仍可能到达 | 同一次真实pop记录marker和先前payload归属；Task有界Poll |
| EFFECTIVE | marker对应本lease，GONAKEFF事实成立 | 目标若enabled，发EPDIS/SNAK；其他OUT仍归各自Buffer所有者 |
| RETIRING | 目标有Disable及事件退休证明 | 归还旧PIO引用；不清共享RX；EP0 SETUP继续有消费者 |
| RELEASED | 目标proof完整，所有参与者一致释放 | 仅本lease发CGONAK并核对效果，按位恢复原合法mask；再允许Commit |
| RECOVERY_REQUIRED | 任意超时、owner变化、Reset/Disconnect、证据缺失 | 不释放Buffer、不关闭全USB IRQ，不清全局owner，不把HAL_ERROR当Pending |

## 当前HAL冲突的具体证据

VERIFIED：HAL `stm32h7xx_hal_pcd.c:1146–1148` 不检查本lease，对任意OUT EPDISD发CGONAK；`:1415`修改GONAKEFFM；`:1417–1423`对ISO参与者同步Abort；`:1460–1464` ISO OUT会自行启用mask并SGONAK。C0真实HAL测试暴露这些操作，不是用伪造的取消回调模拟成功。

INFERRED：只mask目标DAINT不能阻止其他OUT的CGONAK。全局mask全部OUT/ISO事件会延迟EP0或其他参与者的合法收尾，并没有给遗留事件提供身份；先peek再放开HAL也不能保证两次读取之间无新事件。不能以“当前MSC只有一个OUT”替代可复用契约。

UNRESOLVED：在不修改HAL的情况下，是否存在经全面证明、保留全部HAL设备功能的最小BSP独占分发方案。本轮没有这份证明，未实施选择性清位/重放或复制HAL IRQ。扩展申请建议在原HAL内保留默认行为、增加受控owner通知与决策点。

## 失败和恢复

INFERRED：GONAKEFF超时只表示未获得屏障，不说明旧包已排空。EPDISD超时也不归还Buffer。Reconcile必须原lease、原StopAttempt、原Session、未发生其他SGONAK/CGONAK、单消费者journal连续；只有实际marker/EPDISD/PIO退休证据补齐，才继续原阶段。owner未知或lease已被提前解除，不能仅因后来GONSTS=1推测还是原事务。

NOT_TESTED：上述C1恢复、重复Stop/Commit的生产行为全部尚未实现；本轮模型仅验证当前HAL冲突，不能将之列为C1 PASS。
