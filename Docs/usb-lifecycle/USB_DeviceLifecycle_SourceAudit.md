# USB Device Lifecycle 源码审计

> **P1进度（2026-11-19）**：本轮为未完成/受阻交付，生产代码未改；下面原有内容是Phase0历史设计。当前结果见本文末尾及三份EndpointLifecycle报告。

> 基线：`35401119bef1d5a4594925478119e048f68c2c77`；研究分支：`codex/usb-device-lifecycle-manager`；审计日期：2026-11-19。
> 范围：Phase 0，仅文档；未修改生产/测试代码，未编译或烧录。
> 标记：**VERIFIED**＝本基线源码确认；**INFERRED**＝推断或拟议设计，尚非已实现能力；**UNRESOLVED**＝证据/契约尚缺；**NOT_TESTED**＝本轮未运行验证。源码引用采用仓库相对路径和一基行号，均相对于上述 SHA。

## 1. 基线、证据边界与阅读范围

**VERIFIED**：新 worktree 起始 HEAD 与指定 SHA 一致，分支正确，起始 `git status --short` 为空。首次远端查询中 `codex/usb-msc-bringup` 指向该 SHA，研究分支尚不存在；后续网络查询曾连接失败，交付时必须重新核对。所有命令限定新 worktree，未操作原工作区。本次事实来自实际函数体与失败分支，不以历史报告代替源码。

**VERIFIED**：必读文件的审计范围如下。Core 的 Composite 条件分支也已检查；HAL 的本次深入追踪范围是 Device 模式与 PCD 传输/IRQ/停止路径，LL Host/HCD 算法不是 UDLM 设计依据。

| 文件 | 关键结构/状态 | 阅读后的生命周期结论 |
| -- | -- | -- |
| `CM7/Platform/usb_device.c`、`.h` | `USBD_HandleTypeDef`、`UsbDeviceEndpointState_t`、托管 Class 状态、Deferred Token、静态 Class 内存 | 公开控制 API 要求 Task，Core Class 回调仍可能同步在 ISR 执行 |
| `CM7/BSP/bsp_usb.c`、`.h` | `PCD_HandleTypeDef`、`UsbTxTransfer_t`、RX active、PIO 池 | 硬件/PIO 所有者；只有同步 Abort，没有通用取消完成回调 |
| `CM7/Service/usb_mode.c` | Mode Context、STARTING/STOPPING/WAIT_RECONNECT/ERROR | 已有模式策略与重连时间管理；不能再新增平行策略状态机 |
| `CM7/Service/usb_msc_class.c` | `s_in_open`、`s_out_open`、`s_configured` | 部分初始化和释放有逐方向标志，DeInit 先 BotStop 再 Close |
| `CM7/Service/usb_msc_bot.c` | `s_bot`、epoch、request token、control token、静态数据区 | 任务处理 BOT；Class Reset 已有异步 Worker 排空与 EP0 状态续接 |
| `CM7/Service/usb_uvc_class.c` | `UsbUvcContext_t`、全局 Payload、packet token、IN sequence | Alt0 同步 Close 后放弃 Payload；Task 取消也可 Abort 后放弃 |
| `CM7/Service/usb_uvc_payload.c` | `UsbUvcPayload_t`、FRAME/PACKET/CANCELLING | Cancel BUSY 仍持有帧；AbortPacket 本身不提供硬件证明 |
| `CM7/Application/Tasks/usb_task.c` | 静态 TCB/stack、任务通知、诊断 | 已有所有者任务，处理 MSC/UVC 和 App Stop |
| `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c` | Core handle、Class callbacks、EP0 state | Reset/Disconnect/DeInit 均同步调用 Class；部分返回值丢失 |
| `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c` | Setup request、SetConfig/StdItf/StdEP | SET_CONFIGURATION(0) 丢失 Class DeInit 失败；ClearHalt 先状态包再 Class 通知 |
| `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c` | EP0 长度及状态 | IO helpers 忽略 LL 返回，不能作为硬件提交成功证据 |
| `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c` | PCD IN/OUT ep、IRQ flags、PCD state | Abort 同步轮询；Flush/Stop 会隐藏底层 Flush 失败 |
| `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c` | Endpoint CTL/INT、FIFO/全局掩码 | Abort 检查 EPENA，DeActivate 不等待，StopDevice 会检查 Flush 返回 |

**VERIFIED**：补充阅读 `app_usb.c`、`app_events.c`、`app_messages.c/.h`、`app_uvc_bridge.c`、`app_camera_lifecycle.c/.h`、`control_task.c`、`storage_task.c`、`runtime_event.c`、`usb_msc_storage.c`、`usb_msc_sd.c`，并读取 SD Block/SDMMC 的租约、IRQ、WaitBatch、StopIo/Recover/Quiescent 函数，以验证跨模块所有权边界。

## 2. 九条入口的真实调用链

下列编号是本文证据索引；括号中的 ISR/Task 是实际入口上下文；“同步回调”不会自动切换线程。函数首次出现的引用为定义位置，随后引用可指关键调用语句。

### E1 主动 Stop / 模式请求重连

**VERIFIED** 调用链：

