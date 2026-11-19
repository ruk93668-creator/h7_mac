# MSC-4.1 停止生命周期审计与接口扩展申请

日期：2026-11-19（Asia/Shanghai）。审计HEAD：`98b9117f00a9d148c4f62cecaf371fe083e60348`，分支`codex/usb-msc-bringup`。本报告回应最新的停止状态机要求；**尚未实现整改，不能宣称修复或验收通过**。

## 1. 最新权限及已保存工作的处理

用户曾批准BSP有限扩展，随后最新任务明确规定“禁止修改BSP”“如果发现必须修改未授权模块，停止并提交申请”。以最新范围为准。

此前获准但尚未提交的BSP诊断改动已完整保存至本地 `output/MSC41/superseded-bsp-diagnostics-20261119/`，包含原文件、SHA256与范围变化清单；只将Codex本轮自有改动恢复到HEAD，未覆盖用户Keil修改。相关工具/测试结果也一并归档，避免将BSP取证结果误标为当前代码结果。

该取证版RAM完整构建0错误/0警告，SHA256 `45362cf8a636565c1bf40e8cce576ed1ceb8ec880b468c9d20655d27b7b07028`，**未烧录，现已停用，不得用于当前实板验收**。当前工作区BSP、Platform、MSC源码均无新差异。旧固件、快照、抓包及所有历史提交保留。

## 2. 实际调用栈与源码位置

行号基于上述HEAD。VERIFIED表示源码或指定快照确认；INFERRED表示推断；UNRESOLVED表示尚待定位；NOT_TESTED表示未执行。

### 2.1 Disconnect与Class释放（ISR路径）

```text
CM7/BSP/bsp_usb.c:619 OTG_FS_IRQHandler
 -> Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c HAL_PCD_IRQHandler
 -> CM7/BSP/bsp_usb.c:713 HAL_PCD_DisconnectCallback
 -> CM7/Platform/usb_device.c:1543 Usb_Disconnect
 -> Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:995 USBD_LL_DevDisconnected
 -> CM7/Platform/usb_device.c:1855 Usb_ManagedClassDeInit
 -> CM7/Service/usb_msc_class.c:84 Msc_DeInit
    -> :91 s_configured=false
    -> :92 USB_MSC_BotStop
    -> USB_Device_CloseEp(OUT2)，然后仍尝试关闭IN2
 -> CM7/Platform/usb_device.c:706 USBD_LL_CloseEP
 -> CM7/BSP/bsp_usb.c:337 BSP_USB_AbortEp
 -> Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1992 HAL_PCD_EP_Abort
 -> Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:915 USB_EPStopXfer
```

VERIFIED：Disconnect只在HAL实际通知时走此链，不能把每次物理拔线直接等同Disconnect回调；当前VBUS sensing关闭。Reset的另一入口是HAL USBRST处理硬件地址、ENUMDNE才调用BSP ResetCallback → Platform `Usb_Reset:1481` → Core `USBD_LL_Reset:771` → ManagedClassDeInit。两者共享Class释放路径。

VERIFIED：`USB_MSC_BotStop`（`usb_msc_bot.c:232`）置active=false、清接收就绪及控制资格、取消当前backend epoch，进入BOT_OFF；不清数据缓冲，不把未完成Worker当作已经排空。`USB_MSC_BotStart:221`到BOT_START后还需等待旧epoch drained。未发现Stop时直接释放SD RAW租约。

### 2.2 Endpoint Busy、取消与Close

VERIFIED：`USBD_LL_CloseEP:722`在opened/busy的非零端点先调用BSP Abort；只有SYS_OK才清Platform busy，然后调用BSP Close。全部成功才清opened、Core端点元数据。Abort/Flush失败均保留资源；不能通过直接置busy=false改变这个契约。

VERIFIED：`Msc_DeInit`自身检查方向资源标记，Busy判断由上述Platform包装完成。OUT关闭失败仍尝试IN；失败方向的`s_out_open/s_in_open`不清零。`Usb_ManagedClassDeInit`失败时恢复pUserData归属，Class=RELEASE_FAILED、stop_required=true，不提交最终Class内存归还；仍尝试关闭能够安全关闭的其他端点。

### 2.3 Stop（Task路径）

