# P8.3-A 真实后端与所有权源码审计

源码固定为 e0ad3b5744b73095f09b3609738e7431956082c6。路径和行号均以该提交为准。证据来自生产函数体、调用者和运行时绑定。本轮未执行构建、软件测试或实板测试。G→实际租约、稳定关闭屏障与真实端到端 ACK 消费均为 NOT_PROVEN。

## Git 与工作区保护

原工作区在清理分支 e0ad3b5，暂存区为空。未暂存 v2_types.h、h7_mac.uvprojx、h7_mac.uvoptx，未跟踪 output/。两份 Keil SHA256 分别为 3d4c0323c007cc87bc9db2fd0ba2999828c39e46674aaa6cc49d7b152432e60a 和 765e9e89ce48a74247281b7c3f9630203a3acc80c25ebf6b75569af4c7c70269。output 顶层为 LegacyLifecycleArchiveValidation、P8_1、P8_2、q123、q124、q14、RepositoryCleanup、V1ArchitectureIntegration、V2Phase1～7、V2SourceStyle。

架构本地及 origin 跟踪引用为 4a5db4e7a7ff310c3d09f2d79304787b1ade5068，左右独有提交 0/0。它是 e0ad3b5 的祖先，左右差集 0/12。跟踪 main 为 f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb。v2_types.h 两分支有差异且原工作区存在该文件的用户修改，因此用隔离审计工作树。Git fetch 多次遇到 HTTPS 超时或协商重置。改用 GitHub 只读引用 API 实时确认三条源引用与预期一致，所需 Git 对象均已在本地。隔离工作树在架构分支执行 merge --ff-only，得到 e0ad3b5。仅推送基线的正常 Git 请求遇到多次 HTTPS 连接失败。文档提交前的最新独立核对仍为 4a5db4e。

## 任务边界和真正的端口

- CM7/Application/Tasks/camera_task.c:99–139。CameraTask_Process 依次处理 BeginRound、Legacy 命令、App_CameraProcess、App_UvcBridgeCameraProcess、FinishRound，随后 V2Runtime_Round(CAMERA) 才推进。Bridge Camera 侧先做 App_StorageProcessReturns，然后 Flush/Return/Offer。CameraTask 是公开 Get/Retain/Release 的唯一生产任务。Raw DMA 与 FIFO 的 IRQ/底层状态仍按原算法在各自临界区处理，IRQ 不判断资格。
- 同目录 usb_task.c:104–106、160 在 UsbTask_Process 推进 USB 设备/Bridge，然后 V2 轮次。storage_task.c:54–65 在 StorageTask 内推进 App_StorageProcess 再调用 V2。control_task.c:121、138 推进 V2 Control 且保留 Legacy 控制。当期 Supervisor、Lifecycle、Endpoint 的命令完成通过 v2_rtos_transport.c 既有 request/reply/consume 绑定；CM7/ApplicationV2/Core/Runtime/v2_rtos_runtime.c:118–249、307–371、461–503 初始化 owner 与 BOOT，绑定的是诊断 facts、no_media_obtain、no_grant/no_release、no_lifecycle_send/read、no_receipt 等拒绝端口。目前诊断 cold/idle/facts 不能作为释放或停机依据。
- Camera 模块 VCamera_QueryExitEvidence 首先检查 owner 和端口，再到 MR_VerifyExit（camera_module.c:433–454）。V2 USB/Storage 的 Capture Lifecycle Tx 与 EM/LC 控制完成消费、物理 Return ACK 分属不同边界。两路 Begin 分别调用 app_camera_generation.h 声明、app_camera_qualification.c 实现的 App_QualificationNextGeneration；Cleanup 后原资格 Receive/Process/Flush 已移至历史测试，产品无真实资格服务可直接复用。

## CameraStream Raw / DCMI

CM7/Media/Src/camera_capture.c:1107–1240：Legacy Bridge Camera 侧调用 CameraCapture_StreamGetFrame，Raw 分支到 CameraStream_GetFrame。临界区检查池和唯一 in_use_index，拒绝 M0/M1，READY 转 IN_USE，返回单 span。ReleaseFrame 只有该槽为真实 IN_USE、且不在 DMA M0/M1 才转 FREE，调用者仍须先证实消费者不再读。地址或 in_use_index 单独不足以作为冻结 M，Raw 缺公开跨复用物理代次，不能套 JPEG 多消费者持有。采集池和 DMA 算法留给 Media/BSP。