- Control Task 收到 STOP/FAULT → `ControlTask_Command(CONTROL_STOP_USB)`，`CM7/Application/Tasks/control_task.c:169`、`CM7/Application/Tasks/control_task.c:247` → USB_COMMAND 队列。
- USB Task `UsbTask_Process`（Task，`CM7/Application/Tasks/usb_task.c:119`）→ `App_USBHandleCommand`（`CM7/Application/app_usb.c:46`）标记 stopping → UVC 时 `USB_UVC_CancelFrame`、`USB_UVC_Process`、`App_UvcBridgeUsbProcess(false)` → `App_USBFinishRound`（`CM7/Application/app_usb.c:124`）。后者只有 Bridge idle 才调用 `USB_Mode_Stop`。
- `USB_Mode_Stop`（`CM7/Service/usb_mode.c:362`）→ `USB_Device_Stop`（`CM7/Platform/usb_device.c:370`）→ `Usb_StopDevice`（`CM7/Platform/usb_device.c:1729`）→ **BSP Stop 在 Class DeInit 之前** → `Usb_ManagedClassDeInit` → `Usb_ReleaseEndpoints` → 重配 FIFO → 成功后清理软件端点/Core 状态。
- 另一 Task 路径：`USB_Mode_RequestReconnect`（`CM7/Service/usb_mode.c:167`）只允许相同模式 → `USB_Mode_Process`（`CM7/Service/usb_mode.c:214`）STOPPING 调 Stop；成功后 WAIT_RECONNECT，500 ms 后 Start。任何 Stop 非 OK 均转 ERROR，并未实现 Pending Stop 协调。

**VERIFIED**：BSP Stop 在 `CM7/BSP/bsp_usb.c:195` 先禁用 OTG_FS IRQ，随后逐方向 `HAL_PCD_EP_Abort`、共享 RX Flush、HAL Stop。任一失败直接返回，IRQ 没有恢复；软件 `s_started` 和传输记录仅最后成功时清除。Platform 在 `CM7/Platform/usb_device.c:1748` 保留 stop_required/hardware_stop_failed，禁止假装 Class 已释放。

**INFERRED**：上述顺序可在端点仍不静止时留下无 IRQ 的故障控制器；需要 UDLM Prepare 在最终 PCD Stop 之前完成资源证明。不能声称每次失败都在等待取消 IRQ：本 HAL Abort 自身是同步轮询，不依赖通用取消回调。硬件失败根因与恢复条件见 E9 和 OpenQuestions。

### E2 USB Bus Reset

**VERIFIED**（ISR）：`OTG_FS_IRQHandler`（`CM7/BSP/bsp_usb.c:619`）→ `HAL_PCD_IRQHandler`（`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1049`）。HAL 在 USBRST 分支（同文件 `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1332`）清端点中断、设 OUT SNAK、清硬件 DCFG.DAD、准备 EP0；**到 ENUMDNE 分支 `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1381` 才调用 ResetCallback** → `HAL_PCD_ResetCallback`（`CM7/BSP/bsp_usb.c:685`）→ `Usb_Reset`（`CM7/Platform/usb_device.c:1481`）。

`Usb_Reset` 失效控制 Token、清两个 EP0 软件记录、设 FS → `USBD_LL_Reset`（`Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:771`）先将 Core state/config 改 DEFAULT/0，再同步托管 Class DeInit，再 Open EP0 OUT/IN → Platform `Usb_ReleaseEndpoints`。Class 路径：MSC `Msc_DeInit` → BotStop/Close；UVC `UVC_DeInit` → Alt0/ReleaseFrame/Free。

**VERIFIED**：Core 忽略两次 EP0 Open 返回，Platform 补查 opened 和 reset_result/release_result；失败设置 stop_required。Core Reset 未赋值 `dev_address=0`，HAL USBRST 清 DAD，但未同步赋值 `hpcd->USB_Address=0`；新 Init 才清该 HAL 字段（HAL `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:220`）。因此调试软件地址不等于 DCFG 真实地址。

**INFERRED**：Reset 已由硬件发生，不可在 ISR 等 StorageTask；需先围栏旧 Session，再任务 Prepare，之后受控调用原 Core Reset 同步提交。仅把 ENUMDNE 回调挪到 Task 不足以覆盖 HAL 更早处理的旧 XFRC/RXFLVL。

### E3 Disconnect

**VERIFIED**（ISR，若事件产生）：HAL OTGINT/SEDET 分支（HAL PCD `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1486`）→ `HAL_PCD_DisconnectCallback`（`CM7/BSP/bsp_usb.c:713`）→ `Usb_Disconnect`（`CM7/Platform/usb_device.c:1543`）失效 Token → `USBD_LL_DevDisconnected`（Core `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:995`）改 DEFAULT 并同步 Class DeInit → Platform `Usb_ReleaseEndpoints`。返回失败锁定 stop_required；Connect 仅 `USBD_LL_DevConnected`，Core 该函数无状态恢复动作（Core `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:981`）。

