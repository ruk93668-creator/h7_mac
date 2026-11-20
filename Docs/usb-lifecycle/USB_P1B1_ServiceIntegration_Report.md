# P1-B.1 Service 停止事务集成

基线：`5113000227e8d3d3a9a460b4585993e28096d0cc`。工作区 `D:/32/STM32H7/h7_mac_lifecycle`，分支 `codex/usb-device-lifecycle-manager`。

## 调用链及执行器

VERIFIED（源码）：`CM7/Application/Tasks/usb_task.c:150` 调用 `App_USBProcess()`；`:167` 调用 `App_USBFinishRound()`。`CM7/Application/app_usb.c:105` 仅在 started 且非 stopping 时调用 Mode Process；`:124` 的 FinishRound 只在 stopping 且 Bridge idle 后调用 Mode Stop。两个入口在同一个 USB Task 内串行，根据业务 stopping 互斥，不是两个独立 Stop 执行器。Application 和 USB Task 文件没有修改。

VERIFIED（源码）：`CM7/Service/usb_mode.c:279` STOPPING 和 `:392` 显式 Stop 均使用 `UsbMode_AdvanceStop`（`:357`）。显式意图成功进入 IDLE；同模式 Reconnect 意图成功进入 WAIT_RECONNECT。重复显式 Stop 不创建新事务，IDLE 直接成功。ERROR 保留失败结果，必须显式 RequestReconnect 才再次驱动恢复，不在普通 Process 中自动重启硬件。

## Pending 准入

VERIFIED：`CM7/Platform/usb_device.c:444` 的 `USB_Device_GetStopProgress` 只读原子快照包含 transaction_id、state、result、registered_pending 和实际端点 Transfer/Attempt。`:468` 的 Stop wrapper 每次清除本次 Pending 资格；只有 `Usb_DrainBulkInStops`（`:1876`）成功 Begin/Poll 了真实非零 Attempt，且整个扫描无错误、确实还有待推进端点，才登记 Pending。控制重入、普通 BSP BUSY、无效身份不得借用历史 Pending。事务 ID 不回绕，耗尽拒绝创建。

VERIFIED：跨轮查询使用同一 aggregate transaction_id。该 ID 绑定受控 Stop 扫描中保存的真实端点事务，不用普通 BUSY 推测资格。多个 Bulk IN 的快照只投影当前最后一个 Pending 参与者，完整参与者身份仍保存在 Platform 数组；此快照不是所有端点已停止的证明。完成只以全体端点 Commit、硬件 Stop、Class/Endpoint 清理的实际返回值为准。

VERIFIED：Service 接受真实 Pending 时返回 SYS_ERR_BUSY，但 GetLastResult 维持 SYS_OK。未登记的 BUSY 转为 SYS_ERR_IO/ERROR，原始结果保留在 Platform 进度快照。App 对 BUSY 不提前 ACK、不记 FAULT；最终结果才 ACK。新增测试只读编入真实 app_usb.c，覆盖 Pending、一次最终 ACK 和普通 BUSY 失败 ACK。

## 超时与失败

VERIFIED：Service 从停止意图建立时累计 1000 ms，不在每次 Poll 续期；BSP 保留原 Begin 的有界 deadline。两层还有 4096 次推进上限，防止 HAL_GetTick/Platform_TimeGetMs 冻结时无限 Pending。次数上限是保守活性保险，不代表已经过去 1000 ms，也不代表硬件停止。无符号差值支持 tick 正常回绕；倒退/异常跳变可以保守失败。

VERIFIED：Service 超时只关闭业务推进资格，不能释放 USB 引用或外部 SD IDMA。后续显式恢复仍需原身份和新硬件证据。不同事务 ID 立即 INVALID_STATE/ERROR。Reset/Disconnect 后不能拿原 Pending 当新会话许可。

## 证据与边界

VERIFIED（软件模型）：`Tests/UsbBulkInIntegration/reports/integration.json` 保存逐步状态、寄存器、引用、断言和源码 SHA；原 38 项在 `Tests/UsbBulkIn/reports/specialist.json`。初始夹具失败保存在 integration history JSON，未改原历史断言。

UNRESOLVED：这不是 Bulk OUT 生命周期迁移，也没有保证原 MSC BOT Reset/退出调用者已适配新同步接口契约。NOT_TESTED：真实 IRQ 时序、Task 调度延迟及硬件 ACK。构建/MAP及逐用例回归见 [最终验证](USB_P1B1_FinalVerification.md)。
