# P1-B Bulk IN事务设计与实施边界

> P1-B基线：`5a27af621c219ead2bc680858efd4aeb17e110a4`；工作区：`D:/32/STM32H7/h7_mac_lifecycle`；分支：`codex/usb-device-lifecycle-manager`。
> VERIFIED注明源码/手册/软件模型；INFERRED为设计推导；UNRESOLVED为尚缺证明；NOT_TESTED为未执行的实板验证。本文不批准P1-C、EP0/UVC/MSC迁移或统一生命周期管理器。

## 1. 实际源码与上下文

| 路径 | 真实入口/关系 | 上下文与当前责任 |
| -- | -- | -- |
| `CM7/BSP/bsp_usb.c:202` | BSP_USB_BeginBulkInStop | Task校验身份，发布fence并屏蔽目标DAINT/TXFE位，保存原始CTL/INT/mask |
| `CM7/BSP/bsp_usb.c:270` | BSP_USB_PollBulkInStop | Task每次仅推进一个硬件阶段；无等待循环 |
| `CM7/BSP/bsp_usb.c:364` | BSP_USB_CommitBulkInRetirement | Task复核私有Proof后清本方向引用，恢复目标原mask |
| `CM7/BSP/bsp_usb.c:1006` | OTG_FS_IRQHandler→HAL_PCD_IRQHandler | ISR；Reset前保存/锁定旧引用；共享GRSTCTL拥有期间任何USB IRQ均使事务保守失败，再继续服务HAL |
| `CM7/BSP/bsp_usb.c:1026` | HAL_PCD_DataInStageCallback | ISR同步回调；Bulk XFRC累计completed_bytes，未停止时才提交下一包；fenced回调只记录故障 |
| `CM7/BSP/bsp_usb.c:1140` | HAL_PCD_DisconnectCallback→Platform回调 | ISR；旧会话失效，保留旧引用，不能当作退休 |
| `CM7/Platform/usb_device.c:240` | GetBulkInTransfer | Task在同一快照绑定BSP身份与已有s_in_sequence |
| `CM7/Platform/usb_device.c:282` | CommitBulkInRetirement | BSP提交成功且Platform序号匹配后才清busy；不重复普通Class回调 |
| `CM7/Platform/usb_device.c:1572`、`:1641`、`:2011` | Reset、Disconnect、ManagedClassDeInit | 同步Core/ISR路径发现引用即锁定失败，不驱动长轮询或伪造Class释放 |
| `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1992` | HAL_PCD_EP_Abort→USB_EPStopXfer | 旧同步调用，不是异步取消承诺；新三阶段接口不调用它 |
| `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:915` | USB_EPStopXfer | 旧IN仅SNAK/EPDIS/轮询EPENA，缺完整证明 |
| `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:2018`、`:1234` | wrapper Flush/EPDISD IRQ | 丢弃底层Flush失败；目标DAINT隔离避免新Stop交给这些路径 |
| `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:452`；`Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_usb.h:41` | 公开TX Flush与0xF000000迭代预算 | 返回真状态，但不满足本方案每次Poll的短步骤约束；HAL/LL原样保留 |

以上为**VERIFIED（源码）**。旧API仍可能从同步Core/ISR进入；新三个停止阶段明确拒绝IPSR非零。新路径没有NVIC Disable/Enable调用。旧USB_Device_Lock和其他类型路径未重构，不能宣称全USB所有ISR已无历史等待。

## 2. 身份及数据模型

**VERIFIED（源码/软件模型）**：BspUsbInIdentity_t包含controller_generation、session_generation、endpoint_generation、transfer_sequence、endpoint_address；StopAttempt为独立单调seed。seed在软件启动周期内不静默回绕，耗尽拒绝对应新事务；会话耗尽禁止新Bulk提交。每端点一个私有槽，无RTOS任务/动态内存新增。

**VERIFIED**：Platform原有s_in_sequence保持原语义。UsbBulkInStopTransaction_t在短原子快照将它与BSP身份关联；Begin与Commit核对Platform序号，BSP始终核对完整硬件身份及attempt。没有把两个独立序号假定为数值相同。BSP也有直接提交调用者，所以不依赖Platform内部变量。

**VERIFIED**：queued_bytes取代旧offset，completed_bytes仅在有效当前包XFRC路径增加，current_packet_length保留当前包长。queued不等于USB ACK，失败状态中的排队数据不作成功字节证据。source_owned、pool_owned、pio_users、fifo_complete分别记录应用源引用、PIO槽预留、BSP复制访客及硬件FIFO；不把一个active当作全部所有权。

**INFERRED（调用约束）**：同一USB任务串行调用新接口与BSP变更接口；其他Task/CM4不能并发直接操作控制器。只允许当前USB2 FS/PIO非零Bulk IN；DMA、ISO、OUT、EP0均不由本方案签发Proof。

## 3. 三阶段接口

1. **VERIFIED：BeginStop**验证身份/参数，分配attempt；短原子窗口保存寄存器证据并设置fence，清目标DAINTMSK和DIEPEMPMSK位；先保存再处理原始XFRC。已有正常完成返回ALREADY_COMPLETED；仍须Commit结束本次fence。
2. **VERIFIED：PollStop**按NAK→DISABLE（或当前包XFRC已结束）→AHB→FIFO→RETIRE→PROVEN推进。任务每次调用最多一个阶段；HAL_GetTick累计期限为1～1000ms参数，使用无符号差值。轮询接口不内部循环，不调用旧同步Abort或HAL Flush。
3. **VERIFIED：CommitRetirement**重新核对私有身份/attempt/阶段、当前寄存器、PIO访客、会话与待处理有效事件。成功才归还BSP引用、清PCD旧指针和Platform busy。重复Commit不重复归还；新Transfer后旧Commit被拒绝。