**VERIFIED**：本 BSP `vbus_sensing_enable=DISABLE`（`CM7/BSP/bsp_usb.c:123`）；LL `USB_DevInit` 仅在 sensing 开启时启用 SRQIM/OTGINT（`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:436`），关闭时强制 B-session valid（同文件 `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:312`）。

**UNRESOLVED**：不能保证物理拔线必定触发 Disconnect 回调；无 SOF 也可能是 Suspend。未来若需要确定的物理断线检测，必须先证明板级检测来源/授权边界。不能以“未收到 Disconnect”推出“连接仍在”。

### E4 SET_CONFIGURATION(0)

**VERIFIED**（ISR 同步）：HAL Setup helper（HAL PCD `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:2306`）→ BSP `HAL_PCD_SetupStageCallback`（`CM7/BSP/bsp_usb.c:626`）→ `Usb_Setup`（`CM7/Platform/usb_device.c:1321`）→ `USBD_LL_SetupStage`（Core `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:546`）→ `USBD_StdDevReq`（`Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c:104`）→ `USBD_SetConfig`（同文件 `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c:723`）→ CONFIGURED/cfg0 分支 `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c:770` → state ADDRESSED/config0 → `USBD_ClrClassConfig`（Core `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:505`）→ Managed/Class DeInit → `USBD_CtlSendStatus`（`Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c:171`）。

**VERIFIED**：SetConfig 丢弃 ClrClassConfig 的失败，继续状态包；UVC 在 DeInit 失败时有 SET_CONFIGURATION 特定 CtlError（`CM7/Service/usb_uvc_class.c:532`），不能替 MSC 或通用 Lifecycle 做完整门控。普通 Class BUSY 会走 Platform 错误路径。DeferControlStatus 仅接受 bmRequest=0x21、零数据长度、已配置、正在 Class Setup 的请求（`CM7/Platform/usb_device.c:1025`），不能直接覆盖标准请求。

**INFERRED**：必须在 Core 修改状态之前截获破坏性标准请求，Task Prepare 后只重放一次原 Core Setup；Commit 若失败，LL 层必须否决状态包，不能依赖 Core 返回值。

### E5 UVC SET_INTERFACE Alt0

**VERIFIED**（ISR）：同 E4 到 Core Setup → `USBD_StdItfReq`（ctlreq `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c:167`）→ `UVC_Setup`（`CM7/Service/usb_uvc_class.c:553`）验证请求 → `UVC_SetStreamAlt(ctx,0)`（同文件 `CM7/Service/usb_uvc_class.c:1003`）：CANCELLING → `USB_Device_CloseEp(0x81)` → Platform LLClose → BSP Close/Abort/Flush → `UVC_ReleaseFrame`（同文件 `CM7/Service/usb_uvc_class.c:1105`）→ 清 ep1_opened/alt1/streaming → Core 仅 Class 返回 OK 才发状态。

**VERIFIED**：Close 失败保留 ep1_opened/旧 Alt，并设 close_pending；不执行 ReleaseFrame。Alt0 仅 VS 接口/ISO 端点范围，不应 DeInit 整个 Class 或停止 Controller。Alt1 重新打开 EP1，仍保留 Probe/Commit 协议实现。

### E6 MSC BOT Class Reset

**VERIFIED**：ISR Setup → `Msc_Setup`（`CM7/Service/usb_msc_class.c:111`）精确匹配 0x21/0xFF/wIndex0/wLength0 → `USB_Device_DeferControlStatus` → `USB_MSC_BotReset`（`CM7/Service/usb_msc_bot.c:262`）保存 Token、cancel backend、尝试两个 Abort（此处忽略返回）→ RESET_DRAIN → 返回 USBD_BUSY。Platform 仅对登记且匹配的 WAITING/BUSY 延迟，否则 CtlError（`CM7/Platform/usb_device.c:1394`）。

Task `USB_MSC_BotProcess`（BOT `CM7/Service/usb_msc_bot.c:276`）在 USB IRQ lock 内执行状态机：RESET_DRAIN 检查 Token/2000 ms 超时；重试双端点 Abort 并等 backend drained → CompleteControlStatus → RESET_STATUS；真实 EP0 IN 回调完成后 RESET_CLEAR → 两方向 ClearHalt 齐全才 epoch++/重新收 CBW（BOT `CM7/Service/usb_msc_bot.c:366`）。Core 标准 ClearHalt 在 Class 通知前已提交状态（ctlreq `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c:324`）；现有 WasControlStatusComplete 用于相邻请求间保留最近完成证据。

**VERIFIED**：BotStop 只 active=false、cancel backend、清控制资格、BOT_OFF（BOT `CM7/Service/usb_msc_bot.c:232`），不等 Worker/IDMA。`s_bot.data[1024]` 是全程静态缓冲（BOT `CM7/Service/usb_msc_bot.c:35`），重启 BOT_START 在 drained 和端点 idle 后才再用（BOT `CM7/Service/usb_msc_bot.c:286`）。**INFERRED**：保持此排空门槛，并使生命周期证明独立于 Token，不能将协议 Reset 当作 Bus Reset/Controller Stop 或 SD 同步。

### E7 UVC 活跃 ISO 流停止

