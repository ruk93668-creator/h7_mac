# USB Endpoint Lifecycle P1 实施进度与阻塞报告

> 起点：`9d38e56966f8ea5da9ee63285b037747613c47d5`；研究分支：`codex/usb-device-lifecycle-manager`；日期2026-11-19。
> **结论：P1 未完整通过。交付的是硬件证据核对、停止契约细化、真实 HAL/LL 缺口测试和基线回归。生产 Endpoint Lifecycle 尚未实施。**
> VERIFIED=明确标注来源的证据；INFERRED=设计；UNRESOLVED=阻塞/缺证明；NOT_TESTED=尚未运行或无实板验证。

## 1. 授权、基线与实际修改

**VERIFIED**：本次授权 Endpoint BSP/Platform、必要 USB Task 协调、对应测试/文档/编译；禁止 Core/HAL/LL、MSC/UVC协议、SDMMC/FatFS、Camera/Media、无关RTOS修改和烧录。新worktree起始HEAD/远端均为9d38e56，分支正确，起始干净。没有操作原 `h7_mac_sd` 工作区。

**VERIFIED**：本轮实际修改仅 `Docs/usb-lifecycle/` 文档和新增 `Tests/EndpointLifecycle/` 验证工具/结果。**生产代码修改为0**，旧测试源码和断言修改为0。没有把本报告中的候选API写进生产头文件，避免留下可被误用为已验证安全停止的接口。

## 2. 为什么没有实施生产停止序列

**UNRESOLVED**：P1-A要求以H747 RM0399正文核验四类端点的停止条件。官方PDF下载受到大小限制、HTTP567/超时等阻断，本机也没有原件；只能核对版本索引/目录。ES0445 Rev7虽已取得，不能用勘误片段代替完整的IN/OUT/EP0停止编程模型。

**INFERRED（范围决策）**：任意选用SNAK/EPDIS、全局OUT NAK或共享RX Flush时序会违反“不得复制未经核验寄存器操作”的要求。当前LL返回形态也不足以生成完整proof。因此未改BSP Stop/Abort、未迁移Class生命周期。不会通过清busy、强制Free、直接重启PCD绕开门禁。用户的生产修改授权有效，当前阻碍是证据而非缺少许可。

详细证据见 [HardwareEvidence](USB_EndpointLifecycle_HardwareEvidence.md)，完整软件结果见 [SoftwareVerification](USB_EndpointLifecycle_SoftwareVerification.md)。

## 3. P1-B 候选停止接口与状态契约

以下全部 **INFERRED / 尚未实现**，不是可调用API，也不是通过验收的硬件算法。

### 3.1 每方向身份与状态

建议每个controller/endpoint number/direction维护固定记录：

| 字段 | 契约 |
| -- | -- |
| controller incarnation / session id | 全设备身份；Reset改变新提交资格但不删除旧收尾记录 |
| endpoint address / open_generation | 方向独立，关闭重开递增且不回绕 |
| transfer_id / transfer_generation | arm前分配，零无效；耗尽拒绝新工作 |
| state | CLOSED、IDLE、ARMED、ACTIVE、QUIESCING、QUIESCENT、FAULTED |
| buffer pointer / length / owner | 持有原owner；PIO source、OUT destination、queued callback分别计引用 |
| PIO cursor / continuation_fenced | 单包XFRC不能当整笔完成，fence后禁止继续读source/重装 |
| completion / cancellation / retired flags | 正常完成与取消只能有一个终结；重复事件只去重，不二次释放 |
| first_failure / latest_attempt / raw phase | 首因保留；Reconcile补证据不改写历史失败 |

**INFERRED**：ARMED表示软件已登记且即将提交，ACTIVE必须有实际提交事实；QUIESCING仍拥有未归还引用；QUIESCENT只由完整proof驱动。CLOSED必须有硬件/事件退休及close后置条件，不能直接清opened/busy获得。

### 3.2 同步有界 Abort

拟议 `EndpointAbort(expected_identity, absolute_deadline, out_proof)` 仅允许Task调用。每次结果必须区分：

- `NOT_EXECUTED_BUSY`：依赖/锁/scope不满足，尚未向硬件发停止，不登记“取消已提交”。
- `ALREADY_NORMAL_COMPLETE`：同identity的整笔传输已正常结束，依原正常证据归还一次。
- `ABORT_PROVED`：完整取消proof成立；不是只收到HAL_OK。
- `HARDWARE_TIMEOUT`、`FIFO_FAILURE`、`PROOF_INCOMPLETE`：保留引用和故障记录，进入RECOVERY_REQUIRED。
- `IDENTITY_MISMATCH`/不允许的上下文：拒绝，不更改当前传输。