CM7/Application/app_camera.c:113–154、355–395：常规停止由 AdvanceStop/FinishRound 依据 GetStopFacts/CameraCapture_Stop 推进，Media 完全停后 Device_Stop，并另查 Bridge Idle；保留启动回退与保护性路径。camera_capture.c:695–835 记录 BUSY/ERROR/DRAINING；JPEG 硬件停后仍可能持有。CM7/BSP/bsp_dcmi.c:590–638：BSP_DCMI_Stop 禁事件和 IRQ、停止 DCMI/DMA，再用 Dcmi_WaitStopped/HAL 恢复结果确认。IRQ 关闭、Media DRAINING 或一次 HAL 返回都不足以证明资源全部退出。

## JPEG 物理 owner

CM7/Media/Src/camera_mjpeg_fifo.c:325–437、840–866。GetFrame 消费 ready ring，在当前 Session 下登记独立 loan slot/id 并设 usb_owned/legacy_index。FIFO 数据跨块和环回绕时保留两段，size_bytes 是总长、首段长度为 size_bytes 减 size_bytes2。Retain 验当前原始持有及视图、设置 sd_owned 并返回 CameraMjpegLease_t(session,id)。ReleaseFrame 清原位，ReleaseLease 匹配 session/id 且 sd_owned 后清 SD 位。Mjpeg_ReleaseLoan 仅最后 owner 回收原块引用。地址校验仍是防误用条件，不能作为 G/H/M 绑定。SD-only 时原 usb_owned 位代表 Camera 内部临时持有。

## USB 交接、读者消退及 ACK

1. CM7/Application/app_uvc_bridge.c:137–265、268–413：UsbTask 生成 command_id，经 APP_MESSAGE_FRAME_REQUEST 交给 CameraTask。Camera 在 enabled/dispatcher 条件下 Get，成功后分配 B=(epoch,lease,command)，同轮 Publish 可能给 SD Retain。Legacy Request 没有 G/H，B 须在真实 Get 后才绑定。
2. UsbTask 接 Offer 后用 USB_UVC_SubmitFrame/View。CM7/Service/usb_uvc_payload.c:61–103、157–195 保存原帧 view 并把原 span 逐包复制到 Payload 静态缓冲。usb_uvc_class.c:368–472 → CM7/Platform/usb_device.c:870 → BSP_USB_Transmit。CM7/BSP/bsp_usb.c:108 的 PCD DMA 为 DISABLE。端点处理的是包缓冲，CPU 后续包仍可读原帧，单次包发送完成不代表整帧安全。
3. HAL_PCD_DataInStageCallback 经 Device/Core 到 UVC_DataIn，按活跃端点、in_sequence 和 class 状态锁存 token。UsbTask 的 USB_UVC_Process 再匹配 token、调用 PayloadCompletePacket，只有整帧完成或成功取消才解除。CancelFrame 只是设置 CANCELLING；Process 的 USB_Device_AbortEp → BSP_USB_AbortEp 要求 HAL Abort、Flush 和完成标记清理。失败进入 RECOVERY，原 view 保持。ISO incomplete 也走恢复。Legacy Bridge 的 GetStreamStatus 返回 INVALID_STATE 即可转 Return；必须另证 reset/deinit 与读取停止的顺序，不能把该分支用作可信 readers_quiescent。
4. Bridge_UsbReturn 只发 Return；Camera 校验 B 后 Bridge_CameraRelease → CameraCapture_StreamReleaseFrame。仅成功才生成 APP_MESSAGE_RETURN_ACK，并保留 last_ack 给匹配的重复 Return。Bridge_CameraFlush 队列满保留 ACK，入队清 Camera pending。UsbTask 在 278–287 实际收到匹配 B 才回 IDLE。当前 Camera 无对应消费确认，通知、队列发送与 IDLE 不能作为 V2 收据已消费的证据。

## Storage 交接、原帧访问和 IDMA