**VERIFIED**：Task 主动停止路径 E1 → `USB_UVC_CancelFrame`（`CM7/Service/usb_uvc_class.c:315`）只设 CANCELLING；`USB_UVC_Process`（同文件 `CM7/Service/usb_uvc_class.c:366`）→ PayloadCancel（`CM7/Service/usb_uvc_payload.c:236`）。PACKET/CANCELLING 返回 BUSY，仍持帧；Task 再 AbortEP1，只有 SYS_OK 才 UVC_ReleaseFrame。ISO incomplete ISR → BSP 标 transfer.failed（`CM7/BSP/bsp_usb.c:727`）→ Platform → Core IsoINIncomplete → `UVC_Incomplete`（UVC Class `CM7/Service/usb_uvc_class.c:1090`）只转 RECOVERY，不直接释放。

正常完成：HAL XFRC → BSP DataIn（`CM7/BSP/bsp_usb.c:633`）→ Platform 记录 IN 完成序号 → `UVC_DataIn`（UVC `CM7/Service/usb_uvc_class.c:1063`）匹配序号记录 packet token → Task PayloadCompletePacket（Payload `CM7/Service/usb_uvc_payload.c:204`）；Frame EOF 后才完成帧计数。

Frame Lease：`App_UvcBridgeUsbProcess`（`CM7/Application/app_uvc_bridge.c:268`）持 LOANED；无 frame_owned 或 Class status INVALID_STATE 后进入 RETURN；`Bridge_UsbReturn`（同文件 `CM7/Application/app_uvc_bridge.c:484`）入队成功仍非归还完成；Camera `Bridge_CameraRelease`（同文件 `CM7/Application/app_uvc_bridge.c:456`）调用 CameraCapture_StreamReleaseFrame 成功后发 RETURN_ACK；USB 匹配 stream_epoch/lease_id/command_id 后才 IDLE（同文件 `CM7/Application/app_uvc_bridge.c:280`、`CM7/Application/app_uvc_bridge.c:425`）。

**INFERRED**：INVALID_STATE 不能成为通用的新 UDLM 归还证明；必须另有旧 Class 退役/端点停止/Payload 无借用证据。现有局部约定需通过适配器加强，不能重写帧管线。

### E8 Init 失败与部分资源回收

**VERIFIED**：Platform Init → `USBD_Init`（Core `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:92`）→ LLInit → `BSP_USB_Init`（`CM7/BSP/bsp_usb.c:83`）→ Clock/PCD Init/FIFO；失败时 BSP 调 HAL DeInit **忽略结果**（BSP `CM7/BSP/bsp_usb.c:130`）。HAL Init 各硬件阶段失败设 ERROR（HAL PCD `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:175`）；不能以 s_ready=false 证明硬件已回收。

RegisterClass 校验描述符/规划 FIFO 后安装 Managed callbacks（`CM7/Platform/usb_device.c:1627`），单 Class 固定池 1024 字节（同文件 `CM7/Platform/usb_device.c:92`）。`Usb_ManagedClassInit`（同文件 `CM7/Platform/usb_device.c:1827`）失败设 RELEASE_FAILED/stop_required，不假装回滚成功。

MSC Init（`CM7/Service/usb_msc_class.c:59`）IN 成功/OUT 失败或 BotStart 失败，会 Msc_DeInit；每方向 Close 仅成功清标志，回滚函数结果未另行上报，原 Init 返回 FAIL。UVC Init（UVC `CM7/Service/usb_uvc_class.c:478`）固定池分配后填上下文，Alt1 Open 另行发生。

Managed DeInit（Platform `CM7/Platform/usb_device.c:1855`）保护硬件失败/重入；Class 失败恢复 pUserData，保留 Class 内存；Class 成功进入 RELEASING，公共端点关闭失败可后续继续，无须重复 Class Free；最终确认 free_pending 后释放固定池（Platform `CM7/Platform/usb_device.c:1876`、`CM7/Platform/usb_device.c:1905`）。**INFERRED**：UDLM 需保留这种逐资源进度，而非统一 memset。

### E9 PCD Stop / DeInit

**VERIFIED**：Task `Usb_StopDevice` → BSP Stop（BSP `CM7/BSP/bsp_usb.c:188`）→ 每个开过的端点 HAL Abort（HAL `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1992`）→ LL `USB_EPStopXfer`（LL `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:915`）。IN/OUT 都写 SNAK、EPDIS，轮询 EPENA 最多 10000 次，失败返回 **HAL_ERROR**，没有 Pending/取消完成通知，也不检查 EPDISD。

BSP Stop 后调用 HAL_PCD_Stop（HAL `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1021`），该函数禁用全局 USB 中断、置软断开、**忽略全 TX Flush 返回**、返回 OK（HAL Lock BUSY 例外）。BSP 随后清 NVIC pending、成功才清 PIO 记录。BSP DeInit（BSP `CM7/BSP/bsp_usb.c:232`）先 Stop，再 HAL_PCD_DeInit（HAL `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:239`）→ `USB_StopDevice`（LL `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:1112`）掩码并清端点中断，检查 RX/TX Flush 结果 → MSP DeInit 关 IRQ/时钟/GPIO。失败不会清 s_ready/callbacks。

