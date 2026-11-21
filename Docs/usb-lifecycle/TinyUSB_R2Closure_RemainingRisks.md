# TinyUSB R2-Closure 剩余门禁

**本轮不是完整Closure通过：活动Bus Reset仍BLOCKED，旧19项中保留1项契约变化FAIL；实板全为NOT_TESTED。**

| 条件 | 状态 | 结论 |
| --- | --- | --- |
| 活动非零Bulk正常取消配置/重配置 | VERIFIED（软件模型） | 新Task延迟状态与身份核验，无共享RX Flush；异常保留资源 |
| EP0正常完成后的活动Bulk Disconnect/重连 | VERIFIED（软件模型） | 复用逐EP取消；未用Controller Reset伪装断开 |
| 正常DeInit/Restart及硬件失败顺序 | VERIFIED（软件模型） | 先Backend/端点证明；成功后才NVIC/PHY关闭，失败恢复GINT且Class不清 |
| EP0仍活动的DeInit | VERIFIED（安全拒绝） | 等待正常控制完成，不主动取消OUT0；不承诺整个Device停止的有限墙钟期限 |
| 活动Bus Reset及与组关闭交错 | BLOCKED | 仍保留EPENA/RXFLVL/取消冲突保护，没有正常活动Reset成功证明 |
| 空闲Reset/EP0初始化/配置 | VERIFIED（软件模型） | 不表示硅片FIFO能在ISR首次读取时立即完成；实板时序NOT_TESTED |
| 旧R2 Disconnect故障预期 | CONTRACT_CHANGED，FAIL保留 | 新语义是Pending而非立即fault；本轮不迁移旧断言 |
| PIO ZLP勘误、EP0主动取消 | UNRESOLVED | 不声称已修复ES0445，不把普通Bulk流程套到EP0 |
| ISO、DMA、HS、其他DCD、Host | NOT_TESTED/未资格化 | 新闭环资格限H747 FS Slave PIO线性Bulk和原RAM LUN；保留弱兼容接口不等于全部平台验收 |

## 活动Reset为什么仍停住

VERIFIED：本地批准保留的RM0399 Rev5 PDF SHA256为 `61d07b67deb291aed8a5cf8019c94add0a1065318542e57b3287d2694b14bd60`。正文核查使用 `output/MSC24/udlm-rm0399/pages2747-2924.txt`，既有validation.json记录3535页/178页提取及页码一致性。PDF未改动、未移动、未跟踪。

- VERIFIED（RM）：60.15.6 p2903规定Reset后EP0中断、FIFO、STUPCNT=3初始化；不能据此推出所有旧事务已停止。
- VERIFIED（RM）：p2918 Reset型IN停止要求EPDIS、EPDISD、定向TX Flush完成；普通IN的SNAK/INEPNE路径与之不同。
- VERIFIED（RM）：p2910–2911 Reset型OUT要求全组使能、AHBIDL、共享RX Flush（小于10ms，受EP0新包影响时最多一次重试）、Global OUT NAK、活动OUT禁用及事件完成。
- VERIFIED（RM）：60.14.61 p2846禁止对OUT0执行EPDIS；p2847/2906–2908的SETUP接收不能简单通过普通OUT NAK关闭。
- UNRESOLVED：全组Reset流程中的OUT0例外、新SETUP资格和Flush期间共享RX状态消费者如何同时满足上面规则，尚无完整实现证明。不能把USBAEP因复位清除、单个EPENA=0或模型自动清空FIFO当作全组退休。

因此未删除现有保护，也未将普通Bulk取消调用包装成Reset成功。源码 `handle_bus_reset()`仍保守拒绝活动硬件，Core/Class保持旧状态，直到有真实停止依据；无自动清fault、强制复位或Watchdog恢复。

## 其他范围限制

UNRESOLVED：活动EP0在拔线后缺少正常完成证明时保持故障，不能把本轮“Bulk活动且控制面已静止”的重连结果推广到所有控制阶段。新SETUP资格保护已覆盖配置请求与较新描述符；并未验收所有Class控制请求/Composite场景。

VERIFIED：RAM Backend异步Buffer与USB端点引用分别管理。没有SD/IDMA接入、FAT挂载或RAW租约转换；不能根据USB proof释放未来的SD DMA Buffer。没有第二套Lifecycle Manager、额外USB Task或旧UDLM移植。

NOT_TESTED：Windows/USBPcap、物理Bus Reset与拔插、DWT/时钟精度、硅片IRQ竞态和持续负载；没有自动烧录。完整固件可供人工审核，不代表可以跳过上述硬件门禁启用生产。完成已能证明的软件部分后提交推送并停止，不合并main、不修改原P1分支。
