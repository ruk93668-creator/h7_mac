# UDLM 并发、IRQ 与旧事件退休

> 基线：`35401119bef1d5a4594925478119e048f68c2c77`；研究分支：`codex/usb-device-lifecycle-manager`；审计日期：2026-11-19。
> 范围：Phase 0，仅文档；未修改生产/测试代码，未编译或烧录。
> 标记：**VERIFIED**＝本基线源码确认；**INFERRED**＝推断或拟议设计，尚非已实现能力；**UNRESOLVED**＝证据/契约尚缺；**NOT_TESTED**＝本轮未运行验证。源码引用采用仓库相对路径和一基行号，均相对于上述 SHA。

## 1. 当前执行环境与设计边界

**VERIFIED**：USB_Task priority4/20 ms loop（`CM7/Application/Tasks/usb_task.c:16`），StorageTask priority1/100 ms loop（`CM7/Application/Tasks/storage_task.c:13`、`CM7/Application/Tasks/storage_task.c:43`）；USB IRQ priority6（`CM7/BSP/bsp_usb.c:12`）。`USB_Device_Lock` 只 mask OTG_FS NVIC（`CM7/Platform/usb_device.c:1171`）。Worker mailbox 另用短 PRIMASK 临界区（`CM7/Service/usb_msc_storage.c:20`）。不能将这几种互斥混为一谈。

| 上下文 | 当前实际动作（VERIFIED；完整引用见 SourceAudit） | UDLM 职责（INFERRED） |
| -- | -- | -- |
| USB ISR/HAL callbacks | RXFLVL 写 buffer、IN PIO 续包、Core Setup/Reset/Class DeInit、状态通知 | 保留必要数据 IRQ；只做生命周期 admission fence、事实捕获、控制资格失效、唤醒 |
| USB Task | Mode/AppUSB/BOT/UVC/Bridge pump | 唯一事务执行者，Prepare/Poll、Ticket、受控同步 Core Commit、终态发布 |
| StorageTask | RAW Claim、SD init、execute/quiescent | 继续原 owner 执行/排空；导出匹配 request 的 proof |
| Camera owner | Frame release / RETURN_ACK | 继续原协议归还，UDLM 仅等 proof |
| Core 同步 callback | 使用调用者上下文 | Commit 时短同步；不等待 RTOS 消息或 SD |
| 启动阶段 | Framework Init、PCD/Clock Init | 可执行已有初始化等待；不放进 IRQ mask 内 |

## 2. 同步有界 Abort 的串行化步骤

以下为 **INFERRED** 的 BSP 契约建议；具体寄存器次序在硬件核验前 **UNRESOLVED**。

1. USB Task 先在短原子区域关闭 scope admission，持久记录 transaction/endpoint identity；退出临界区后调用 participant Prepare。
2. 端点操作期间保存 OTG_FS enable 状态并进入短 USB IRQ fence；禁止 BSP PIO 对该端点自动续包，拒绝 Task 新 arm。不能仅在 Class 关闭提交，因为 BSP 回调可能在 Class 之前续包。
3. 捕获原 CTL/INT/TSIZ/Mask/FIFO 和 PIO offset/total/active，登记同一 transfer 的 stop attempt。先处理已完整正常完成的竞争事实，剩余则执行经手册核验的短停止原语。
4. 轮询采用经测量的单次时间预算；不能调用 vTaskDelay、等消息、等任务通知或依赖同一个已屏蔽 USB ISR 来完成软件状态。硬件轮询是否需要特定 interrupt handler 动作必须独立论证。
5. 只有停止与 FIFO/事件退休条件都满足，才更新 BSP PIO 引用、HAL buffer 引用和 proof，再由 Platform 更新 busy。不能先清 busy 让调用者覆盖 buffer。
6. 恢复进入前的 IRQ enable 状态，并按已知安全策略继续处理未退休事件。取消失败不无条件长关 IRQ，不无条件重新开危险 continuation；故障可保留端点独立 fence，由后续 Reconcile 具名处理。
7. 离开锁后通知 USB Task。跨任务 Worker/Lease 等待只能在 IRQ enable 且调度允许时推进。