Core `USBD_Stop`（Core `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:400`）忽略 LL Stop/Class DeInit 后总返回 OK；Core `USBD_DeInit`（Core `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:149`）忽略 Stop/Class 返回并清指针，再 LLDeInit。**VERIFIED**：当前 CM7 主动 Stop 绕开 USBD_Stop 而走 Usb_StopDevice；未搜索到 CM7 的直接 USBD_DeInit 调用。Platform LLDeInit 有失败恢复指针保护（Platform `CM7/Platform/usb_device.c:573`），但不是 PreparedTicket。

## 3. 跨入口所有权、等待和停止证明

| 对象 | 持有/完成事实（VERIFIED） | 尚欠条件（INFERRED / UNRESOLVED） |
| -- | -- | -- |
| USB IN 原缓冲 | BSP transmit 保留 source，逐包复制进 PIO 池；BSP `CM7/BSP/bsp_usb.c:498`、`CM7/BSP/bsp_usb.c:655`，全部续包完才上报 Class | 单个 XFRC 仅是一包；需阻止 ISR 续包、退休旧硬件事件后才能回收 source |
| USB OUT 缓冲 | HAL RXFLVL 直接向 OUT_ep.xfer_buff 读 FIFO，HAL `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1080`；BSP `CM7/BSP/bsp_usb.c:675` 回调清 RX active | IRQ mask 不是硬件停止；共享 RX FIFO 内遗留数据及旧回调必须处理 |
| USB DMA | BSP `CM7/BSP/bsp_usb.c:108` dma_enable=DISABLE，当前是 PIO | 不可据此推出 SD IDMA 也已停；未来 USB DMA 是另一个能力版本 |
| MSC Worker | Storage mailbox QUEUED/RUNNING/DRAIN/DONE；`CM7/Service/usb_msc_storage.c:86` Cancel 只直接清 QUEUED/DONE；`CM7/Service/usb_msc_storage.c:156` quiescent 后才 DONE/EMPTY | Waiter/Token 取消不释放 RUNNING buffer。drained(epoch) 是局部后台证据，不是所有系统资源证明 |
| SD IDMA | `CM7/Service/usb_msc_sd.c:105` 检查 io.active，必要时一次 Recover；`CM7/BSP/bsp_sdmmc.c:957` Quiescent 检查 IDMAEN、DPSMACT/CMDACT、CMDTRANS；`CM7/BSP/bsp_sdmmc.c:1029` 另等 HAL READY、DCOUNT0、Card TRANSFER | 软件 active0 需要追溯到这些出口；未实板证明迟到 IRQ/硬件型号时序 |
| RAW 租约 | `CM7/Service/usb_msc_sd.c:19` Claim RAW；`CM7/Platform/sd_block.c:42` SD_TEST 拒 FATFS；Release `CM7/Platform/sd_block.c:76` 必须原 owner 且无 active/fault；MSC adapter 无 Release 调用 | USB 生命周期不能自动交还 FatFS；需独立媒体所有权阶段，Phase0 不实现 |
| UVC Payload/Frame Lease | Payload Cancel BUSY 保留 frame；Payload AbortPacket `CM7/Service/usb_uvc_payload.c:261` 仅验证 Token；Bridge ACK 后 IDLE | 端点安全证明与 Camera 归还 ACK 缺一不可；EOF 与取消归还不可混淆 |
| Class Context | Platform 托管池与 free_pending/RELEASING | 旧回调需 cleanup-only 路由；Class ACTIVE 过滤不能替代端点退休证明 |

**VERIFIED**：USB Task 静态创建、优先级4、常规等待20 ms（`CM7/Application/Tasks/usb_task.c:16`）；StorageTask 独立、优先级1、常规等待100 ms（`CM7/Application/Tasks/storage_task.c:13`、`CM7/Application/Tasks/storage_task.c:43`）。BSP USB IRQ 优先级6（BSP `CM7/BSP/bsp_usb.c:12`）。USB_Device_Lock 仅禁 OTG_FS NVIC 并恢复原 enable 位（Platform `CM7/Platform/usb_device.c:1171`），不是全系统锁。BOT Process 和 UVC Process 目前都在该锁内做协议处理/数据复制/Abort；不可将未来跨任务等待塞进该锁。

**VERIFIED**：App USB 通知统一压成 USB_PROGRESS bit（`CM7/Application/app_events.c:268`），FreeRTOS eSetBits 可合并。`app_messages.c:193` 生命周期队列发送要求 Task owner；不能从 USB ISR 用它代替持久事务记录。已有 Camera 消息身份与 pending/reply/replay 工具（`CM7/Application/app_camera_lifecycle.c:112`、`CM7/Application/app_camera_lifecycle.c:172`、`CM7/Application/app_camera_lifecycle.c:206`）可借鉴，**没有证据表明现有 USB Task 已消费这些 Camera 生命周期队列**，不宣称统一管理器已经存在。

## 4. Flush 与硬件证据审计