1. app_frame_dispatcher.c:61–74 同 CameraTask 通过 wants/offer 调 app_storage.c:237–330 App_StorageOfferFrame。临界区预订 S=(session,slot,generation)，CameraTask Retain 真 JPEG lease，再提交槽队列。若 accepting/session 变化，把取得的 lease 转 RETURN_PENDING。Retain 在两个临界区之间，s_accepting 不能替代 Camera owner 对 G/R 的关闭。
2. app_storage.c:470–520 StorageTask 取槽执行快照或 MjpegAvi_AppendFrame。同步 Storage_Write/AVI span writer → StorageFS_Write → FatFS f_write → diskio.disk_write → SD_Block_Write → BSP_SDMMC_WriteBlocks → Sdmmc_Transfer。CM7/BSP/bsp_sdmmc.c:1159–1196 先 CPU 复制当前块到 AXI g_sdmmc_dma_buffer，HAL_SD_WriteBlocks_DMA 传入的是 bounce buffer。同步写入返回后该操作的原帧 CPU 读取已终止；IDMA 是否仍访问 bounce buffer 是另一件事。Sdmmc_StopIo:1054–1079 要求 HAL Abort 成功及 Sdmmc_Quiescent 才清 active，失败锁存 fault 并阻止新传输。I/O 退出仍须真实确认。
3. Storage_Release:988 只把匹配槽变 RETURN_PENDING 并通知 Camera。App_StorageProcessReturns:334–375 由 Camera 调真 ReleaseLease；成功写槽 FREE，失败重试。文件关闭、卸载和通知与实际释放不同。Legacy 没有 G/H/S/M/T 的独立 ACK 消费事件，不能把 slot FREE 或文件状态当作已经被 Storage 消费。
4. SD-only 是 app_uvc_bridge.c:217–227 的 local_only Get/Publish/Release。若 Retain 成功，只清内部 owner，等 Storage Return 再回收 SD 位；失败则只归还原持有，无 USB ACK。Camera 先回收 Storage，再 Flush USB ACK；Flush 失败仍已处理当轮 Storage Return，但会阻止之后的新 Get，不能承诺在 USB 背压下仍可持续取帧。

## G/R/H/B/M/S/T 如何关联

| 标识 | 冻结字段与当前证据 | 生产缺口 |
|---|---|---|
| G | MRGrant(session,consumer,generation,acquire)，Capture Lifecycle Tx 与原始 ACQUIRE reply | 原始 G 未绑定 Legacy Frame Request/StorageOffer，也缺当前产品服务端 |
| R | MRRelease(原 G, 独立 request) | 缺 Camera owner 的真实 RELEASE 消费及关闭 |
| H | MRHandoff(session,sequence) 由 MR_Stage 形成 | 需跨队列绑定 G，不与 Legacy command 或槽号混用 |
| B | MRBridge(epoch,lease,command)，真实 USB Offer | Get 后再与预留 H/M 绑定 |
| M | MRMedia(backend,session,id,generation) | JPEG 真 session/id 和 owner bits 可复用；Raw 缺公开稳定 token |
| S | MRSlot(session,slot,generation)，真实 Storage 槽 | Retain 尚无 G/H/M 闭环 |
| T | MRReceipt(g,h,m,serial,result,bridge 等)，现有 handoff/receipt 各 7 | 真实发出、消费回传及退休未连接，满容量不得强删 |

CM7/ApplicationV2/Resource/media_resource.c:140–296、355–507、535–621。L_admit 指同一 Camera owner 对完整 G/H 成功 MR_Admit、预留 T 且未做 Get/Retain。代码先设置 ACCEPTED 再验证，失败会终结，因此不能只读状态。L_close 是同一 owner 用原 G+R 关闭入口。MR_Obtain 在首次取 M 前再检查 closed，已受理但未绑定的 H 应负向终结，不能补取；已绑定才允许排空或读原结果，不重复 Get。MR_Return 核对完整请求和 M，再验证 readers_quiescent、真实 release、released。MR_FlushReceipt / ConfirmConsumed / Retire 分别记录入队、消费与回收。L_verify 只有对应 G/R 已关、没有该 G 的 handoff 和 receipt、可信 exit_safe 成功才成立。单路 G 退出不得清另一 G，最后退出还需内部持有、DMA 停机与首错证据。两个全链路设计目标继续 NOT_PROVEN。

## 被引用生产源码的 Git blob

提交 e0ad3b5744b73095f09b3609738e7431956082c6 的固定对象。

