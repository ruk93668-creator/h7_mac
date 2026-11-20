# P1-B XFRC与Stop仲裁报告

> P1-B基线：`5a27af621c219ead2bc680858efd4aeb17e110a4`；工作区：`D:/32/STM32H7/h7_mac_lifecycle`；分支：`codex/usb-device-lifecycle-manager`。
> VERIFIED注明源码/手册/软件模型；INFERRED为设计推导；UNRESOLVED为尚缺证明；NOT_TESTED为未执行的实板验证。本文不批准P1-C、EP0/UVC/MSC迁移或统一生命周期管理器。

## 1. 唯一完成权

**VERIFIED**：每端点单私有槽，Begin持有fence后普通BSP回调不得继续推进。原始事件在W1C前被保存，当前包有xfrc_claimed去重。普通已完成传输与COMPLETED_UNDELIVERED分别处理，避免把取消收尾伪装成协议成功回调。

| 输入 | 仲裁结果 | 资源处理 |
| -- | -- | -- |
| XFRC已被正常处理并全部完成 | ALREADY_COMPLETED | VERIFIED：普通路径已归还一次，Commit不再加计数 |
| 中间包已处理，下一包在途 | 只承认已处理包completed_bytes | VERIFIED：queued_bytes包含下一包，Stop后不再续包 |
| 中间包XFRC未处理 | Stop接管当前XFRC | VERIFIED：当前包完成一次，停止多包传输，不交普通Class |
| 最后包XFRC未处理 | COMPLETED_UNDELIVERED | VERIFIED：保存完整完成事实，等待全部Proof与Commit |
| Stop后无XFRC | 正常禁用路径 | VERIFIED：等NAK/Disable/FIFO和退休，不虚构成功字节 |
| 本attempt禁用后XFRC与EPDISD并存 | 记录两位、当前包一次 | VERIFIED：两事件不互相替代，不重复归还 |
| Begin前已有未归属EPDISD | RECOVERY_REQUIRED | VERIFIED：不能把旧禁用标志当本次完成 |
| late XFRC在旧事务退休后、新Arm前出现 | 拒绝Arm，受控故障 | VERIFIED：不给新buffer贴上旧完成 |
| 旧身份/旧StopAttempt在新Transfer后调用 | INVALID_STATE | VERIFIED：不修改新source/pool/busy |
| 无法区分的硬件事件在新Arm以后出现 | UNRESOLVED | 软件无法仅靠generation辨别原始位；依赖正确硬件屏障及后续实板证据，不宣称已注入验证这种物理来源 |

## 2. 失败与上下文

**VERIFIED（源码）**：BSP旧HAL状态映射保留：HAL_BUSY→SYS_ERR_BUSY，HAL_ERROR→SYS_ERR_IO，HAL_TIMEOUT→SYS_ERR_TIMEOUT。旧Abort有引用时的BUSY是未执行同步取消，不是异步Pending。新Poll的PENDING在proof.result内，不能与工程BUSY混为一谈。

**VERIFIED（软件模型）**：NAK、禁用、FIFO超时均保留两个引用；异常提交/续包失败也保持Platform busy。Reset/Disconnect/共享命令竞争不自动清任何引用，旧会话身份仍可诊断但不能重新提交。Slot进入FAULT后没有自动Reconcile或直接PCD重启。

## 3. 历史失败处理

**VERIFIED**：保留原17个入口及修改前报告`Tests/EndpointLifecycle/reports/baseline-regression-p1b-before.json`。旧fixture中offset→queued_bytes仅机械标识符替换；预期常量、断言数量/顺序不变。MSC共用fixture只补HAL_GetTick链接边界，不拿这个固定时钟测试新停止时间。

**VERIFIED**：新增Core旧用例失败334、335、361、362、364分别要求同步Close/Abort完成或未知失败后直接复用；新契约保留BUSY/引用。它们继续报告FAIL，不改断言换绿。其余逐用例及最终计数见Test_Report；既有UVC历史失败和MSC枚举缺链接依赖均保留。

**UNRESOLVED**：这意味着并非所有历史行为回归通过；主动Platform Stop已分阶段推进，但Service把Pending转ERROR；旧同步API调用者仍须后续迁移到任务三阶段接口，相关范围未扩展到Class/USB Task。本轮不宣称P1-B整体验收通过或修复完整重连。

## 4. 验证范围与模型局限

**VERIFIED（软件模型）**：ARMCC编译真实BSP/Platform代码，Unicorn注入寄存器变化；W1C及SNAK写命令按显式模型处理，保存每步身份、寄存器、计数、引用及mask。链接桩计数任何意外硬件/Core调用。Reset与GRST冲突用真实BSP IRQ前置路径，HAL IRQ主体在这些场景是计数桩；Disconnect到真实Platform guard。

**NOT_TESTED**：完整真实HAL IRQ+Core+MSC任务的全部互锁时序、真实PHY事务、禁用后硬件事件管线的最长迟到时间、实板重连及UVC活跃ISO流。测试模型不证明芯片一定按模型在指定读取时刻进展。
