# TinyUSB R2-Closure 实施报告

基线：`daa6d7c82412c883555fd8c6d3fd9a9f80f0c4a3`。分支：`codex/usb-tinyusb-p1-replacement`。日期：2026-11-21。

**总体 BLOCKED：活动 Bus Reset 的硬件退休证明未完成。已完成的正常关闭/Disconnect/Restart路径通过完整AXF软件模型；不等于实板或全部交付门槛通过。旧R2 19项中保留一项契约冲突FAIL，未降低原断言。**

## 本轮实际变化

| 文件 | 实施内容 |
| --- | --- |
| `ThirdParty/tinyusb/src/device/dcd.h`、`usbd.h` | VERIFIED：追加可选 `dcd_edpt_close_all_safe()` 四态结果、`dcd_task()` 和 `dcd_deinit_ready()`；旧API不删除 |
| `ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c` | VERIFIED：非零线性Bulk组提交Fence，复用原逐EP Abort，现有Task有界推进，退休后二次硬件检查；控制端点不走普通OUT Disable |
| `ThirdParty/tinyusb/src/device/usbd.c` | VERIFIED：DeInit前置证明；配置请求延迟状态；Disconnect延后Class reset；SETUP序号与当前EP0上下文分别保留；没有重写BOT/EP0协议 |
| `ThirdParty/tinyusb/src/portable/synopsys/dwc2/dwc2_common.c` | VERIFIED：资源静止后才进入Core关闭；CSRST失败恢复先前GINT admission，不关闭PHY、不伪报成功 |
| `CM7/TinyUSB/tinyusb_experiment.c` | VERIFIED：原UsbTask内添加可选调试命令3/4调用软Disconnect/Connect；默认命令0不会触发，未自动执行实板动作 |
| `MDK-ARM/h7_mac_TinyUSB_RAM.uvprojx` | VERIFIED：保留R2独立配置，构建输出目录更新；未改原默认工程/用户uvoptx |
| 测试、上游元数据、文档 | VERIFIED：新增完整AXF专项及调用者/源码/产物收据；保留原测试断言与失败历史 |

[本轮实际生产Diff](../../Tests/TinyUSBReplacement/reports/R2Closure_production.patch)、[固定上游累计差异](../../Tests/TinyUSBReplacement/reports/R2Closure_upstream.patch)、[全部新API及相关接口引用](../../Tests/TinyUSBReplacement/reports/R2Closure_callers.json)。固定TinyUSB上游仍为 `da5b55a5803a3b9ca788ccc82569db699c01aa74`，52个选取文件的原始Git blob已复核。

## 1. DeInit顺序

VERIFIED：`usbd.c:646` 的tud_deinit先拒绝现有DCD Pending和MSC async IO，保留IRQ/队列/Class；随后通过 `usbd_close_group()` 取消非零Bulk，只有SUCCESS及 `dcd_deinit_ready()` 均满足才调用dcd_deinit。Pending继续由原tud_task推进，不能理解为已释放。EP0活动且未正常完成时拒绝DeInit，允许原IRQ完成其正常请求；不主动EPDIS OUT0。

VERIFIED：`dcd_dwc2.c:818` 中dcd_deinit再次检查静止，实际Controller Reset成功后才断开pullup、清DCD引用；Core随后才关闭NVIC并删除Class/队列/Mutex。`dwc2_core_deinit()` 仅在已证明端点不需要IRQ推进后暂时屏蔽GINT以Reset；失败恢复原GINT位，保留故障与Class，不关PHY，不清busy/假装STOPPED。

VERIFIED（模型）：活动IN/OUT可直接请求DeInit → PENDING → 原Task取消 → 再次DeInit成功 → Init → 枚举/配置/INQUIRY。CSRST缺失时真实函数有界失败，NVIC没有Disable写，GINT恢复、pullup不被提前移除、Class字节保持。已经成功退休的USB源可合法归还；不能把它们表述为在Controller失败后仍在途。

## 2. 活动组与配置关闭

VERIFIED：`dcd_edpt_close_all_safe()`先审计作用域类型，原子关闭所有非零新提交，然后逐方向驱动R1B已有Transfer generation/源指针、NAK、Disable、FIFO证据。没有StopAttempt、Resource Ledger或第二个生命周期协调器。状态仅是DCD内部组Fence/期限及原EP取消状态。

VERIFIED：同步轮询不等待中断；50ms累计Tick/DWT期限、时钟有效性及停走/频变检查有界拒绝。失败不清旧引用。成功路径不Flush共享RX，不修改EP0分配；最后由既有已退休组close做二次检查再关闭非零端点。Global OUT NAK仍由原唯一owner串行处理；EP1/EP2同时退出专项已覆盖。

