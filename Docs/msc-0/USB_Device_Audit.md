> 审计日期：2026-11-16；源码基线：`f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`；分支：`codex/usb-msc-bringup`。
> VERIFIED = 已由当前源码确认；INFERRED = 源码推导或拟议设计，尚未实现；UNRESOLVED = 仍需确认；NOT_TESTED = 未实测。源码事实不等于硬件验收。

# USB Device 源码审计

## 结论

- VERIFIED：当前有效构建只有一个 Class。`CM7/Platform/usbd_conf.h:5` 的 `USBD_MAX_NUM_INTERFACES=1` 在此 Core 中实际作为最大接口编号比较，允许接口 0、1，不等于只允许一个接口。`usbd_def.h:55` 未启用 USE_USBD_COMPOSITE 时 Class 数量为 1；Keil CM7 Define 未启用该宏。
- VERIFIED：中间件保留 `USBD_RegisterClassComposite` 条件分支，但当前树没有 `usbd_composite_builder.h` 实现；不能直接打开宏就宣称支持多 Class。Platform 只有一份 callbacks/driver、一个生命周期、一个固定分配块，并固定 classId=0。
- INFERRED：独立 MSC 可注册为当前唯一 Class，复用现有 Core/LL/BSP，不需为普通 Bulk 增加 Core 传输机制。需要扩展 mode 注册与应用启动接入，BOT/SCSI 放 Service，不放 Core/BSP。
- INFERRED：普通 MSC EP0 Class 请求可由现有 Setup 回调接收；异步 BOT Reset 的状态阶段需要专门支持（见下），复合路由不能沿用单 Class 逻辑。

## 文件、结构、函数、调用关系

以下路径相对仓库根目录；行号对应审计基线。头文件是关联结构/API 的证据，行为取自 .c 实现。

