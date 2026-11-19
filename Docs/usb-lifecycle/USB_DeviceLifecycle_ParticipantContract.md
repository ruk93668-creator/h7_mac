# UDLM Participant、Ticket 与所有权契约

> **P1进度（2026-11-19）**：本轮为未完成/受阻交付，生产代码未改；下面原有内容是Phase0历史设计。当前结果见本文末尾及三份EndpointLifecycle报告。

> 基线：`35401119bef1d5a4594925478119e048f68c2c77`；研究分支：`codex/usb-device-lifecycle-manager`；审计日期：2026-11-19。
> 范围：Phase 0，仅文档；未修改生产/测试代码，未编译或烧录。
> 标记：**VERIFIED**＝本基线源码确认；**INFERRED**＝推断或拟议设计，尚非已实现能力；**UNRESOLVED**＝证据/契约尚缺；**NOT_TESTED**＝本轮未运行验证。源码引用采用仓库相对路径和一基行号，均相对于上述 SHA。

## 1. 注册与依赖方向

**INFERRED**：静态 `ParticipantRegistry` 每项包含稳定 participant_id、kind、parent/scope、固定上下文、generation、Prepare/Poll/Commit/Query/Retire 函数和静态依赖列表。注册仅 Controller 未开始 Session 时；事务中冻结成员表和版本。动态注销必须无事务、无 ledger 引用且旧事件已退休。第一版只注册当前单 Class；未来多个 Class 可成为多个 participant，不由此推断 Composite 已实现。

**VERIFIED**：当前固定单 Class callbacks/池，见 `CM7/Platform/usb_device.c:1678`、`CM7/Platform/usb_device.c:972`。Class 驱动将来只注册 adapter，不能让协调器直接访问 BOT 私有状态或 Camera/Storage 硬件。

**INFERRED** 依赖图（等待箭头是“必须先确认”，不是允许跨层直接调用）：

- Class Commit 等待所有相关 endpoint proof、旧 callback 引用退休、Class-owned 缓冲外借归零。
- MSC endpoint 可独立 Abort；MSC data buffer 可回收还要等 Worker ticket；Worker 由原 StorageTask 执行并验证 SD 状态，不在 USB IRQ 内执行。
- UVC Payload 停止等 EP1 proof；Frame Lease 归还等 Payload 无借用，RETURN_ACK 由原 Camera owner 产生；Class commit pin 保留到最后，不能为了查询 status 而提前 free。
- Controller Stop 等 Class Release、数据端点 Close、FIFO/PIO/旧事件退休。EP0 只在相应 Device scope 最后收尾，Interface/BOT scope 不清全局 EP0/RX FIFO。

## 2. 接口返回契约

以下均为 **INFERRED** 的概念接口，不是本轮新增代码：

| 操作 | 返回/效果 | 禁止 |
| -- | -- | -- |
| Prepare(tx,scope) | READY+evidence、WAIT+dependency、FAILED+retained obligations | WAIT 时 Free；跨任务阻塞；把 timeout 当成功 |
| PollPrepare(tx) | 有界读取/推进原依赖，重复调用幂等 | 重新分配不同请求 token、盲目重复外部 Release |
| Commit(ticket) | COMMITTED 或 FAILED_WITH_PROGRESS；至多一次消费 | 无票操作、隐式启动新 I/O、等队列 ACK |
| QueryOwnership(identity) | 返回不可变快照版本及 outstanding refs | 只返回笼统 busy 布尔来替代证明 |
| Reconcile(failed_tx) | 在原 ledger 上补取硬件静止/迟到完成证据 | 修改历史失败为成功或恢复旧 EP0 Token |
| Retire(identity) | 全引用结算且硬件事件已围栏后释放记录 | 仅因 session 不当前而丢事件 |

**INFERRED**：Prepare 关闭 admission，停止/取消是允许的副作用，但不得释放最终 Class Context。纯逻辑 Prepare 无需异步化；Worker/Lease 等待只记录 WAIT，下次 Task Process 推进。Commit 严格短同步；如出现依赖仍 Pending，是契约违例，否决状态并故障，不在 Core callback 中等。

## 3. PreparedTicket

**INFERRED**：票据由 coordinator 独占固定存储；不接受外部任意构造的结构体。建议字段：

