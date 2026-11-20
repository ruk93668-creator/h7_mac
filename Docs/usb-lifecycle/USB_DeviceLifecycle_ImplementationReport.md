> P1-B.1 更新：以下原 P1-B 内容保留为历史证据；Service Pending、纯 SOF 仲裁与受控 Reconcile 的当前结论以 [P1-B.1 最终验证](USB_P1B1_FinalVerification.md) 及其四份专题报告为准。五项旧同步断言未迁移，历史 FAIL 未删除，硬件验收仍 NOT_TESTED。

# P1-B 实施报告与受阻项

> P1-B基线：`5a27af621c219ead2bc680858efd4aeb17e110a4`；工作区：`D:/32/STM32H7/h7_mac_lifecycle`；分支：`codex/usb-device-lifecycle-manager`。
> VERIFIED注明源码/手册/软件模型；INFERRED为设计推导；UNRESOLVED为尚缺证明；NOT_TESTED为未执行的实板验证。本文不批准P1-C、EP0/UVC/MSC迁移或统一生命周期管理器。

## 1. 交付状态

**VERIFIED（源码）**：已增加Bulk IN身份、独立StopAttempt、提交/完成计数、任务三阶段停止、唯一XFRC结算、定向FIFO进度、失败引用保留及Platform序号/busy联动。新接口没有调用旧HAL同步Abort/Flush，没有新增RTOS任务、堆内存或Keil条目。

**UNRESOLVED（总体验收）**：本阶段是可评审的受限生产实现，不能标为P1-B整体PASS。主动Platform Stop已驱动单步事务，但Service将BUSY转ERROR，不会持续推进；同步Core/旧Class也不会驱动任务事务，关键冲突采用保留现场；全负载停止可用性、ZLP勘误规避和实板证明仍缺。本次交付不授权烧录或P1-C。

## 2. 修改范围

- VERIFIED：生产只修改`CM7/BSP/bsp_usb.c/.h`和`CM7/Platform/usb_device.c/.h`。
- VERIFIED：专项`Tests/UsbBulkIn/`及四份旧夹具的机械接口适配；原17入口、旧断言和修改前失败证据保留。
- VERIFIED：五份P1-B文档及测试报告；原RM PDF原地、哈希不变，不暂存。
- VERIFIED：ST Core/HAL/LL、MSC/UVC Class、SDMMC/FatFS、FreeRTOS、Keil/RTE/MPU/Linker、CM4均无本轮代码变化；原工作区未操作。

## 3. 验收证据

详[事务设计](USB_BulkIN_Transaction_Design.md)、[Stop Proof](USB_BulkIN_StopProof_Contract.md)、[仲裁报告](USB_BulkIN_Arbitration_Report.md)、[测试报告](USB_BulkIN_Test_Report.md)。最终构建/回归证据以`p1b04`冻结源码快照及specialist.json为准；前三次构建快照随实施继续变化，被标为中间/被替代证据，不用于最终源码验收。

**NOT_TESTED**：任何烧录、USBPcap/实板插拔、SD真实读写、UVC实板流。本次不使用既往板级报告充当新实现证据。

## 4. 未解决问题及后续接口扩展候选

| 项目 | 当前受控行为 | 后续最小候选（INFERRED，未实施） |
| -- | -- | -- |
| 同步Core/旧Class调用 | VERIFIED：BUSY、stop_required及Class/引用保留，不等待ISR | Platform已单步推进；usb_mode.c:274～280及370～372仍将BUSY转ERROR。需另审Service的Pending/错误区分与重试，不改Core |
| 共享GRST窗口遇IRQ | VERIFIED：停止转RECOVERY_REQUIRED，其余IRQ继续 | 在HAL保持原样前提下设计更精确的共享命令仲裁；不能直接丢IRQ或放行不明Flush |
| 超时/硬件状态不明 | VERIFIED：保留fence、owner及buffer，无自动清零 | 需要可审计Reconcile/静止证明；目前没有允许复用的自动恢复条件 |
| ES0445 2.24.5 | UNRESOLVED：当前slave ZLP启动规避未实现 | 单独审查所有ZLP启动入口与时钟序列；涉及HAL/EP0时提交范围扩展申请，不在本轮越权 |
| 物理迟到事件/IRQ时间 | NOT_TESTED：只有软件模型 | 后续授权实板，测NAK/EPDISD/Flush、mask恢复、回调不重复及旧事件边界 |
| API兼容 | VERIFIED：五个旧Core用例保留FAIL | 后续迁移调用者；不能把旧断言改为忽略资源错误来制造通过 |

**UNRESOLVED（审批）**：若要求在现有禁止范围内同时完成全设备自动Stop/重枚举与所有旧同步调用语义，当前无法证明。对应自动协调实现已停止于失败保留边界，不绕过完成条件；本轮只提交可核验的Bulk IN机制及未完成清单。

## 5. 回滚与Git

**INFERRED（回滚方案）**：人工审核不接受时，可在该研究分支对本轮提交作普通revert并重新验证，不reset历史、不强推、不删除output/PDF、不改其他工程。本轮尚未执行回滚。

**VERIFIED（交付约束）**：提交只按明确路径暂存。最终提交号与远端核对结果见交付回复，文档不嵌入会导致自指循环的自身SHA。构建产物留output，不提交固件、PDF或依赖库。

**VERIFIED（MAP复核）**：中间p1b03默认固件把无调用者的三个停止入口裁除；因此补入范围内Platform主动Stop协调，并以p1b04重新完整编译，最终MAP须验证入口保留。不能用早期三配置成功替代最终版本验收。

Service扩展的具体源码与有限方案见[事务设计第7节](USB_BulkIN_Transaction_Design.md)。未实施调用者迁移，未宣称默认模式切换恢复。