| 源文件 | Git blob SHA |
|---|---|
| CM7/Application/Tasks/camera_task.c | 646ee37182925cadad7a9cd52c439a2a8a19296c |
| CM7/Application/Tasks/control_task.c | 9431547ef822c06d1e190d7221d7cee79296bf03 |
| CM7/Application/Tasks/storage_task.c | 77ab9918baf091c413d3f4ffbc0e4d195d38eeeb |
| CM7/Application/Tasks/usb_task.c | 084b57910874b8427f7d52ddc61d7828e78bb760 |
| CM7/Application/app_camera.c | cfd3e262e88d0656de0d8ba3612a55aca14b9328 |
| CM7/Application/app_camera_generation.h | 136dd49e7c159929c0d00530e61eb3af62e47aa6 |
| CM7/Application/app_camera_qualification.c | 34a6fa3851825b53159bef32762c6cab4e68b26f |
| CM7/Application/app_frame_dispatcher.c | 772898d434bdef8ee5cdad57496cc2c430548594 |
| CM7/Application/app_messages.c | e948b413382253f6d4955bc55a324a24a09dbdbc |
| CM7/Application/app_storage.c | be528fab3715a52fef976119d3eff1668daa1226 |
| CM7/Application/app_usb.c | e69e5ed1671eb2524e6250ca62a18951aca590c2 |
| CM7/Application/app_uvc_bridge.c | 5223988c6412deb8827039d8dba594b056d1f390 |
| CM7/ApplicationV2/Core/Runtime/v2_rtos_runtime.c | a26841fb7a869c31031308ea485cad38b5313504 |
| CM7/ApplicationV2/Core/Runtime/v2_rtos_transport.c | 53f2ff6d6864f45c5eca446a66e7f9784499fee7 |
| CM7/ApplicationV2/Lifecycle/managed_lifecycle.c | 1fb778586cb574e775611ac0aebbe8c00c4fb6a5 |
| CM7/ApplicationV2/Modules/Camera/camera_endpoint.c | 3df790886048a0347fa17fd18b3c427ebf7bc5ec |
| CM7/ApplicationV2/Modules/Camera/camera_module.c | adec8bcc18b706a7f0eb67a148f7620fa56e1f43 |
| CM7/ApplicationV2/Modules/Storage/storage_consumer.c | ed65cf2228f926a123efea043a356638998e6fc7 |
| CM7/ApplicationV2/Modules/Storage/storage_endpoint.c | b560eb8ea1b4c16858ea0aea34e643ea46faba5c |
| CM7/ApplicationV2/Modules/Storage/storage_frame.c | e45216ad6880295c9d7a129f5bd5ebc5b5d55b8a |
| CM7/ApplicationV2/Modules/USB/usb_consumer.c | 02630d591225fca51e88dd3489dfa0ccd1435ecf |
| CM7/ApplicationV2/Modules/USB/usb_endpoint.c | f5a2d3da7404ed61585733b2d1ef42817bcee68f |
| CM7/ApplicationV2/Modules/USB/usb_frame.c | 721f26b4b888fed61ec2e85b8c89defa83db0298 |
| CM7/ApplicationV2/Resource/media_resource.c | d57f4207759250cfd38fdd87ff9891a8cfadcb39 |
| CM7/ApplicationV2/Resource/media_resource.h | 82d4d1185c280add0eb8171f0f245c7b7fc74abf |
| CM7/ApplicationV2/Resource/media_source.c | e6bb0ef1de8958aa1b8ddda806fa5c9bf7a0fd3e |
| CM7/ApplicationV2/Resource/media_source.h | edba6530f399f100a389b75975f11242e7fef95f |
| CM7/ApplicationV2/Supervisor/execution_manager.c | 6e2da57082bbbc0f32534daf3b8026d987441e97 |
| CM7/BSP/bsp_dcmi.c | 34e2493b6d979a7b5d6ccc9899429c7bab7f1e0b |
| CM7/BSP/bsp_sdmmc.c | 1d1f03cb98aac538018dc12d26ef3d011169cf09 |
| CM7/BSP/bsp_usb.c | 0bf250c131c8e6f6cfd0381414d4933445e600f2 |
| CM7/Media/Src/camera_capture.c | 35e47b8435a9c504a9c98fc00344a4ad3b19aac2 |
| CM7/Media/Src/camera_mjpeg_fifo.c | ede931c7657dbe472ad1d6861221b129e51fba7a |
| CM7/Platform/diskio.c | 48c3247587969c94f02fb20edd023253e7f80cf3 |
| CM7/Platform/sd_block.c | 480e6c3ba76d0703c27cd72841bc98bed7a99257 |
| CM7/Platform/storage_fs.c | eb1cb9a625ecc20daea2ad6ff49a9b0a4c70f831 |
| CM7/Platform/usb_device.c | 461003da2dcd6936223b928e04b144a2f6b59439 |
| CM7/Service/usb_uvc_class.c | 9d0451589edfc20f1714a36fb6f972662b482eee |
| CM7/Service/usb_uvc_payload.c | 6b3d930b8322db034ac99984d0da927f9946dcac |
