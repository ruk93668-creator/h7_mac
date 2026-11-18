# Phase 5 Legacy 接入待审差异

以下是拟议的文件级最小接缝，未应用。原 52B/56B 结构、Keil Target、BOOT、Media、UVC、任务优先级及栈配置在本阶段保持原样。

| 现有路径与锚点 | 拟加或替换的调用 | 门禁 |
| --- | --- | --- |
| CM7/Application/Tasks/usb_task.c，`UsbTask_Process` 118，UVC 顺序约 153–160 | 按构建选择将原 App_USB 业务推进替换成一个 VUSB Endpoint round。相同 owner 使用现有 Mode/UVC 函数，取消、Process、Frame、Process、Stop 顺序保持。命令投递通过独立执行 transport，旧控制入口只作适配 | 未消费终结不可复用；通知合并后仍继续轮询。Legacy 和 V2 入口只启用一个 |
| CM7/Application/app_uvc_bridge.c，`App_UvcBridgeCameraProcess` 137、Get/Offer 分支 217–267、`Bridge_CameraRelease` 456、`App_UvcBridgeUsbProcess` 268 | 用定长带身份元数据为原 Frame REQUEST 匹配 G/H，Camera owner 准入。既有 Get 成功后绑定真实 M，再分配 B。Return 核验 G/H/B/M，按原 Camera owner 位置 Release；原 ACK 入队与 USB 匹配出队分别发布事实 | 不增加 Get；不绕过旧 Return；SD-only 分支继续归内部 Camera，独立 Terminal Receipt 不可被覆盖 |
| CM7/Application/Tasks/camera_task.c，`CameraTask_Process` 112；CM7/Application/app_camera.c，`App_CameraFinishRound` 365、Qualification Ops 621–622 | 挂接独立 R 到原 G 的 owner 核验。通过已有 `VCamera_QueryExitEvidence` 使用同一 MRContext 并验证退出。released/idle 当前为 NULL，待真实证据通过后才可替换 | ACTIVE→RELEASING 与全部 Get/Retain 入口在单所有者内串行。普通 STOP 仍由 FinishRound 推进 |
| CM7/Media/Src/camera_capture.c，Raw Get/Release 1107/1184；camera_mjpeg_fifo.c，Get/Retain/Release 325/377/405/424 | 增加只读权威持有和最后归还视图，报告真实 token/实例代次及消费者所有权，绑定现有 Raw 池或 JPEG FIFO | 不通过地址推导身份，不新增引用计数，JPEG 保留双 Span 和 SD 独立 Retain |
| CM7/Service/usb_uvc_class.c，Cancel 315、状态 335、Process 366；CM7/Service/usb_mode.c，Stop 352 | 优先消费已有底层状态。若 Frame 的无读者事实无法完整取得，只补可核验的 UVC/Payload 读者或 Abort 完成端口；延迟 Stop 到 VUSB gate | Cancel 接受不等于 Abort 完成。必须覆盖 Payload 后续 SOF、原始 Frame 的 CPU 读以及 DMA 操作，禁止猜测控制器状态 |
| 原启动、Keil CM7 Target 和 Composition 描述符 | 在获准集成后注册 VUSB Ops、选择唯一活动入口并添加静态队列，明确模块身份、消息尺寸和任务栈预算 | 队列和优先级变更、原 Watch/ACK/FAULT 映射、ABI、Map、双核 Rebuild 分别审查 |

### 接缝调用顺序草案

```diff
# USB 任务。只在 V2 集成 Target 替换原业务轮次。
- App_USBProcess(); legacy_uvc_frame_round(); App_USBFinishRound();
+ VUSBEndpoint_RunRound(usb_endpoint, now);
# VUSBOps 内继续使用已存在的 Mode / UVC 底层调用。

# Camera Bridge。以下是语义差异，故障与队列重试分支必须单独审查。
- on_matching_request: GetFrame(); AllocateBridgeLease(); Offer();
+ on_matching_request: ValidateSideMetadata(); MR_Commit(); MR_Admit();
+ MR_Obtain(); AllocateBridgeLease(); MR_BindBridge(); Offer();
- on_matching_return: ReleaseFrame(); enqueue_return_ack();
+ on_matching_return: MR_Return(); MR_ReadReceipt(); MR_FlushReceipt();
+ on_exact_usb_ack_consumed: MR_ConfirmConsumed(); MR_Retire();
```

禁止把草案作为可直接应用的补丁。至少执行 V1 实板 YUY2 与双 Span JPEG、断开/复位/挂起、Abort 失败、真实 ACK full、延迟消费、重启及双路并行退出；保存原始硬件/任务证据。现阶段 Grant→Frame Lease 与 Stable Admission Closure 都继续 NOT_PROVEN。

### MSC、Composite 和 Phase 6

UVC 端口仅包含视频 Mode、Class 与帧交接，MSC/BOT/SCSI 不获得任何 Camera Grant，也不得借用 VUSB 的 G/H/B/M 状态。未来 Composite 需要单独审批 USB Device Core 端点/描述符与 Class 组合策略、带宽和排队，以及 FatFS/MSC 对卷的独占交接。当前实现无 Composite 暴露面。

Phase 6 Storage 可复用 EMEndpoint、真实 Camera exit query 和唯一 MRContext，Storage 异步输入独立 S、G、R。SD 资源归还由 Camera owner 执行。文件 Finish/Close/Unmount 与 Frame/SDMMC DMA 事实分列，任何一项都不得代替另一项。完成可控软件模块不等于 USB 或 Storage 已接入真实任务，真实 Backend、ACK 消费和物理读者证据仍是集成前置条件。