VERIFIED：INEPNE不携带PIO源引用。仅当该IN Transfer已有phase5（Disable及目标TX Flush均完成）且source归还时，readiness可忽略其NAK通知；XFRC、EPDISD及错误事实仍不忽略、不强清。来源是实际 `handle_epin_slave()` 的事件处理分支，不以一个EPENA位作停止证明。

VERIFIED：Core在组开始前废除非零旧完成事件资格。`SET_CONFIGURATION`保存原请求和原生SETUP序号；PENDING不调用Class reset、不发送Status ZLP。成功后才切换配置并提交一次合格的Status。新SETUP可以替换待处理配置请求；非配置新请求使旧Status失去资格。

VERIFIED：旧配置退休不能擦掉较新的描述符请求。收尾保存/恢复当前ctrl_xfer、EP0 claim及generation，序号复核和合格Status提交在同一短临界区串行化；中途Disconnect/故障会重新检查并否决。实际新描述符完成后能进入其正确OUT状态阶段。

## 3. 正常Disconnect

VERIFIED：SEDET只立即撤销旧会话SETUP/完成事件资格并登记原生UNPLUGGED事件；它不是资源退休。Class reset延至同一tud_task中的非零Bulk证明完成，并核验EP0正常完成/无活动PIO后执行。工作进行中不关USB IRQ、不重启Controller、不强清fault。

VERIFIED：软Disconnect也使用同一原生Task收尾，证明完成后才移除pullup。Pending时Connect被拒绝；完成后的Connect及合法空闲Bus Reset/配置可接收新CBW。物理拔线无需写Controller SDIS来冒充证明。

VERIFIED：Class reset保留TinyUSB MSC既有pending_io及Buffer持有语义，延期Backend完成回调到安全的Class状态再运行；新配置不得在旧IO完成前收CBW。DeInit比取消配置更严格，Backend不静止就完全拒绝退出。没有修改MSC协议源码或原RAM Backend。

## 4. 尚未闭合的Bus Reset

BLOCKED：仍保留活动EPENA/RXFLVL、取消与Reset交错的安全拒绝。未把USBRST本身当成功，也未通过模型自动清位补齐活动Reset。

VERIFIED（手册）：RM0399 Rev5 60.15.6 p2903给出Reset后的EP0初始化；p2918的Reset型IN停止是EPDIS→EPDISD→定向TX Flush；p2910–2911的Reset型OUT停止包含全组使能、AHB idle、共享RX Flush（小于10ms且最多一次重试）、Global OUT NAK和活动OUT禁用。60.14.61 p2846明确控制OUT0不可EPDIS。USBAEP因Reset清除不等于EPENA/PIO/RX已退休。

UNRESOLVED：Reset型全组OUT步骤中EP0例外范围、新SETUP与共享RX Flush窗口、旧PIO/事件所有权的完整适配尚不能证明。本轮没有用普通Bulk取消替代这套序列。模型仅验证无法证明时保留Class，**没有新增活动Reset成功用例**；该交付要求未达到。

## 完整固件与边界

VERIFIED：Keil ARMCC5完整RAM MSC重编链接：**0 Error(s), 0 Warning(s)**。Code=47918，RO=1086，RW=316，ZI=25788。输出：`D:\32\STM32H7\h7_mac_lifecycle\output\TinyUSBReplacement\R2Closure-review-749a21eb3ea4`，AXF/MAP/HEX及build.log均保留。

AXF SHA256：`749a21eb3ea461ab52ac8bea5e71caf39a13fc5ea78d1de7065bc33c289e9b19`。向量117=`0x8002d89`，唯一TinyUSB强IRQ；无HAL_PCD_IRQHandler、旧USB_Mode、StorageTask、动态分配符号。源码哈希、接口真实链接地址和完整证据见[R2Closure_receipt.json](../../Tests/TinyUSBReplacement/reports/R2Closure_receipt.json)。

NOT_TESTED：全部实板和USBPcap、物理插拔、硅片/RTOS真实并发。只使用原静态UsbTask、现有协议和RAM LUN；未启用旧默认工程的新路径，未引入旧UDLM，未修改原P1分支、CM4、HAL/LL/ST Core、SD/FatFS、相机或UVC，未烧录/合并main。整体BLOCKED，等待人工集中审核。

VERIFIED（公共接口契约）：tud_disconnect现在是Task上下文的可延迟请求；false表示尚未完成或故障，调用者保持原USB Task推进，不能据false归还资源。实验Task是选取源码中唯一调用者，已按Pending语义重试。tud_connect在未Init、关闭Pending或故障时拒绝，避免invalid rhport访问；头文件原“always true”注释已修正。其他平台只保留弱接口兼容，不声明全面验证。