**VERIFIED**：当前 LL Abort 轮询10000次（`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:915`），当前 FIFO timeout 默认0xF000000次（`Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_usb.h:41`）。**INFERRED**：这不是可靠的毫秒延迟上界；需要目标时钟、编译优化、总线负载下测量。若单次预算不可接受，才改用经过完整事件契约设计的异步 Cancel，而非让长轮询留在 ISR。

## 3. PIO 与共享 FIFO

**VERIFIED**：BSP IN PIO 在 `CM7/BSP/bsp_usb.c:655` 按源地址续包；HAL IN TXFE 在 `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:2134` 继续取 HAL buffer；HAL RXFLVL 在同文件 `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1080` 读共享 RX 状态并写 OUT buffer。硬件不启用 USB DMA 不代表 CPU/IRQ 不再访问 buffer。

**INFERRED**：一个完整传输的终结条件必须覆盖 BSP offset==total、最后一包完成、PIO continuation 已结束，不能把第一次 XFRC 当作全部完成。停止时先阻止 continuation 重装，再解释已锁存 XFRC；旧事件不再触发重新 arm。

**VERIFIED**：当前 OUT Abort 的 other-active guard 只扫非零 OUT（`CM7/BSP/bsp_usb.c:469`）；`BSP_USB_FlushEp` 的 OUT guard 也从1开始（同文件 `CM7/BSP/bsp_usb.c:424`）。AbortAll 从非零端点循环，但最后清整个 PIO/RX 数组（同文件 `CM7/BSP/bsp_usb.c:373`）。**INFERRED**：Composite/EP0 下不能把这些现有局部 guard 当作完整共享 RX proof。

**INFERRED**：共享 RX 维护依赖 scope：

- 仅停止某 OUT endpoint 时，优先使用已核验的局部停止和有归属的 RX 数据退休；无法不伤害其他 OUT/EP0 时返回 WAIT_DEPENDENCY，附 other endpoint/open generation。
- 全局 RX Flush 必须证明所有可能被冲掉的数据都属于本次冻结 scope，或具有其他端点 owner 的准备许可；不丢掉新的 SETUP。
- Device Reset/full Stop 允许扩大 scope，但必须使旧票据失效并重新 Prepare，不能悄悄清掉其他 Class 的接收状态。
- 禁止诊断读取 GRXSTSP 弹出 FIFO；原始只读状态快照和实际 drain 由 BSP 独占完成。

**UNRESOLVED**：保持 EP0 新 SETUP 接收与全局 OUT NAK/RX Flush 的精确序列需要 RM0399 和硬件证据。缺证据时不签发 proof。

## 4. HAL IRQ 前后证据