**VERIFIED（源码/软件测试）**：当前HAL Abort不获取PCD Lock；LL只有OK/ERROR，不产生普通Bulk Cancel完成回调。故不能把HAL_ERROR包装成等待回调的Pending。HAL Flush/Stop可能因Lock返回BUSY，该BUSY又不同于BSP共享RX guard。定位见 `CM7/BSP/bsp_usb.c:346`、`CM7/BSP/bsp_usb.c:758`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1992`。

**UNRESOLVED**：实际硬件deadline预算和RM顺序未闭合。当前10000次EPENA轮询及0xF000000次FIFO轮询都不是经校准的时间上界；外部超时检查不能中断内部长轮询。

### 3.3 IRQ/PIO串行化

**INFERRED**：短临界区只发布admission fence与身份/引用快照，不持全局USB IRQ关闭等待任务或必需的软件IRQ收尾。BSP必须在HAL消费前保存事件身份/原始状态，HAL后收集消费结果；硬件停止阶段按已核验的scope与掩码处理，其他必要IRQ继续可服务。

**INFERRED**：raw IRQ没有软件Token，不能等新传输arm之后再贴当前identity。新arm前必须退役旧pending event和FIFO/PIO引用；旧软件事件带原身份只进cleanup-only通道。XFRC先到则normal complete赢一次；Abort先fence则迟到XFRC不能触发新业务续包；EPDISD不能再产生第二次释放。当前未实现这些通道，不能宣称竞态安全。

### 3.4 真实FIFO结果

**VERIFIED（源码/软件测试）**：公开LL FIFO函数能取得真实返回；HAL wrapper会吞掉timeout。候选BSP封装应在正确PCD锁和停止scope下调用LL并保留raw result，不能再调用HAL_EP_Flush后把HAL_OK签成proof。

**UNRESOLVED**：sharedRX包括EP0，当前other-RX guard仅扫非零端点；必须证明所有被Flush数据都属于冻结scope或取得相关owner许可。不弹出GRXSTSP作为“诊断清理”。不新增未经手册核验的轮询实现来缩短LL预算。

### 3.5 EndpointQuiescenceProof 字段与签发条件

| 证据字段（INFERRED） | 签发要求 |
| -- | -- |
| endpoint/direction/open_generation/transfer_id/generation | 与本次停止和原借用记录精确一致 |
| abort outcome + raw HAL/LL result | 区分未执行/正常先完成/取消成功/超时/FIFO失败 |
| EPENA pre/post、NAK/EPDISD/XFRC事实 | 按端点类型的RM接受谓词，不能单看任一位 |
| IRQ capture/consume sequence、retired event set | W1C前已有持久归属，旧事件不可能驱动新arm |
| PIO source/destination/continuation refs | 无剩余访问，不能仅因USB DMA关闭省略 |
| FIFO scope、真实结果和后置证据 | 共享RX安全、失败未被HAL隐藏 |
| buffer owner/ref ledger | 正常完成或取消只归还一次；未证明引用仍pin |
| proof status / first failure / reconciliation history | 不完整不得Ready；后续补证据不复活旧事务 |

**INFERRED**：USB proof不包含SD IDMA或Camera Frame owner的归还。USB quiescent成立，也不能自动开放FatFS、清RAW租约或释放Camera frame。

### 3.6 Reconcile 与Stop失败

**INFERRED**：Reconcile必须以原identity/failed attempt进入，保持admission关闭，在原ledger补取晚到EPDISD/正常完成/FIFO/IRQ事实；完整后只能退役原义务，不把历史失败改为成功，不发送旧EP0状态包。若新session已存在，旧记录只可cleanup，不可访问新context。

**INFERRED**：BSP Stop将来应分阶段记录冻结、各端点Prepare/proof、Class/endpoint收尾与PCD Stop进度。PCD Stop成功前不得无条件关闭IRQ；PCD失败保留started与实际mask/缓冲记录；重复Stop只继续未完成阶段，不重放已释放动作。当前实现与此不同（`CM7/BSP/bsp_usb.c:188`），本轮没有修正，必须保留为未完成项。

## 4. 软件完成与未完成边界

**VERIFIED（软件测试）**：新增18项真实HAL/LL刻画测试、108条断言通过；17个原测试入口全部执行，原有失败/脚本错误保留。完整构建和分组结果以SoftwareVerification中的实际记录为准。

**UNRESOLVED**：尚无生产Transfer Identity/Ledger、Endpoint状态机、同步Abort proof API、IRQ pre/post退休路由、Reconcile或安全Stop实现。因此不能把18项缺口刻画当成P1要求的全部竞态/所有权验收。

**NOT_TESTED**：新Endpoint实现的重复释放、真实PIO续包与取消竞争、迟到IRQ跨generation归属、共享FIFO并发、EP0硬件边界等未验收。所有板级项目NOT_TESTED。

## 5. 修改范围、回滚和Git

**VERIFIED**：新增测试不改生产代码、不改旧断言。单元scatter仅位于 `Tests/EndpointLifecycle/test.sct`，不是生产linker脚本。回滚可移除本轮新增测试/报告和P1文档更新；不需要刷机，不动原工作区。构建产物和隔离依赖在现有ignored output/python_deps目录，不提交凭据/本机运行库。

**INFERRED（交付程序）**：按实际未完成状态创建明确标识的备份提交，推送当前研究分支并核对SHA。提交不代表P1批准通过；最终Git结果由实际工具返回值记录。

## 6. 解除阻塞后继续的顺序

**INFERRED**：先取得可校验的RM0399完整正文与修订差异，再关闭四类端点时序/Errata实例适用性/锁与时间预算；之后实现BSP proof primitive和每方向身份，连接Platform查询/失败保留，最后执行P1全部竞态与所有权故障注入和三配置构建。不得直接进入P2或迁移MSC/UVC Class生命周期。