| 字段组 | 绑定/用途 |
| -- | -- |
| ticket_id、tx_id、requested_reason/target、scope_mask | 精确绑定一次破坏性操作 |
| controller/session/class/interface generations | 防止票据跨会话/跨实例使用 |
| membership_version、resource_set_version | 防止 Prepare 后新注册/新打开/扩大 scope |
| proof_handles[participant]、proof_versions | 引用 BSP/Worker/Lease 的具体证明记录，不复制虚假布尔 |
| ledger_revision、outstanding_dependency_mask | 证明覆盖同一份资源集合 |
| setup_sequence + 全8字节请求 + status资格 | 仅 EP0 请求使用；新 SETUP 失效 |
| prepared/consuming/consumed/veto、progress_bitmap | 单次提交、失败后续收尾、防重复 Free |

**INFERRED** 消费规则：USB Task 在短 IRQ lock 下核对所有字段、admission 仍关闭、无新硬件 Reset/fault、proof 覆盖和版本未变，原子转 CONSUMING 后调用 Core。每 participant commit 记录进度。成功 CONSUMED；失败 FAILED_WITH_PROGRESS，销毁“再执行 Commit”的资格，保留 cleanup ticket/ledger。重复同事务 Query 返回原终态，不能再调用 Class Free。ticket 失效只失去提交权，不释放其引用。

**INFERRED**：资源的 owner pin 与外借 ref 分开。PREPARED 需要所有外借/回调/Worker/Frame 依赖结束，但 Class Context 的 coordinator commit pin 此时仍为1；因此“ledger 所有引用必须等于0”不应错误地要求提前释放 Class 本体。最终 retire 才要求全部归零。

## 4. Endpoint Quiescence Proof

**VERIFIED**：当前 Platform Abort 仅 BSP SYS_OK 后清软件 busy（`CM7/Platform/usb_device.c:426`）；BSP Abort（`CM7/BSP/bsp_usb.c:337`）和 HAL Flush（`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:2018`）尚不提供下面完整证据。**INFERRED**：BSP 新 proof port 必须输出：

1. identity：controller/session/ep address/open_generation/transfer_sequence、stop attempt id；同时标端点类型、PIO/DMA 模式。
2. stopping method：已完整正常完成、已禁用取消、已空闲（无 arm 记录）、已由已证明的 Reset 退休；不能任意混同。
3. raw pre/post snapshot：CTL/INT/TSIZ、相关 DAINT/Mask/GRSTCTL；时间/序号；HAL 消费前的 EPDISD/XFRC、消费后剩余。读取不得弹出 GRXSTSP 或写 W1C。
4. 排他/停止事实：admission 关闭、ISR PIO continuation 禁止重装、没有正在运行的同源回调，EPENA/停止结果满足该方式的硬件契约。
5. FIFO 结果：所需 Tx FIFO 或共享 RX scope 的 Flush **实际底层返回值**、完成后置状态；旧数据和相关事件已归属结算，不能仅 HAL_PCD_EP_Flush HAL_OK。
6. 软件所有权：PIO source/destination 引用、待完成事件记录、HAL buffer 引用都已退休；旧回调不会在新 Open/Arm 后指向新 buffer。
7. result/source：原返回码、原因、failed phase、timeout/partial flags；失败 proof 不能用来释放。

**UNRESOLVED**：RM0399 尚未核验部分顺序；proof 接受谓词是待完成硬件论证的设计。不得把“EPENA 已清”提升为所有场景的静止证明。EP0、ISO 和 Bulk 必须分开论证；只有与身份匹配且完整的软件/硬件边界闭合的 proof 才是 READY。

## 5. Endpoint Request / Pending / Cancelled / Normal Complete / Failed

| 状态（INFERRED） | 资源意义 | 唯一合法后续 |
| -- | -- | -- |
| Request | 接受停止意图，冻结新提交；原传输仍拥有 buffer | 同步 proof 成功，或 WAIT 依赖，或 Failed |
| Pending | 协调器等待依赖/补证据；不是当前 HAL Abort 的原生返回 | 正常完整完成、具名取消证明、故障；没有“超时即释放” |
| Cancelled | identity 对应传输不再可能访问 buffer，旧事件已结算；不产生正常数据成功 | owner 在 ledger 中只结算一次，再 Retire |
| Normal Complete | 同 identity 的整个 BSP 传输（包括续包）正常完成 | 一次性结算，Stop 继续；不能再 Cancelled 一次 |
| Failed | 保留现场及所有未证明停止的引用；硬件状态可未知 | Reconcile 收尾；保留业务失败；禁止新会话复用 |

**INFERRED**：Cancel 与 XFRC 竞争由同一端点记录仲裁。如果捕获的是单包 XFRC 且 BSP 还有剩余 source，则不是整个传输 Normal Complete；停止 continuation 后须做取消 proof。EPDISD 只提供相关硬件事实，不自动合成任何 Class DataIn/DataOut 回调。