**VERIFIED**：HAL 会清 XFRC/EPDISD 后调用部分回调；IN EPDISD 内部 Flush 返回被吞（HAL PCD `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1197`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1234`），OUT EPDISD 并不产生通用取消完成（同文件 `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1144`）。HAL 处理数据 IRQ 后才处理 USBRST（同文件 `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1332`）。

**INFERRED**：BSP `OTG_FS_IRQHandler` 将来在 HAL 前 capture raw+mask/armed identity，检测 Reset 等 sticky lifecycle 事实并围栏旧资源；调用 HAL 后再 capture consumed/postcondition。快照记录应包含 ISR sequence/observed reset generation，不靠 post 状态推测旧事件。事件只归属 capture 时的已登记 arm；HAL 一次 handler 内多事件顺序必须保留。

**INFERRED**：因为 HAL 未带 Transfer Token，BSP 不得在新 arm 后给旧原始 IRQ贴“当前 token”。新 arm 的前置条件是旧 PIO/FIFO/INT 与所有软件 queued callbacks 都退休，或者硬件能力提供等价且已核验的隔离证明。缺此 fence 时 session check 只能拒绝部分软件事件，不能防止旧 IRQ 被错贴新身份。

## 5. 竞争时序

所有以下时序是 **INFERRED**；测试见 TestMatrix。

| 竞争 | 仲裁/资源处理 |
| -- | -- |
| Stop 前整笔 normal complete 已到 | 用原 sequence 结算 Normal Complete；Stop 继续，Cancel 不重复归还 |
| Stop 前只有中间包 XFRC | fence 后不续包；按原 transfer 做取消 proof，不能补正常成功 |
| Abort 失败后 EPDISD 迟到 | 保留首个失败；Reconcile 在旧 record 补取证据并收尾，不发旧成功状态包 |
| 新 Reset 与旧 Prepare 同时 | 旧 control ticket 失效；继承 cleanup obligations，扩大 scope 后重新验证；不删除 Worker/Frame refs |
| Stop 中晚到 DataIn/Out | 有 raw event fence/identity 时进入旧 cleanup-only 路由；不再驱动协议产生新数据/CSW/CBW |
| 恢复后旧软件 callback 到 | 查 retired ledger/tombstone 去重；不得操作新 Class Context/序号 |
| ClearHalt 抢在 BOT reset status 完成附近 | 保留 WasControlStatusComplete 行为与精确 Token，未确认不重启 CBW；不把这条 EP0 标准请求等同全设备 Reset |
| Class Context 已释放后 query INVALID_STATE | 只有已登记退役证明允许 Frame Return；无证明则保留旧 lease 并故障 |
| PCD Stop 部分失败 | 保留 Class/端点进度、IRQ/global mask 实况、RAW；禁止假定 Controller STOPPED |

**INFERRED**：旧事件处理需三分类：当前业务事件、旧 cleanup 事件、重复/无主事件。前两类都必须有 ownership record；无主事件保存故障快照，不访问任意当前 buffer。不能简单 `if(epoch != current) return` 丢弃必要的归还事件。

## 6. Task Prepare 与 Core Commit 原子窗口

**INFERRED**：异步等待完全在 Task 普通上下文。票据准备好后仅在短 USB IRQ fence 中做最终版本检查和同步 Commit；新 Reset 若在解锁前被硬件锁存，须用 BSP proof/status 检查否决，或在下一 ISR 立即围栏已完成本地 Commit 的 Session。不得依赖“IRQ 已关所以硬件不再 Reset”。

**INFERRED**：标准 Setup gate 原子保存 SETUP、setup sequence 和原 Session；Prepare 期间新 Setup 覆盖控制资格但不覆盖 cleanup record。Task 进入 Core 前验证当前 Setup 仍相同；Core 未准备失败时在 LL STATUS_IN 路径做 veto。短 Commit 不能递归执行整套 Task Process，也不能允许另一个 Task 调直接 Core Stop。

**VERIFIED**：现有 `USBD_CtlSendStatus` 无论 LL 结果仍返回 OK（`Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c:171`）；`USBD_StdEPReq` ClearHalt 先状态再 Class callback（`Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c:324`）。**INFERRED**：状态完成只凭对应 Status transfer 的实际完成，不凭 Core返回或“Class回调随后成功”。正常标准请求行为须做历史对照，不把全EP0都强行异步化。

## 7. 通知、队列、内存顺序与公平性

**VERIFIED**：App USB notify 将原因合并成 progress bit（`CM7/Application/app_events.c:268`），`App_EventsWait` 用 xTaskNotifyWait（同文件 `CM7/Application/app_events.c:162`）；现有 lifecycle queues 固定长度4（`CM7/Application/app_messages.c:110`）。

**INFERRED**：通知是 wake hint；事务事实存固定持久记录，Task 每轮无论是否收到特定位都检查 pending。ISR 写记录须先写内容、memory barrier、再发布 sequence；Task 读取在短 lock 或 seqlock 重读下形成一致快照；volatile 本身不是完整并发协议。队满保留 sticky flags/原消息，不能丢 Reset/Disconnect 或覆盖首个失败。

**INFERRED**：USB Task 每轮只做有界 Prepare/Poll 和协议工作，不持续忙轮询 Storage drained。高优先级 USB 不得饿死 StorageTask；记录等待对象并让出调度，下次事件或周期继续。测量 UVC 1 ms SOF 机会与20 ms Task 常规等待及 notify 合并的关系，不能以 priority4 保证每帧服务。

**NOT_TESTED**：以上锁预算、HAL 前后事件观测、共享 RX FIFO/EP0 竞争、Worker 迟到完成和 UVC 活跃流在本轮均未运行。