**VERIFIED**：HAL_PCD_EP_Flush（HAL `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:2018`）对 USB_FlushTxFifo/RxFifo 都 `(void)`，然后 HAL_OK。LL FIFO 例程（LL `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:452`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:489`）会等待 AHBIDL，再轮询 TXFFLSH/RXFFLSH，可能 HAL_TIMEOUT。`HAL_USB_TIMEOUT=0xF000000` 为迭代上限（`Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_usb.h:41`），不是已标定的毫秒预算。

| 吞掉结果的位置（VERIFIED） | 影响（INFERRED） |
| -- | -- |
| HAL PCD `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:2024`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:2028`，EP_Flush | BSP 返回 SYS_OK 仍不能证明 FIFO flush 完成 |
| HAL PCD `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1029`，PCD Stop | 最终 Controller Stop 不能只信 HAL_OK |
| HAL PCD `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1236`，IN EPDISD IRQ | 若事后仅读 GRSTCTL，不能还原这次 flush 是否超时；需在 BSP 边界独立可追踪操作/后置证明 |
| HAL PCD `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1335`，USBRST | Bus Reset 的清理证据可能在回调前丢失 |
| HAL PCD `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1800`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1829`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1861`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1902` | Open/Close/Transmit/Receive 也不传播 LL 返回；不能把 HAL_OK 泛化为所有硬件成功 |

**VERIFIED（仅位定义）**：CMSIS `Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29349` IN EPENA bit31；`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29638` OUT EPENA bit31；`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29346` EPDIS bit30；`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29479` IN EPDISD bit1；`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29646` OUT EPDISD bit1；`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:29476` IN XFRC bit0；`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:28600` RXFFLSH bit4；`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:28603` TXFFLSH bit5；`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:28618` AHBIDL bit31。

**VERIFIED（源码行为）**：HAL OUT EPDISD 分支 `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1144` 与 IN EPDISD `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1234` 清除事件，仅 iso_incomplete 条件触发 ISO 错误回调；不存在普通 Bulk Cancelled 回调。HAL 先处理 RXFLVL/OEPINT/IEPINT，之后 USBRST；BSP 必须在 HAL 消费前保存必要旧事件证据，post-IRQ 快照用于确认消费后的状态，不能用清零后的标志冒充“从未发生”。

**UNRESOLVED（硬件语义）**：不能将 EPDIS=1 当完成，EPENA=0 单独也不能区分取消/正常完成/Reset。拟议 proof 需结合唯一传输身份、停止方式、匹配 EPDISD 或正常完成账目、FIFO 与 IRQ/PIO 围栏。STM32H747 EP0 特例、IN NAK 生效/OUT 全局 NAK 顺序和关钟状态必须用 RM0399 对应器件版本核验并实板测试。本轮官方 PDF 抓取超时/浏览器大小限制，未读取这些段落；不引用其他 STM32 手册来替代。

官方目标文档：[ST RM0399](https://www.st.com/resource/en/reference_manual/rm0399-stm32h745755-and-stm32h747757-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)。**NOT_TESTED**：本轮无寄存器实测、无 USBPcap、无端点取消/重连实板试验。

## 5. 单 Class 与 Composite 边界

**VERIFIED**：Platform RegisterClass 固定 classId0/NumClasses0，保存一个 callbacks/driver（Platform `CM7/Platform/usb_device.c:1678`）；Core 非 Composite 注册写 pClass[0]（Core `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:202`）。Core 有 USE_USBD_COMPOSITE 条件分支，其默认类数由 `Middlewares/ST/STM32_USB_Device_Library/Core/Inc/usbd_def.h:55` 控制；当前 `CM7/Platform/usbd_conf.h:6` 只设置最大接口索引1，没有启用该宏。UVC 使用接口0/1、0x81，MSC 使用接口0/Bulk 0x82/0x02（MSC Setup、UVC Setup 和本文端点调用已确认）。

**INFERRED**：Participant Registry 应支持多个 participant，但不等于已经实现 Composite；当前仍是单 Class 模式。Composite 需要单独的接口重编号/路由、描述符、共享 EP0/RX FIFO、FIFO 和 Full-Speed 带宽验收，不能以“有空端点编号”判定通过。

## 6. 本轮验证范围

**NOT_TESTED**：未执行构建、运行旧测试、板卡试验或编写测试代码。下列设计文档中的测试均为未来计划，不能计为 PASS。

**VERIFIED**：文档交付检查应验证八文件完整、源码引用存在且行号非空、源码快照与基线一致、Git 仅八文档变更；提交推送结果以最终报告核对值为准。

## 7. 源码内容指纹

**VERIFIED**：下表由本 worktree 文件字节计算；可用于对照行号所在版本，非固件 SHA。

| 文件 | 行数 | SHA256 |
| -- | -- | -- |
| `CM7/Platform/usb_device.c` | 1982 | `b1c972c3fc1c0877715632557c39b46e72bd9f110b8624dcec6d613ded59e7d4` |
| `CM7/Platform/usb_device.h` | 166 | `f37bb27c4701d41a8bfd31449370691fcdac9edc7ea134e9b6bbbe2972480c1a` |
| `CM7/BSP/bsp_usb.c` | 895 | `0c5126d639e1b505ecad5ba1b5344d2463da87d2bc74af4a3d20b7918cb2b778` |
| `CM7/BSP/bsp_usb.h` | 104 | `7a9d16e9803b6bd896129c767b8732409569a816b1e1c466855a6d345f430531` |
| `CM7/Service/usb_mode.c` | 374 | `cd30b83881abd7ff14e11e2b354592e203d2126e2957b3394f48e439e4a32cc4` |
| `CM7/Service/usb_msc_class.c` | 186 | `b7fa3dea9bf8333857ca07f55494453af191110944fb1e4776369dc34c72abec` |
| `CM7/Service/usb_msc_bot.c` | 397 | `59cf0aaeaf0dd2360178d30546baab761b5fe25e67387caba15d9592f06b1f48` |
| `CM7/Service/usb_uvc_class.c` | 1126 | `a3bd3d84b6a4bfa5234bd71aaafdef2e58b0bcc1f711a9620a51a7848823ade3` |
| `CM7/Service/usb_uvc_payload.c` | 334 | `05ac143e5b61c662007efc3565d840080bbf26c991eff4650bf199a78c08aab6` |
| `CM7/Application/Tasks/usb_task.c` | 223 | `c88133b93188fd3a8ef096d8df9dafd632e5a2fe85719fb5944f9fb4fec3bdf3` |
| `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c` | 1215 | `4cb3e3487e2cb6b474ea632fc5d4b1da6e58b5e4d8f6302495d87c5bd17f8c1a` |
| `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c` | 1058 | `5f1c211ed991e074589c82bc52571357a268f36776111d99a85539df53fd80f0` |
| `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c` | 224 | `26fab50d3392b6a2b05acf1e08b8c809e0bd98c010cbf32690f158c95f715760` |
| `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c` | 2346 | `f1c48624654428a73a196bcd83b7b8477a0b6cebb1a229088090982f957823fa` |
| `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c` | 2257 | `e18900b0d91d79d8eea02d60eec865ea0eada440a053354c81b709c33f5e890a` |

## 8. 端点包装/回调的完整跨层定位

下表为 **VERIFIED**。所有箭头均是同步调用；若从 E2–E6/E7 IRQ 进入，则不会因为跨层调用变成 Task。

| 操作 | 实际跨层函数与定义位置 |
| -- | -- |
| Open | USB_Device_OpenEp `CM7/Platform/usb_device.c:414` → USBD_LL_OpenEP `CM7/Platform/usb_device.c:641` → BSP_USB_OpenEp `CM7/BSP/bsp_usb.c:251` → HAL_PCD_EP_Open `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1766` → USB_ActivateEndpoint `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:577` |
| Abort | USB_Device_AbortEp `CM7/Platform/usb_device.c:426` → BSP_USB_AbortEp `CM7/BSP/bsp_usb.c:337` → HAL_PCD_EP_Abort `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1992` → USB_EPStopXfer `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:915` |
| Close | USB_Device_CloseEp `CM7/Platform/usb_device.c:459` → USBD_LL_CloseEP `CM7/Platform/usb_device.c:706` → busy非零端点先BSP Abort，再BSP_USB_CloseEp `CM7/BSP/bsp_usb.c:276`（非零端点再次执行硬件Abort/Flush）→ HAL_PCD_EP_Close `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1812` → USB_DeactivateEndpoint `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:654` |
| Flush | USB_Device_FlushEp `CM7/Platform/usb_device.c:471` → USBD_LL_FlushEP `CM7/Platform/usb_device.c:753`（busy/其他OUT busy拒绝）→ BSP_USB_FlushEp `CM7/BSP/bsp_usb.c:411` → HAL_PCD_EP_Flush `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:2018` → LL USB_FlushTxFifo/RxFifo `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:452`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:489` |
| Bulk/ISO IN | USB_Device_Transmit `CM7/Platform/usb_device.c:483` → USBD_LL_Transmit `CM7/Platform/usb_device.c:859` → BSP_USB_Transmit `CM7/BSP/bsp_usb.c:498` → HAL_PCD_EP_Transmit `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1884` → USB_EPStartXfer `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:743`；末包IRQ → BSP DataIn → Usb_DataIn `CM7/Platform/usb_device.c:1430` → USBD_LL_DataInStage `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:681` → Class DataIn |
| Bulk OUT | USB_Device_Receive `CM7/Platform/usb_device.c:495` → USBD_LL_PrepareReceive `CM7/Platform/usb_device.c:914` → BSP_USB_Receive `CM7/BSP/bsp_usb.c:540` → HAL_PCD_EP_Receive `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1843` → USB_EPStartXfer；完成IRQ → BSP DataOut `CM7/BSP/bsp_usb.c:675` → Usb_DataOut `CM7/Platform/usb_device.c:1410` → USBD_LL_DataOutStage `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:586` → Class DataOut |
| ISO incomplete | BSP HAL_PCD_ISOINIncompleteCallback `CM7/BSP/bsp_usb.c:727` → Usb_IsoInIncomplete `CM7/Platform/usb_device.c:1565` → USBD_LL_IsoINIncomplete `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:930` → UVC_Incomplete；不是正常完成 |
| Class回收 | Usb_ManagedClassDeInit `CM7/Platform/usb_device.c:1855` → Msc_DeInit `CM7/Service/usb_msc_class.c:84` 或 UVC_DeInit `CM7/Service/usb_uvc_class.c:512` → Usb_ReleaseEndpoints `CM7/Platform/usb_device.c:1190`；成功后固定池Free提交 |
| Controller启动/撤销 | USB_Device_Start `CM7/Platform/usb_device.c:350` → Usb_StartDevice `CM7/Platform/usb_device.c:1698` → USBD_Start `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:384` → USBD_LL_Start `CM7/Platform/usb_device.c:613` → BSP_USB_Start `CM7/BSP/bsp_usb.c:169` → HAL_PCD_Start `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:996`；LLDeInit `CM7/Platform/usb_device.c:573` → BSP_USB_DeInit `CM7/BSP/bsp_usb.c:232` → HAL_PCD_DeInit `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:239` |

