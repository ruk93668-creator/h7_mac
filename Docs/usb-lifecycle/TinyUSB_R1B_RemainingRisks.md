# TinyUSB R1B 剩余风险与启用门禁

**整体状态：BLOCKED（生产启用/完整硬件验收）。VERIFIED的软件Bulk正向路径不等于整机已经可用。**

| 项目 | 状态 | 已完成与剩余条件 |
| --- | --- | --- |
| 非零Bulk IN/OUT线性buffer取消 | VERIFIED（源码/ARM模型） | 正常SUCCESS、异常FAILED、PENDING不释放；硅片时序NOT_TESTED |
| DWT时间提供者 | UNRESOLVED（集成） | 必须已启用且自由运行，SystemCoreClock有效稳定；未启用/停走即FAILED。生产目前没有新加DWT启动代码 |
| 任务调度期限 | UNRESOLVED | 50ms从首轮取消累计，但需要Task再次调用才能观察失败；已有tud_task轮询承担推进，未引入独立定时器 |
| FreeRTOS集成 | BLOCKED | 组件0错误57历史警告，pdTICKS_TO_MS/xSemaphoreCreateMutexStatic缺口未解决；完整新固件未链接、IRQ优先级未实测 |
| EP0共享RX/整组关闭 | BLOCKED | close_all保守fault，不假Flush成功；SET_CONFIGURATION换配置/取消配置不能宣称正常恢复 |
| EP0活跃IN被新SETUP抢占 | BLOCKED（正常恢复） | 无立即停止证据则ISR有界拒绝、保留资源；未实现完整异步EP0续接 |
| PIO ZLP ES0445门禁 | UNRESOLVED | 保留原有顺序，不能把Status提交或ZLP寄存器观察当作勘误修复 |
| Legacy非零OUT Stall | BLOCKED（可用性） | 无共享RX退休证明即保守拒绝，合法取消仅走新Bulk异步路径；协议错误恢复仍可能锁死整个实例 |
| ISO/UVC活跃流 | NOT_TESTED | 仅验证旧停止等待有界及失败不继续激活；无ISO正常退休资格、无完整UVC移植 |
| DMA / HS / FIFO-ring | NOT_TESTED或拒绝 | 本轮SUCCESS限FS Slave/PIO线性Bulk；USB DMA、SD IDMA的停止证明均独立，不凭USB取消释放它们 |
| 其他MCU/多控制器/Host | BLOCKED（移植） | 其他DCD弱兼容保留，未全面编译；Host hcd_dwc2.c:1591仍忽略新增deinit bool，启用Host前需补齐传播 |
| Reset/Disconnect竞争 | VERIFIED（保留失败） | 取消在途遇Reset/Disconnect终态FAILED，不自动清fault重开；无实板重连成功声明 |
| DeInit失败恢复 | UNRESOLVED | 未完成Reset时保留source，不自动重新初始化。非PENDING的DeInit失败可能已关IRQ/断开，必须另行证明控制器静止和恢复资格 |
| 资源资格 | UNRESOLVED（硬件） | 模型验证NAK marker、EPDISD、RX空和PIO引用；需真实硬件证据排除不符合假设的迟到事件 |

VERIFIED：未通过任何clear busy、清fault、强制重启或Watchdog制造取消成功；失败保留事实，后续无通用Recovery API。本轮也未把所有USB操作异步化。状态锁存牺牲可用性以保留所有权，不是生产恢复机制。

VERIFIED：原R1两项回归仍FAIL，证据见SoftwareVerification；这是旧stub契约/未映射DWT夹具问题的分类结论，不是删除失败。新33项验证修改后的实际源码；旧六项挂死证据与所有output保留。

NOT_TESTED：所有实板、USBPcap和Windows验收。未烧录、未物理插拔、未进入SD/UVC完整移植、未合并main、未修改旧P1生产分支。下一步必须集中人工审核本轮实际Diff与以上门禁；不能根据软件全绿批准生产启用。
