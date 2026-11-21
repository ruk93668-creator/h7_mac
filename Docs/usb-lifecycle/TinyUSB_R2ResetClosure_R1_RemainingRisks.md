# TinyUSB R2-ResetClosure-R1 — 剩余门禁

**VERIFIED（软件）**：原Reset END持锁无限等待及发布前清DCD引用的问题已修复；普通post-ENUM单次SETUP可在原生队列中处理。**NOT_TESTED（实板）**：没有自动烧录、物理USB操作或合并main。

| 项目 | 当前结论 | 门禁/后续 |
| -- | -- | -- |
| END/START队列31/32、32/32 | VERIFIED：实际AXF零等待，保留资源，过期事件可由同Task清理后重试 | 真实调度/负载NOT_TESTED |
| 全部是不可退休回调的满队列 | VERIFIED：保留回调、源、Class及pending，50ms发布阶段或累计期限受控fault；不任意执行/丢弃 | 不承诺无故障恢复；如需增强应另审原生队列容量/回调退休契约，不能加第二消费者 |
| fault之后 | VERIFIED：冻结原队列和首次错误；不强清fault、busy、source，不重启PCD冒充成功 | 无自动Reconcile；恢复仍须独立硬件静止证明 |
| post-ENUM新SETUP | VERIFIED（模型）：保存合法最新RX/DONE，END后处理一次请求，不依赖主机重发 | 实际硅片FIFO/握手及初始化时序NOT_TESTED |
| 活动EP0 IN的新SETUP抢占 | UNRESOLVED/BLOCKED：旧ISR停止路径仍拒绝，fault并保留source；新负向测试确认无新Status ZLP | 按用户第6条停止该成功路径，待人工审批；未写新的EP0停止协议 |
| Reset前/同窗早于RX Flush proof的ENUM | VERIFIED：沿用原保护受控失败 | 不为缩短延迟去掉Reset窗口证明 |
| PIO W1C | VERIFIED（源码/模型）：OUT端点IRQ不抢先清SETUP/XFRC；唯一FIFO消费者按状态退休 | 芯片事件时序NOT_TESTED；不宣称修复ES0445 ZLP勘误 |
| 旧Class回调被新Reset抢占 | VERIFIED（选定交错）：只收尾已证明的旧scope，旧最终提交不得覆盖新fence | 只验证当前RAM MSC；不泛化任意未来Class回调 |
| Group Close尚未proof时Reset | VERIFIED：保留原受控故障 | 未实现并发硬件停止合并；不宣称全部交错正常恢复 |
| Backend完成投递失败 | VERIFIED：Core锁存fault，MSC pending_io仍保留，不会作为退休成功 | 上游tud_msc_async_io_done仍忽略defer返回值；不能单独用它的true宣称事件入队，故障路径不提供正常恢复 |
| Task stop API | VERIFIED（源码）：满队列返回false，不等待自己 | 完整RAM AXF无调用者，符号被剔除，直接API运行NOT_TESTED |
| 其他OS / DMA / ISO / SD / UVC | 未移植、未扩展成功契约 | UNRESOLVED/NOT_TESTED；不推广PIO/RAM证明 |
| 历史测试 | VERIFIED：保留所有原断言、原始FAIL；Reset31有1项本轮语义变化，其他旧FAIL不变 | 人工审核后决定测试契约迁移，本轮不改断言 |

## 时基与资源独立性

VERIFIED：所有硬件等待继续沿用DWT/RTOS tick有效性、频率稳定性和累计期限；队列软件清理预算8条不代表硬件超时。发布阶段的新增reason39表示END入队期限不足；reason41表示不合法的新SETUP RX状态。首次phase/寄存器证据不覆盖。

INFERRED：Reset停止证明、事件被原队列接收、Class回调执行及Backend完成是不同事实；不能互相替代。USB PIO停止也不能代表未来SDMMC IDMA停止。本轮仅RAM，未操作真实SD。

NOT_TESTED：板上GSNPSID、USB物理Reset、Windows/USBPcap、DWT精度、FreeRTOS真实调度、ES0445 Rev7 2.24.5。历史REV_ID 0x2003仍只识别U/V组，不区分U和V。

## 人工审核边界

VERIFIED：已交付原生事件发布修复、正常枚举资格、选定抢占和故障保留证据，完成授权软件部分。BLOCKED子路径继续保留失败，不用模型假设“主机会重试”或“硬件自动静止”替代证明。

仅普通提交/推送新独立分支；保留PDF、用户Keil/RTE、所有output和历史证据。远端SHA核对以最终交付消息为准。完成后停止，等待集中审核；未经批准不烧录、不合并，不进入SD/UVC或MSC BOT迁移。