## 6. Ownership Reference Ledger

**INFERRED**：每条记录存 resource_id/generation、原 owner、当前借用者、借用 identity、用途/范围、获得时刻、归还证据、terminal classification、refcount 和 outstanding obligation。owner 必须具名，禁止匿名计数。

| 资源 | 借用来源 | 允许归还的证据 |
| -- | -- | -- |
| MSC CBW/OUT 数据 | USB OUT HAL/PIO | 全传输正常完成，或 endpoint cancel proof + 事件退休 |
| MSC IN/CSW | BSP PIO source / USB IN | 全部包完成并匹配 sequence，或取消 proof |
| MSC data[1024] | USB 和 Storage Worker 轮流借用 | USB proof、Worker request 完成/取消排空两份独立证据；不可双向无约束使用 |
| SD 专用 IDMA buffer | 原 SDMMC 实现 | 原 BSP diagnostics + quiescent 出口证明；UDLM 不直接清 IDMA 或释放 RAW |
| UVC packet | Payload/BSP | packet token 正常完成，或 EP1 proof 后 PayloadAbortPacket |
| Camera frame spans | Bridge/USB Payload | Payload 无借用 + 同租约 RETURN_ACK；不改变 Camera/Media owner |
| Class Context | Core/Platform callbacks | 旧回调引用清零、Class commit 完成后才释放 commit pin |
| EP0 状态 | Setup/status transfer | LL 提交和实际 IN 完成分开；失效后仅能 cleanup |

**VERIFIED**：MSC Worker 在 `CM7/Service/usb_msc_storage.c:156` 要求 device quiescent；SD adapter 在 `CM7/Service/usb_msc_sd.c:105` 读取 active/Recover；SDMMC 物理静止条件在 `CM7/BSP/bsp_sdmmc.c:957`。Camera lease 身份匹配在 `CM7/Application/app_uvc_bridge.c:425`。这些原有事实可以作为 adapter 输入，不直接将整个系统标 READY。

## 7. 错误码与诊断来源

**VERIFIED**：共享 OUT RX 活跃冲突在 HAL 调用前返回 SYS_ERR_BUSY（`CM7/BSP/bsp_usb.c:346`）；HAL EPStopXfer 的计数耗尽返回 HAL_ERROR（`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:934`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:953`），经 `Usb_HalResult`（`CM7/BSP/bsp_usb.c:758`）映射 SYS_ERR_IO。HAL Lock 也可能 HAL_BUSY，Flush 底层可能 HAL_TIMEOUT 但被 HAL 隐藏。

**INFERRED**：保留 `SysResult_t` 给兼容调用者，另带结构化来源：`origin(BSP_GUARD/HAL_LOCK/LL_STOP/FIFO/CORE/PARTICIPANT)`、raw HAL/LL code、endpoint/scope、phase、dependency id、first_failure、latest_attempt、proof_valid。仅明确 BUSY 依赖可 WAIT；HAL_ERROR 为失败并保留证据；HAL Flush 表面 OK 但底层未知应标 PROOF_INCOMPLETE，而非“正常 Pending”。诊断记录与唤醒消息分离，不受通知合并覆盖。

## 8. 外部 participant 与审批边界

**INFERRED**：继续复用现有 Worker mailbox 和 Frame return 消息；如需新增 proof 回复，必须有版本/identity/幂等 replay，不能直接套用 Camera lifecycle 的 consumer flags 当 USB 端点证据。队列满保留原 message 待重试，ACK 丢失时重发原请求只返回缓存结果，不重复 Release。

**VERIFIED**：既有消息发送并不代表执行完成（`CM7/Application/app_camera_lifecycle.c:184`），跨任务所有者校验在 `CM7/Application/app_messages.c:193`。**UNRESOLVED**：新增 BSP proof/应用 port 的具体 ABI、容量与授权需 Phase1 前确定；当前没有可用于实际 Commit 的 PreparedTicket 实现。

## P1 本轮证据与状态更新

**VERIFIED（软件）**：18项HAL/LL刻画显示“HAL_OK但无EPDISD/旧INT未退休”和“真实LL超时被wrapper隐藏”均可能出现。**INFERRED**：proof字段、owner/pio/fifo/event引用条件继续有效，候选接口细化见ImplementationReport。**UNRESOLVED**：当前没有实现可签发的proof API，不能由测试生成一个假READY票据。

详见 [P1实施/阻塞报告](USB_EndpointLifecycle_ImplementationReport.md)、[硬件证据](USB_EndpointLifecycle_HardwareEvidence.md)、[软件验证](USB_EndpointLifecycle_SoftwareVerification.md)。