```text
CM7/Application/app_usb.c App_USBProcess / App_USBFinishRound
 -> CM7/Service/usb_mode.c USB_Mode_Stop
 -> CM7/Platform/usb_device.c:370 USB_Device_Stop
 -> :1729 Usb_StopDevice
 -> CM7/BSP/bsp_usb.c:188 BSP_USB_Stop
    -> :195 HAL_NVIC_DisableIRQ(OTG_FS_IRQn)
    -> :202/:208 对各已打开方向调用HAL_PCD_EP_Abort
    -> 任一失败直接返回
    -> 所有取消成功后Flush共享RX
    -> HAL_PCD_Stop
    -> 清NVIC pending，成功后才BSP s_started=false、清PIO占用
 -> Platform仅在BSP Stop成功后才释放Class/端点、恢复FIFO布局
```

VERIFIED：当前主Stop顺序为“先硬件Stop，再Class释放”，与新要求的CLASS_RELEASE → PCD_STOP不同。不能仅改变枚举名称来声称完成状态机。

VERIFIED：Stop失败锁存`s_hardware_stop_failed/stop_required/last_result`，Platform与BSP started可能仍true，Class及各方向保留各自真实状态；未调用到HAL_PCD_Stop不等于硬件已经停止。重复Stop可以再尝试原路径，但没有独立取消事务和完成通知。

## 3. 取消返回值与回调的精确定义

| 观察项 | 当前真实语义 |
| --- | --- |
| BSP Abort返回SYS_OK | VERIFIED：当前HAL同步停止轮询成功，并且BSP要求的FIFO处理成功；代表本实现已确认的停止结果，不是“仅提交了异步请求” |
| BSP Abort返回SYS_ERR_BUSY | VERIFIED：可来自共享RX的其他活动接收检查；该分支尚未调用HAL取消，不能记成取消已提交 |
| HAL Abort返回HAL_ERROR | VERIFIED：本地LL等待EPENA清零超时等失败，可能已写SNAK/EPDIS；不证明硬件已经静止，也不保证稍后会有可用通知 |
| 专用取消完成回调 | VERIFIED：当前不存在 |
| OUT EPDISD处理 | VERIFIED：HAL IRQ约1150行处理并清EPDISD；仅iso incomplete条件下通知ISOOUTIncomplete，普通Bulk没有取消确认通知 |
| Data OUT完成 | VERIFIED：XFRC → PCD_EP_OutXfrComplete_int → BSP HAL_PCD_DataOutStageCallback:675 → Platform Usb_DataOut:1410，是数据完成 |
| Data IN完成 | VERIFIED：XFRC → BSP HAL_PCD_DataInStageCallback:633 → Platform Usb_DataIn:1430，是数据/PIO分包完成 |

**结论：当前不能描述为“Abort调用成功仅表示已提交，随后有取消回调确认”。这不是现有API的契约。** 若改为异步取消，必须新增独立的请求身份和完成证明，不能把普通XFRC、超时或Epoch变化当作替代品。

VERIFIED：Platform非零数据回调在Class非ACTIVE时被过滤；目前没有例外通道继续完成旧Stop的资源收尾。因此新增状态机时需拆开“旧会话所有权收尾”和“当前会话业务回调”，不能用新epoch统一丢弃所有旧事件。

VERIFIED：RAM复现中OUT2仍opened/busy、BSP rx_active[2]=1，DOEPCTL2=`0xC00A0040`，EPENA/EPDIS/NAKSTS均1；Class RELEASE_FAILED，USB IRQ101已禁用，started仍1。原始证据见前一份报告及`Tests/MscReconnect/results/`。

INFERRED：当前关闭IRQ后，普通数据完成等依赖USB IRQ的处理无法推进；但**没有证据支持“当前正在等待某个已存在的Bulk取消完成回调”**，因为该回调根本不存在。OUT首次取消未完成的触发原因仍UNRESOLVED。

## 4. 仅Platform/MSC整改的实际阻塞

1. 新要求禁止PCD Stop成功前无条件关闭IRQ；现有BSP Stop在195行进入即关闭。Platform换调用时机仍不能改变此函数内部顺序；调用后强行再开IRQ既不能保证之前没丢事件，也可能绕过底层失败锁定，不能作为修复。
2. 现有BSP不提供异步取消完成身份/状态，普通EPDISD也不向上通知。Platform不能靠busy快照、延迟时间或普通数据回调伪造Cancel Complete。
3. 未授权在Platform直接访问USB寄存器取代BSP。绕过BSP重新实现OUT停用、清IRQ或强制Close不属于安全的分层整改。