|路径|关键结构/函数与职责|调用关系、上下文、所有权及复用|
|---|---|---|
|`Middlewares/ST/STM32_USB_Device_Library/Core/Inc/usbd_def.h`|`USBD_HandleTypeDef`、`USBD_ClassTypeDef`、`USBD_EndpointTypeDef`、`USBD_SetupReqTypedef`；pClass[]/pUserData[]、单 pClassData、classId、设备/EP0状态|设备句柄由 Platform 静态持有；回调表指向 Class；不是自动多 Class 实现|
|`.../Core/Src/usbd_core.c:202`|`USBD_RegisterClass`、`USBD_SetClassConfig`、`USBD_ClrClassConfig`、`USBD_Start/Stop`|注册到槽0，配置时 Init，取消配置/Reset/断开时 DeInit；Start/Stop 任务，主机配置及总线事件 ISR；可复用单 MSC|
|`.../Core/Src/usbd_core.c:545`|`USBD_LL_SetupStage`|解析 Setup→按 recipient 调 `USBD_StdDevReq/StdItfReq/StdEPReq`；USB ISR|
|`.../Core/Src/usbd_core.c:582`|`USBD_LL_DataOutStage`、`USBD_LL_DataInStage`、`USBD_CoreFindIF/EP`|EP0管理分段/状态，数据EP按方向查Class；非 composite 查找恒返0。EP0 OUT按请求查归属，EP0 IN完成固定Class0；复合分发器需自行跟踪事务|
|`.../Core/Src/usbd_core.c`|`USBD_LL_Reset/Suspend/Resume/DevDisconnected/SOF/IsoINIncomplete`|更新 Default/Addressed/Configured/Suspended，reset/deconfig回收Class；ISO incomplete依当前classId，不能直接视为可靠复合路由|
|`.../Core/Src/usbd_ctlreq.c:166`|`USBD_StdItfReq`、`USBD_StdEPReq`、`USBD_GetDescriptor`、`USBD_SetConfig`|标准描述符来自 pDesc/Class；Class Setup完成且wLength=0立即 `USBD_CtlSendStatus`；CLEAR_FEATURE(HALT)先清硬件并发状态，再调用Class Setup|
|`.../Core/Src/usbd_ioreq.c:86`|`USBD_CtlSendData/ContinueSendData/PrepareRx/ContinueRx/SendStatus/ReceiveStatus`|维护EP0 total/rem length→LL；保留源/目的缓冲至事务完成或确认取消；不能用栈临时数组|
|`.../Core/Inc/usbd_core.h/usbd_ctlreq.h/usbd_ioreq.h`|上述接口声明|Core API边界；desc/conf template是模板，实际配置在CM7 Platform|
|`CM7/Platform/usb_device.c`、`.h`|`s_device`、`UsbClassState_t`、`UsbDeviceEndpointState_t`、`s_endpoints[2][16]`、`s_in_sequence/completed`|全局单设备/单Class；公开Register/Init/Start/Stop拒绝ISR，端点操作以USB IRQ锁保护|
|`CM7/Platform/usb_device.c:1058`|`Usb_ValidateConfig`、`Usb_PlanEndpoints`、`Usb_RegisterClass`|解析完整配置、Alt0/接口/EP/IAD边界→BSP FIFO计划→包装Init/DeInit→`USBD_RegisterClass`；注册者必须提供静态描述符|
|`CM7/Platform/usb_device.c:1172`|`Usb_Setup`、`Usb_DataIn/Out`、`Usb_Reset/Disconnect`|新SETUP先取消旧EP0；禁止ISO HALT；交Core，回调完成更新busy/sequence后唤醒任务；失败锁存stop_required，不伪造释放|
|`CM7/Platform/usb_device.c:834`|`USBD_LL_Transmit/PrepareReceive`、`USB_Device_OpenEp/CloseEp/AbortEp/StallEp/ClearStallEp`|提交前busy，失败撤销；完成后归还。Flush拒绝busy；OUT共享FIFO；可复用但必须遵守真实完成|
|`CM7/Platform/usb_device.c:938`|`USB_Device_Alloc/Free`、`Usb_ManagedClassInit/DeInit`|唯一1024字节、32对齐内存；释放过程有pending/failed状态，禁止提前重用；不适合两个Class各自malloc|
|`CM7/BSP/bsp_usb.c`、`.h`、`bsp_usb_hal_config.h`|`PCD_HandleTypeDef s_pcd`、`UsbTxTransfer_t`、`s_tx_pio_pool`、`BSP_USB_Init/Start/Stop`|USB2_OTG_FS，PA11/12，内置PHY，Full-Speed，DMA禁用，IRQ优先级6；无VBUS sensing；断线可靠性需实板验证|
|`CM7/BSP/bsp_usb.c:487`|`BSP_USB_Transmit/Receive/GetRxSize`、`Usb_PrepareTxPacket`|IN逐包复制到独立PIO槽、HAL完成后复制下一包；源在全部完成前仍被引用。OUT HAL直接CPU写调用者buffer；size须整包，实际长度另查|
|`CM7/BSP/bsp_usb.c:619`|`OTG_FS_IRQHandler`、全部 `HAL_PCD_*Callback`|IRQ→HAL IRQ→BSP回调→Platform→Core→Class；IN最后一包才上报完整提交完成，ISO incomplete保留所有权待Abort|
|`CM7/BSP/bsp_usb.c:336`|`BSP_USB_AbortEp/AbortAll/FlushEp`|OUT与其他OUT共享RX；Abort可能BUSY；AbortAll先停全部数据EP再清共享FIFO；复合独立停止必须处理共享影响|
|`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c`、`stm32h7xx_hal_pcd_ex.c`、`stm32h7xx_ll_usb.c`及对应Inc|`HAL_PCD_IRQHandler/EP_Transmit/EP_Receive/EP_Abort`、`HAL_PCDEx_SetRxFiFo/SetTxFiFo`、`USB_EPStartXfer/ReadPacket/WritePacket`|PCD设置xfer_buff，PIO IRQ读写FIFO推进指针；底层异步，HAL_OK只表示提交。Ex负责FIFO寄存器；LL依dma_enable选择路径|
|`CM7/Service/usb_test_device.c`|`Usb_TestRegister`、静态设备/配置/字符串、`Usb_ClassSetup`|Vendor单接口，无数据EP；只作枚举参照，不是Bulk或MSC|
|`CM7/Service/usb_mode.c/.h`|`UsbModeContext_t`、`USB_Mode_SelectBootMode/Init/Start/Stop/Process/RequestReconnect`|仅TEST/UVC/NONE有效，MSC枚举预留但返回NOT_SUPPORTED；只允许同模式重连；Start后等待Configured最长5000ms，重连等待500ms|
|`CM7/Service/usb_uvc_desc.c/.h`|`USB_UVC_ConfigureFSDescriptor/ValidateFSConfig`、静态descriptor storage|IAD设备EF/02/01，VC0、VS1 Alt0/1、EP81 ISO1023/1ms；动态生成当前格式；注册后静态有效|
|`CM7/Service/usb_uvc_class.c`、`usb_uvc.h`|`UsbUvcContext_t`、`UvcStreamingControl_t`、`USB_UVC_Register/Process`、`UVC_Init/DeInit/Setup/ControlSetup/EP0_RxReady/TxSent/SetStreamAlt/DataIn/Sof/Incomplete`|pUserData[0]、s_uvc_active；IRQ控制和完成，Task构包/确认；Probe/Commit及pending selector；Alt1打开81，Alt0关闭确认后释放帧|
|`CM7/Service/usb_uvc_payload.c/.h`|`UsbUvcPayload_t`、`USB_UVC_PayloadStartView/BuildPacket/CompletePacket/Cancel/AbortPacket`|最多两段帧view，包token与FID/EOF；完成前偏移不推进。仅复用设计经验，不让MSC使用UVC包格式/帧内存|
|`CM7/Application/Tasks/usb_task.c/.h`、`app_usb.c/.h`、`app_uvc_bridge.c/.h`|`UsbTask_Process`、`App_USBHandleCommand/Process/FinishRound`、`App_UvcBridgeUsbProcess`|任务推进mode/UVC/Bridge；停止先等帧归还再停设备；不能把SD阻塞调用放在此UVC每SOF处理路径|

