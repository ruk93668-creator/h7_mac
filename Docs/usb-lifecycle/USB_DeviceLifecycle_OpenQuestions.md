# UDLM 未决问题、证据缺口与审批门禁

> 基线：`35401119bef1d5a4594925478119e048f68c2c77`；研究分支：`codex/usb-device-lifecycle-manager`；审计日期：2026-11-19。
> 范围：Phase 0，仅文档；未修改生产/测试代码，未编译或烧录。
> 标记：**VERIFIED**＝本基线源码确认；**INFERRED**＝推断或拟议设计，尚非已实现能力；**UNRESOLVED**＝证据/契约尚缺；**NOT_TESTED**＝本轮未运行验证。源码引用采用仓库相对路径和一基行号，均相对于上述 SHA。

## 1. 已确定的设计方向

**INFERRED**：推荐“短同步有界Abort + USB Task多轮Prepare + PreparedTicket同步CoreCommit”。保留ST Core、MSC BOT/SCSI、UVC Payload/描述符、Camera/Media、现有Task与BSP层次。异步化仅用于已经跨任务/硬件事实需要等待的部分；不无理由新增Task或heap。

**VERIFIED**：推荐依据为现有HAL Abort调用同步LL EPStopXfer（`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1992`）、BSP PIO续包（`CM7/BSP/bsp_usb.c:655`）、现有USB Task（`CM7/Application/Tasks/usb_task.c:119`）；不存在通用Bulk取消完成回调（HAL PCD `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1144`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1234`条件分支）。这只证明当前软件接口形态，**不证明现有同步Abort硬件顺序已经安全**。

## 2. 问题清单

下表所有未关闭项均 **UNRESOLVED**；“建议”是 **INFERRED**；“关闭证据”尚未取得。

| ID | 问题与真实源码依据 | 影响 / 建议 | 关闭证据与责任边界 |
| -- | -- | -- | -- |
| O1 | LL EPStopXfer仅SNAK/EPDIS和EPENA轮询；`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:915` | IN NAK生效、OUT global NAK、EP0/ISO例外未用RM0399证明；EPENA0不能单独定性 | 核验对应芯片RM/errata精确节/页/修订，OUT/IN/EP0/Bulk/ISO分别波形/快照；BSP方案需授权 |
| O2 | HAL_PCD_EP_Flush吞底层timeout；`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:2018` | 原BSP检查返回不构成Flush proof | BSP有界独立调用既有LL原语或等效可验证后置操作并保存raw result；若必须改HAL则单独申请 |
| O3 | HAL Stop/USBRST/IN EPDISD也吞Flush；同文件 `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1029`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1335`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1236` | 回调后仅读flush位清零不能还原历史timeout | BSP IRQ pre/post证据与具名独立flush proof；故障注入每一位置 |
| O4 | BSP Stop先关NVIC，任一Abort失败直接return；`CM7/BSP/bsp_usb.c:188` | 控制器可能仍started但IRQ已关；不能在Platform写寄存器绕过 | PCD Stop前proof齐全、失败保留可安全服务的IRQ/端点fence；BSP有限扩展审批 |
| O5 | Core cfg0吞DeInit失败且发status；`Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c:770` | Class返回BUSY不足以Task协调 | pre-Core标准Setup gate + LL status veto，Core原样；模拟/实板证明无假状态包 |
| O6 | 现有Deferred仅bm0x21/configured；`CM7/Platform/usb_device.c:1025` | 不能复用为通用标准请求延迟；PIO EP0等待新SETUP细节未证实 | Gate独立cookie/NAK/最新Setup重放契约；主机超时/取消/状态失败验收 |
| O7 | HAL USBRST先于ENUMDNE callback，数据IRQ顺序更早；HAL PCD `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1080`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1332`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1381` | 仅挪Platform Reset到Task会漏旧事件，软件地址与硬件不一定一致 | BSP入口session fence、raw snapshot、最新Reset提交和新地址一致性测试 |
| O8 | vbus_sensing关闭，LL条件启用OTG事件；`CM7/BSP/bsp_usb.c:123`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:436` | 无法保证拔线产生Disconnect；不能用SOF缺失区别Suspend | 审核V1板检测电路/可用信号及USB配置；不在本阶段改BSP/硬件；必要时独立申请 |
| O9 | HAL原始IRQ无token，BSP callback仅ep；`CM7/BSP/bsp_usb.h:27` | 旧IRQ可能被错贴当前arm identity | 新arm前完整停止/FIFO/事件退休fence；pre/post可观察性和竞争测试 |
| O10 | RAW Claim/Worker owner独立；`CM7/Service/usb_msc_sd.c:19`、`CM7/Service/usb_msc_storage.c:156` | endpoint proof不等于IDMA静止；不能自动开放FatFS | StorageTask具名proof port、IDMA诊断出口核验；保留RAW，SDMMC/FatFS不改 |
| O11 | Bridge INVALID_STATE也触发RETURN；`CM7/Application/app_uvc_bridge.c:315` | 通用新架构不能把丢Context当无借用 | retired Class/Payload proof + Frame lease ACK；必要App adapter改动单独授权，Camera/Media管线保留 |
| O12 | 当前Mode Stop非OK即ERROR；`CM7/Service/usb_mode.c:274`、`CM7/Service/usb_mode.c:362` | 新Pending语义不可偷偷加入旧Stop | 显式Request/Poll API和调用者逐批迁移/回归；只有一位执行者 |
| O13 | USB Task20ms、通知合并、StorageTask较低优先级；`CM7/Application/Tasks/usb_task.c:16`、`CM7/Application/app_events.c:268` | 短锁预算、Task公平性、EP0 deadline尚未测量 | cycle/time记录，真实UVC负载与Worker排空；不靠加Task/提优先级掩盖问题 |
| O14 | 固定Class池1024，拟议registry/tickets需空间；`CM7/Platform/usb_device.c:92` | 静态容量/retired数量/overflow策略需定量 | 不占Class池；独立静态预算、MAP/栈检查、耗尽故障测试；不默认修改Linker/MPU |
| O15 | 单Class包装、Core有条件Composite；`CM7/Platform/usb_device.c:1678`、`Middlewares/ST/STM32_USB_Device_Library/Core/Inc/usbd_def.h:55` | 注册多个participant不等于多Class枚举/联合传输 | 未来独立Composite任务：interface/IAD/EP0路由、RX/FIFO、FS带宽、ISO+Bulk实测 |
| O16 | BSP Init回收HAL DeInit结果被丢；`CM7/BSP/bsp_usb.c:130`；Core DeInit会清指针 | 部分Init失败可能留下硬件资源，不能由s_ready推定清理完成 | acquisition bitmap与原始cleanup error；所有部分失败点注入；回收失败保留记录 |