**VERIFIED（API语义）**：SysResult_t表示调用是否合法/可执行；事务进度由proof.result及stage给出。SYS_OK不能单独代表停止完成。PENDING表示尚缺证据，RECOVERY_REQUIRED表示保留现场；FAILED枚举保留给无进行中事务的失败表示，当前参数/身份错误直接返回工程错误码，不覆盖旧Proof。业务必须检查两个通道。

## 4. IRQ与临界区边界

**VERIFIED**：Begin/Poll/Commit中的寄存器/状态发布使用保存并恢复原PRIMASK的短临界区；Platform快照/Commit也采用嵌套保存恢复。硬件阶段等待发生在调用之间，不跨等待屏蔽USB IRQ；不修改全局NVIC使能。只更改目标端点对应的两个外围mask位，其他端点mask保留。

**UNRESOLVED（审查点）**：短PRIMASK窗口暂时屏蔽CPU可屏蔽中断，不能描述为“从未屏蔽USB中断”。若“禁止关闭整个USB IRQ”被解释为连原子发布窗口的CPU临界区也不允许，需要另评原子寄存器更新方案；本次没有用未经硬件证实的外设LDREX/bit-band替代。临界区最长实际用时、IRQ延迟均NOT_TESTED。

**VERIFIED（保守冲突策略）**：GRSTCTL是全局共享命令寄存器。FIFO阶段若任何USB IRQ进入，BSP先标RECOVERY_REQUIRED，保留source/pool，再继续HAL服务，避免把HAL内部其他Flush误认作本次成功。EP1正常业务不被全局关IRQ；但此极短窗口中的普通IRQ也会使EP2停止失败。此限制在BI24明确测试，不伪装成任意负载下可无故障停止。

## 5. 同步Core接入阻塞

**VERIFIED**：旧Abort/Close在Bulk引用仍被持有时返回BUSY，表示未完成同步取消，不承诺未来取消callback。BSP Stop在关IRQ前拒绝持有引用的情况。ManagedClassDeInit发现引用时设置stop_required、保留Class并返回BUSY；Reset/Disconnect同样保持锁定。没有迁移MSC/UVC Class，已在Platform主动Task Stop内接入Bulk IN单步推进，不在同一次调用循环等待；没有修改USB Task或Service状态机。

**VERIFIED**：`CM7/Platform/usb_device.c:1836`的Usb_DrainBulkInStops在主动Stop中保存事务、每调用推进一阶段并在Proof成立后Commit，PENDING返回BUSY；`CM7/Platform/usb_device.c:1872`的Usb_StopDevice只在这些引用退休后进入既有PCD/Class释放。

**UNRESOLVED**：端到端自动停止/重新枚举尚未接通；不能据此宣称MSC重连修复。后续最小接入候选是在允许的任务协调层保存transaction并周期推进、仅在Commit后重试同步释放；涉及现阶段禁止的USB Task/MSC/EP0迁移时必须另审。此处仅提交候选，不实施，不修改Core。

## 6. 适用硬件与证据边界

**VERIFIED（手册）**：RM0399 Rev5，60.15.6，2917～2918页；60.14.5，2784页；60.14.56，2842～2843页。PDF SHA256为`61d07b67deb291aed8a5cf8019c94add0a1065318542e57b3287d2694b14bd60`，原文件原地保留、不跟踪、不纳入提交。

**VERIFIED（勘误）**：ES0445 Rev7第1页表2，REV_ID=0x2003仅定位U/V组。2.24.2第43页限定Buffer DMA取包；不直接套当前PIO。2.24.5第44页覆盖slave的ZLP，需要单独审查发送启动时序。新停止路径没有实现其20/15 AHB周期的启动规避，也没有改HAL/EP0；不能宣称所有ZLP已经正确。这是UNRESOLVED的系统级发送限制，不以停止模型测试覆盖。

**NOT_TESTED**：板级NAK/Disable/FIFO进展、真实延迟事件边界、抓包、设备重连、外部DMA所有权。软件模型与手册推导不能升级为实板通过。

## 7. 已定位的Service接口扩展申请（未实施）

**VERIFIED（源码）**：`CM7/Service/usb_mode.c:274`调用USB_Device_Stop，`:276`将所有非SYS_OK（包括PENDING对应BUSY）在`:279`转ERROR；`:370`的USB_Mode_Stop也在`:372`将非OK直接转ERROR。因此默认模式流程不会继续驱动第一个Pending事务。当前保持真实错误/引用；不通过内部忙等或返回OK隐藏。

**INFERRED（最小申请）**：后续允许修改usb_mode.c上述两个调用点及对应测试，以独立进度证据区分已登记Bulk IN Pending与普通HAL_BUSY/RECOVERY_REQUIRED；仅Pending留在STOPPING，利用已有USB_Mode_Process周期继续调用，并设有限期限；成功后才WAIT_RECONNECT/IDLE。可在BSP/Platform补只读进度查询，不改Core，不新增Task。不把所有BUSY无条件吞掉。若其他调用者也依赖同步完成，须另列迁移，不借此扩大授权。

**UNRESOLVED（权限门禁）**：当前允许文件为BSP USB、Platform USB Device及测试/文档，Service迁移不在本轮范围。该调用者修改已暂停；本文件是具体申请，未获得批准前不改usb_mode.c、Task、MSC/UVC或EP0。
