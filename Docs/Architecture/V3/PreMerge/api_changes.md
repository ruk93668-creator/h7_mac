# 正式接口清单

| 层 | 合并候选接口 |
| --- | --- |
| 初始配置 | `VideoConfig_GetDefault`、`VideoConfig_GetStorage` |
| 生命周期 | `AppV3Supervisor_Set`、`Query`、`Read`、`Recover`，由 ControlTask 调用 |
| Service | `CameraService_Request`、`UsbService_Request`、`StorageService_Request`，命令成功入队后在 Business 结果通道确认终结 |
| Raw Media | `CameraCapture_StreamAcquire` 与 `CameraCapture_StreamReleaseLease` |
| JPEG Media | `CameraCapture_StreamAcquireJpeg` 与 `CameraCapture_StreamReleaseJpeg`，底层 `CameraMjpegFifo_Acquire` 与 `CameraMjpegFifo_Release` |
| Legacy | 生产 START 自检用 `CameraCapture_StreamGetFrame` 与 `CameraCapture_StreamReleaseFrame`，共用原物理安全契约 |
| USB | `USB_UVC_SubmitFrame`、`USB_UVC_SubmitFrameView`、`USB_UVC_ReadReaderProof`、`USB_Device_ReadLinkState` |
| Storage | `StorageFS_Mount/Open/Write/Sync/Close/Unmount/Recover`，`MjpegAvi_Begin/AppendFrame/Finish` |
| 诊断 | `CameraCapture_GetStats`、`CameraCapture_GetStopFacts`、`CameraMjpegFifo_ReadOccupancy`、`StorageFS_GetDiagnostics`、`BSP_SDMMC_GetIoDiagnostics`、`App_EventsReadStackWatermarks` |

正式硬件命令仍从数值 16 开始，状态机及 ACK/FAULT 路径沿用。已废弃模拟编号获得 `SYS_ERR_NOT_SUPPORTED`，命令号和发起者仍在终结消息中。应用 IPC 快照 80 B、AppMessage 52 B、ServiceCommand 16 B。

显式 JPEG 凭据为 Session、Loan ID 与 Consumer。归还时核对 FIFO 当前会话、Loan ID 和内部所有权位。FIFO 仍由 CameraTask 操作，每个消费者最多一张，Legacy 自检与显式租约拥有独立身份。USB/SD 专用 Media API 在 R1 已删除，正式清理未改变此契约。