## 9. 九入口的等待/所有权/失败横向对照

前四列为 **VERIFIED** 源码事实（定位 E1–E9 和上表），末列为 **INFERRED** 的安全边界评价。IRQ lock均指短NVIC操作，不等于中断回调完成证明。

| 路径 | 上下文/等待 | IRQ与Abort/Close/Flush | 失败后资源/跨任务事实 | 现有安全收尾能力与缺口 |
| -- | -- | -- | -- | -- |
| E1 主动Stop | Task；UVC可跨多轮等待；进入BSP后同步轮询 | BSP先关IRQ，逐EP Abort/RX Flush/PCD Stop，后Class Close | BSP失败保留started/PIO；Platform硬件失败锁定；Worker Cancel不等排空 | 有stop_required及Class部分回收；无统一prepare/proof，IRQ失败路径欠缺 |
| E2 BusReset | ISR同步Core DeInit，端点Abort会轮询 | HAL先硬件Reset处理；Class Close/Flush；Core开EP0 | Core已改DEFAULT/config0；失败锁定；SD可能仍运行、RAW保留 | 不在ISR等Worker是事实，但旧资源需要Task收尾；无ticket |
| E3 Disconnect | ISR同步DeInit/Close，端点Abort会轮询 | 无先Task Prepare；不等同BSP Stop | Class失败可保留Context/端点；Core DEFAULT；Worker继续排空 | 失败保留状态，但回调可能不产生且统一恢复缺失 |
| E4 cfg0 | Setup ISR同步DeInit | Close/Abort/Flush；Core随后发status | Core已ADDRESSED/config0；MSC open失败标志仍持有；Worker Cancel非完成 | 有局部保留，无通用status veto，存在假成功风险 |
| E5 Alt0 | Setup ISR同步Close/轮询 | EP1 Close/Abort/Tx Flush；不是Controller Stop | 失败保留ep1_opened/close_pending与Payload；成功后Payload取消 | 局部失败保护存在；完整硬件proof/Frame ACK不在此同步函数内 |
| E6 BOT Reset | Setup ISR尝试Abort；Task多轮等backend/drain/EP0确认 | 两Bulk Abort；ClearHalt；不PCD Stop | 原1024数据buffer静态保留；Worker RUNNING不取消释放 | 有Token/timeout/排空链；Abort/Flush证据尚需加强，超时保持Fault |
| E7 活跃UVC停止 | Task多轮Cancel/Abort/Process；Camera ACK异步 | Abort EP1；设备Alt/Stop另决定Close | Cancel BUSY仍持packet/frame，Abort失败RECOVERY；Bridge等ACK | 已有Lease握手；INVALID_STATE推断需retired proof补强 |
| E8 部分Init失败 | 启动/Task或Class Setup ISR；HAL Init可能Delay，Class分配不wait | 资源按阶段获得；BSP回收HAL DeInit；MSC部分Close | 回收失败可有硬件残留；Managed Class保持RELEASE_FAILED | 无完整acquisition ledger；HAL回收失败可能被隐藏 |
| E9 PCD Stop/DeInit | Task；同步Abort/FIFO轮询；不等普通Cancel回调 | NVIC/global mask、soft disconnect、FIFO；DeInit最后关钟 | 失败保持ready/callbacks或部分mask，不能当STOPPED | 需要逐阶段proof/IRQ顺序，不能用HAL_OK或超时替代静止 |

## P1 本轮证据与状态更新

**VERIFIED（源码/软件）**：以9d38e56重新审读BSP Stop/Abort/Close/Flush/PIO与HAL/LL入口；生产内容未改变。新增真实HAL/LL刻画18项/108条断言，证实HAL Flush/Stop吞timeout、HAL Abort只看EPENA且不负责旧INT退休。证据：`Tests/EndpointLifecycle/reports/hal_contract.json`。**UNRESOLVED**：这些事实不证明正确的H747停止序列；RM正文和实物REV_ID仍缺。

详见 [P1实施/阻塞报告](USB_EndpointLifecycle_ImplementationReport.md)、[硬件证据](USB_EndpointLifecycle_HardwareEvidence.md)、[软件验证](USB_EndpointLifecycle_SoftwareVerification.md)。
