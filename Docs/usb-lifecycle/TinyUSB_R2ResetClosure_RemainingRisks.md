# TinyUSB R2-ResetClosure — 剩余门禁与审核项

**VERIFIED（软件）**：正常活动Reset已不再统一立即故障；完整AXF31项Reset测试证明显式硬件响应下可恢复，缺事件则失败保留。**NOT_TESTED（实板）**：不能据此宣称实际STM32H747活动Reset验收通过。

| 门禁 | 当前处理 | 状态/后续条件 |
| -- | -- | -- |
| RX Flush需在Reset窗口内完成 | 以USBRST/ENUMDNE正文语义门控，2ms/最多一次重试，raw ENUM提前到达即故障 | VERIFIED（源码/模型）；NOT_TESTED：实际UsbTask能否及时观察；延迟时安全拒绝而非承诺恢复 |
| OUT0不可disable | 依赖PIO fence、共享RX退休和fresh SETUP资格；不把EPENA=0当proof | INFERRED（RM组合契约）；NOT_TESTED：硅片SETUP/Flush边界 |
| 真实FIFO/事件时序 | 按RM事件顺序及W1C事实，模型缺事件分别锁存原因 | VERIFIED（软件）；NOT_TESTED：物理FIFO、迟到IRQ、持续负载 |
| 与旧取消/组关闭/Disconnect交错 | 不合并未知硬件所有权；进入fault并保留旧指针/Class | VERIFIED（负向模型）；正常重叠恢复仍UNRESOLVED，不强制重启 |
| 重复Reset发生在pending | 无法证明上一阶段已静止时失败保留；已完成后连续Reset/配置可成功 | VERIFIED（模型）；反复主机Reset压力NOT_TESTED |
| ES0445 Rev7 2.24.5 | 没有修改PIO ZLP时序或宣称消除勘误 | NOT_TESTED：EP0 Status、零长度IN等所有相关路径仍需独立门禁 |
| DMA、ISO、其他PHY/速度/动态时钟 | 本轮只为FS/PIO/线性Bulk/固定RAM配置闭环；不推广OUT0 PIO证明 | UNRESOLVED，未授权移植/验证 |
| FIFO布局变化 | 活动Reset保留已配置布局，避免新SETUP到达时重划 | VERIFIED（模型）；不同配置/复合/UVC的重新预算UNRESOLVED |
| Backend异步所有权 | 不修改MSC原生pending_io，真实完成才可复用；Reset失败也不替Backend清状态 | VERIFIED（RAM模型）；SDMMC/IDMA未接入、NOT_TESTED |
| Class回调与短锁 | Reset/组关闭reset回调移到锁外，状态与Status资格发布仍锁内 | VERIFIED（源码/AXF）；其他未来Class的open回调耗时没有泛化证明 |
| 历史断言迁移 | 原Closure25中1项、原R2中1项新Reset契约FAIL均保留；旧Disconnect/R1 FAIL保留 | VERIFIED：逐用例对照；人工审核后才决定测试契约迁移 |
| 故障恢复 | 保留首次reason/phase/寄存器快照、BUSY、Buffer和IRQ；无自动清fault/软重启冒充成功 | VERIFIED（模型）；故障后硬件静止与人工恢复策略UNRESOLVED |

## 错误诊断

VERIFIED：`reset_progress` 保存第一次失败，不用超时推导静止。reason20为ENUM早于RX proof；21为AHB空闲等待；22为RX Flush；23为NAK marker/effective；24为IN disable；25为OUT disable/RX空闲等未闭合；26为TX Flush；27为NAK释放；28为DWT/时钟无效；29为并发Reset/取消/旧组/所有权冲突；30为RX后出现外部NAK；31为非本FS速度；32为Reset中Disconnect；34为端点/PIO引用未退休；35为共享GRSTCTL命令冲突；36为未支持活动类型/FIFO源；37为无对应Reset的ENUM；38为累计1s期限。各值不是硬件状态本身，必须结合failed_phase、fault_gint/grst/dctl及等待/已完成掩码分析。

VERIFIED：所有等待均在原tud_task/DCD轮询中推进，IRQ不无界等待。DWT必须实际启用、持续前进且频率稳定；RTOS tick辅助累计期限。模型计数并不证明物理超时精度。时钟失败时保留资源，不用Watchdog或PCD重新初始化解锁。

## 集中审核结论

VERIFIED（软件）：已提供正常成功与有界失败两条路径、完整RAM固件和逐项证据，可以进入人工代码/契约审核。NOT_TESTED：是否允许实板验证由后续人工审批决定；本轮没有自动烧录或启用默认生产路径。

保持原分支历史、原默认工程/原P1分支、PDF和全部output；普通提交/推送本次授权范围后停止。不合并main，不进入SD/UVC移植或MSC BOT迁移。
