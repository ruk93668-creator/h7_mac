# P1-FINAL：全局生命周期契约与实施边界

VERIFIED：起点9d51d0c5d5b62d04af3be968ec452dae8835c0c1，研究分支codex/usb-device-lifecycle-manager；仅批准PDF未跟踪。当前远端读查询connection reset，保留基线，未拉取/重置。实际源路径、编码、哈希、定义行号见Tests/UsbLifecycleFinal/reports/source_audit_baseline.json。

## 实际生产链（起点行号）

| 入口/上下文 | 真实链与资源归还点 | 当前缺口 |
| --- | --- | --- |
| 显式Stop/Task | UsbTask_Process（usb_task.c:119）→App_USBProcess/FinishRound（app_usb.c:105/124）→USB_Mode_Process/Stop（usb_mode.c:223/392）→UsbMode_AdvanceStop:357→USB_Device_Stop（usb_device.c:470）→Usb_StopDevice:1930→Usb_DrainBulkInStops:1882→BSP_USB_Stop（bsp_usb.c:1125）→ManagedClassDeInit | IN-only进度；没有统一消费者Prepare ticket；BSP仍同步处理EP0 |
| Reset/ISR | HAL_PCD_IRQHandler→HAL_PCD_ResetCallback→Usb_Reset（usb_device.c:1618）→有引用则锁定；无引用才USBD_LL_Reset | Session失效不等于引用退休；Core同步Reset不能等待Task |
| Disconnect/ISR | HAL Boundary→OutInvalidate；BSP DisconnectCallback→Usb_Disconnect（usb_device.c:1687）→有引用保留 | 缺少已批准EP0与消费者恢复闭环 |
| SETUP/ISR同步Core | HAL RX pop/copy→SetupStageCallback→Usb_Setup（usb_device.c:1444）→Core | 旧EP0 busy通过Close/Open处理；OUT0不能通用EPDIS；标准请求状态阶段门控未闭合 |
| BOT Reset/ISR→Task | Msc_Setup→DeferControlStatus→USB_MSC_BotReset（usb_msc_bot.c:262）→BotProcess:276 | Reset直接同步Abort82/02；Process整段持USB IRQ锁，不能把新Poll直接塞入原锁 |
| MSC DeInit/同步Core或Task | Msc_DeInit（usb_msc_class.c:84）→BotStop→同步Close02/82 | 未等待Storage drained；不能先清配置标记冒充Prepared |
| Worker/StorageTask | StorageProcess（usb_msc_storage.c:125）→device.execute→DRAIN→quiescent→DONE/EMPTY | drained现有条件不能替代USB proof；SD RAW不随断线释放 |
| UVC Alt0/ISR；Cancel/Task | UVC_SetStreamAlt（usb_uvc_class.c:1003）→Close81→ReleaseFrame:1105；USB_UVC_Process:366→PayloadCancel→同步Abort | ISO缺独立停止ticket；不能把Bulk ACK/INEPNE契约直接套入 |
| Frame return/Task | Payload_FinishFrame→Frame consumer退出→Bridge_UsbReturn（app_uvc_bridge.c:484）→Return/ACK→Bridge_CameraRelease:456 | Camera/Media仍按原lease协议；不以USB state清零替代全部消费者退出 |

INFERRED：目标只由原UsbTask推进Controller StopIntent；Class提出Prepare请求并返回消费者票据，BSP只证明端点/PIO/FIFO，Platform汇总身份。模式切换和BOT Reset分别引用同一Coordinator的不同scope，不能创建并行硬件执行器。Controller、Session、EP0、Endpoint/Transfer、Worker、Frame独立命名空间，全部计数耗尽拒绝。

INFERRED：顺序为Fence→Prepare consumers→并行有界推进端点→验证消费者drained→核对PreparedTicket全集→Class提交释放→Controller停止/重新初始化→新Session。实际依赖须由具体proof决定，不能先停IRQ再等待IRQ生成证明。任何未闭合项保留RECOVERY_REQUIRED。

BLOCKED：完整EP0/ZLP安全原语未获LL边界扩展批准，见P1_LL_ZLP_InterfaceExtension_Request.md。不得为完成阶段数字而自动Enable C1、强制Class释放或自动重启PCD。允许先形成不越权的双向端点proof组件及测试；没有消费者/EP0票据不向破坏性BSP Stop提交。

NOT_TESTED：实板全部项目；不合并main。