VERIFIED：完整传输链为 Class→USB_Device_Transmit/Receive→USBD_LL_*→BSP_USB_*→HAL_PCD_EP_*→LL；IRQ反向完成。类事件通知仅为唤醒，不代表磁盘完成。

## MSC 接入风险与最小范围

- VERIFIED：BSP非EP0 OUT只接受MPS整数倍；MSC使用64字节CBW接收槽，实际31字节才通过有效性校验。不能用Receive(...,31)。PIO尾部按word访问，所有OUT目的容量按整包且4字节尾部安全安排。
- VERIFIED：Core的Interface上限比较为 `<=`，Platform为 `bNumInterfaces-1 <= limit`；复合三个接口需要最大编号至少2。不能机械按宏名解释。
- INFERRED：独立MSC普通Setup和数据分发无需改Core；候选新增Service MSC注册、BOT、SCSI及Platform adapter，扩展mode/任务接入即可。Reset若不能同步完成协议复位，需要在Platform拦截专属请求并延后状态，或后续经审批增加Core deferred-status机制。
- VERIFIED：Class Reset返回OK会立即状态ACK，返回BUSY也没有现成Task续接机制。不能在Setup调用SD_Block_Recover；其ISR前置条件不满足。Reset完成定义需确保旧工作不会产生新USB响应并保留缓冲；不完整Reset不得ACK。
- UNRESOLVED：BSP声明9组EP、1024word FIFO是否与具体芯片revision资源完全一致，需RM/寄存器/硬件复核；软件预算见复合报告。当前没有MSC硬件传输证据。
- NOT_TESTED：枚举、Bulk吞吐、短包、stall、Reset/断线回调实板可靠性、UVC回归均未在本阶段运行。

## 审计文件清单补充（VERIFIED）

以下为当前树实际文件与行数；用于核对扫描范围，不能替代上文行为证据。HAL/LL和FatFS实现另见上文对应路径。

|路径|行数|
|---|---|
|`CM7/Application/app_usb.c`|237|
|`CM7/Application/app_usb.h`|72|
|`CM7/Application/app_uvc_bridge.c`|546|
|`CM7/Application/app_uvc_bridge.h`|43|
|`CM7/Application/Tasks/usb_task.c`|220|
|`CM7/Application/Tasks/usb_task.h`|53|
|`CM7/BSP/bsp_usb.c`|895|
|`CM7/BSP/bsp_usb.h`|104|
|`CM7/BSP/bsp_usb_hal_config.h`|8|
|`CM7/Platform/usb_device.c`|1752|
|`CM7/Platform/usb_device.h`|99|
|`CM7/Platform/usbd_conf.h`|26|
|`CM7/Service/usb_mode.c`|364|
|`CM7/Service/usb_mode.h`|62|
|`CM7/Service/usb_test_device.c`|253|
|`CM7/Service/usb_uvc.h`|72|
|`CM7/Service/usb_uvc_class.c`|1126|
|`CM7/Service/usb_uvc_desc.c`|460|
|`CM7/Service/usb_uvc_desc.h`|42|
|`CM7/Service/usb_uvc_payload.c`|334|
|`CM7/Service/usb_uvc_payload.h`|99|
|`Middlewares/ST/STM32_USB_Device_Library/Core/Inc/usbd_conf_template.h`|230|
|`Middlewares/ST/STM32_USB_Device_Library/Core/Inc/usbd_core.h`|175|
|`Middlewares/ST/STM32_USB_Device_Library/Core/Inc/usbd_ctlreq.h`|101|
|`Middlewares/ST/STM32_USB_Device_Library/Core/Inc/usbd_def.h`|523|
|`Middlewares/ST/STM32_USB_Device_Library/Core/Inc/usbd_desc_template.h`|61|
|`Middlewares/ST/STM32_USB_Device_Library/Core/Inc/usbd_ioreq.h`|113|
|`Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c`|1215|
|`Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c`|1058|
|`Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c`|224|