## 3. RM0399 核验记录

**VERIFIED（过程）**：本轮尝试访问ST官方RM0399，浏览器返回文档过大（约22MB）/超时，内存PDF请求也超时。未获得端点停止段落正文，因此没有编造节号/页码，也没有引用F4等其他芯片手册代替H747证据。

目标来源：[ST RM0399 官方PDF](https://www.st.com/resource/en/reference_manual/rm0399-stm32h745755-and-stm32h747757-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)。**UNRESOLVED**：实际采用的RM修订、硅片Revision与errata适用项，下一阶段需保存可校验的文档版本/哈希及精确节页。

**VERIFIED（源码而非硬件保证）**：本地CMSIS提供位号（`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29349`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29479`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29476`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29638`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29646`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:28600`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:28603`、`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:28618`），HAL/LL展示软件读写顺序。位定义不足以证明“某位在所有竞争下出现必然足够释放buffer”。所有硬件停止接受谓词继续标UNRESOLVED，实板NOT_TESTED。

需核验清单（**UNRESOLVED**）：IN NAK effective等待、EPDIS与EPENA约束、EPDISD与XFRC竞态/清除、ISO incomplete、OUT global NAK与sharedRX、EP0特殊控制/Setup接收、FIFO AHBIDL/Flush完成与所需等待、BusReset对数据端点/FIFO/地址的真实效果、关钟/软断开与pending IRQ的关系。

## 4. 不需要扩大改动的决策

**INFERRED**：

- ST Core可以保持源码不变，通过pre-Core gate/Managed adapter/LL status veto协调，但这是待实现验证的架构可行性，不能写“已解决并通过”。
- 继续使用USB Task；新模块普通C静态对象。暂不增加RTOS任务、不用动态内存，不改FreeRTOS/MPU/Linker。
- MSC协议算法、UVC描述符/Payload和Camera/Media管线保留。BOT Reset、Alt0、BusReset与fullStop各用准确scope。
- 取消后的buffer释放与控制状态资格分离；安全排空不会自动归还RAW/FatFS所有权。
- 超时、失败、证据缺失进入RECOVERY_REQUIRED；不能强制清busy/Free或无条件重启PCD来维持枚举表象。

## 5. 人工审核建议

**INFERRED**：本次可审核八文档的边界、依赖方向、七层模型、Ticket/ledger和迁移顺序。进入生产实施之前至少关闭O1–O9中的硬件/API门禁并明确授权BSP与需要的Application/Service adapter；其余按各迁移阶段关闭。若证据显示同步有界Abort不适合，再单独评审完整异步Cancel契约，不将它作为未证明的默认补丁。

**NOT_TESTED**：没有新固件、软件回归或实板结果，不能宣称重连故障已修复。Phase0完成表示审计和设计交付完成，**不表示这些未决硬件条件已解决，也不批准Phase1**。

## 6. 八文档阅读导航

- [真实调用链、源码指纹与失败分支](USB_DeviceLifecycle_SourceAudit.md)
- [分层、改造矩阵、API与EP0门控](USB_DeviceLifecycle_Architecture.md)
- [七层状态、事务scope与恢复条件](USB_DeviceLifecycle_StateModel.md)
- [Participant、PreparedTicket、Proof与Ledger](USB_DeviceLifecycle_ParticipantContract.md)
- [IRQ/PIO串行化、旧事件退休及共享FIFO](USB_DeviceLifecycle_Concurrency.md)
- [实施依赖、验收门禁与回滚](USB_DeviceLifecycle_MigrationPlan.md)
- [故障注入、BOT/UVC与三组实板矩阵](USB_DeviceLifecycle_TestMatrix.md)