因此按最新“发现必须修改未授权模块则停止申请”执行：**不提前实现或宣称完整新状态机**，不修改ST Core、BSP、HAL、UVC、SDMMC及其他禁止模块。下面是待批准接口方案，不是已实现代码。

## 5. 最小接口扩展申请（待批准）

申请额外允许 `CM7/BSP/bsp_usb.c/.h`，范围限于端点取消完成契约和最终PCD停止契约；其他禁止范围不变。此前BSP批准已按最新范围停止使用，本次重新明确审批对象。

拟议接口与语义：

```c
//示意签名，待审批后结合寄存器级证据定稿
SysResult_t BSP_USB_RequestCancel(uint8_t endpoint, uint32_t cancel_token);
SysResult_t BSP_USB_GetCancelStatus(uint8_t endpoint, uint32_t cancel_token,
                                  BspUsbCancelStatus_t *status);
```

- Request成功仅表示登记，状态明确区分PENDING、COMPLETE、FAILED；完成证明必须包含该方向停用、旧事件处理与PIO引用归还条件。
- 引入独立Stop/Cancel Token；旧会话取消完成即使USB session epoch变化仍可归还旧所有权，但不能推动新会话业务。身份耗尽拒绝新建，不复用混淆。
- 增加BSP独立的cancel_complete完成通知，携带endpoint、cancel_token和真实结果；任务查询用于补充校验及通知遗漏后的状态复核，不重用Data IN/OUT。旧会话的取消通知始终先交给对应Stop事务做所有权收尾，不因当前USB epoch变化而丢弃；重复通知不得重复释放。不得修改ST Core或ST HAL来取得事件。
- 修正BSP最终停止入口的IRQ提交时机；仅在端点已静止且HAL/硬件PCD停止得到确认后禁用/清pending。失败返回真实结果，保留未释放资源及可收尾的IRQ路径，禁止简单强制开IRQ或伪造成功。
- 根据真实硬件握手补全OUT停用；只应答对应旧事件，不丢弃共享RX中的其他端点或新SETUP，不把任意GRXSTSP弹出当成无害操作。

ST官方当前H7 LL实现的OUT取消包含Global OUT NAK及停用确认，与本工程直接SNAK/EPDIS轮询存在差异。这只作为审计线索，不能直接替换驱动，更不能照搬其RX弹出处理忽略现有共享接收所有权。参考：[ST H7官方LL源码](https://github.com/STMicroelectronics/stm32h7xx-hal-driver/blob/master/Src/stm32h7xx_ll_usb.c)。RM0399完整相关页在线/下载未成功取得，目前不得标成已逐条核验手册。

## 6. 待实现的状态机与验证矩阵

| 状态 | 进入动作 | 进入下一状态的证明 |
| --- | --- | --- |
| RUNNING | 正常会话 | 收到Stop请求 |
| STOP_REQUESTED | 停止新CBW/新业务提交，保留所有在途引用 | 已完成提交入口封锁 |
| QUIESCE_ENDPOINTS | 分方向登记取消及独立Token | 各方向真实取消完成，不用超时替代 |
| DRAIN_CALLBACKS | 旧会话事件仅做所有权收尾，不投递新业务 | 对应取消与缓冲引用均退役；SD Worker/IDMA独立排空 |
| CLASS_RELEASE | 一次性释放已静止Class；失败保留真实部分状态 | Class释放成功，无重复Free |
| PCD_STOP | 调用获准修正的最终停止契约 | PCD停止确认，才提交IRQ/started状态 |
| STOPPED | 允许后续显式启动新会话 | 端点/地址/EP0按新会话重建 |
| RECOVERY_REQUIRED | 保存故障和仍占用资源/RAW租约 | 仅凭明确完成证明收尾，不自动绕过 |

故障注入测试与实板回归目前均NOT_TESTED：OUT空闲/CBW/Data OUT、IN Data IN、Cancel失败/延迟确认、Class释放失败、PCD失败、Reset/Disconnect交错、Stop期间与新会话后的迟到事件、取消完成后继续Stop、重复释放检查；RAM优先，随后SD只读及默认UVC，连续10次每次保存捕获与状态。

获批前只交付此审计与申请，不把尚不存在的异步取消接口写成已完成实现。用户未提交Keil修改、真实SD内容及原始现场不变。
