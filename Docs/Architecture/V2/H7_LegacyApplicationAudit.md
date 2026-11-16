# Legacy Application 全量审计

## 基线和方法

输入 `8a03596`；CM7 源码与 main `f0ce45f` 一致。逐文件读取 CM7/Application 全部 C/H，并交叉追踪 CM7/Common/CM4 调用。下面函数索引是静态调用关系，不代表运行轨迹；间接调用单独登记。未运行测试或编译，不把历史实板或其他分支结果当成本轮验证。

## 文件、状态和执行边界

以下每行包含对应同名 .h；末尾索引另列全部头文件接口。

| 文件 | 功能与调用关系 | 全局状态权威 | Task/ISR、Queue/Event/Timer | 重复职责/迁移关注 |
|---|---|---|---|---|
| `CM7/Application/app_main.c` | App_Init 校验 USB cold mode，APPLICATION Initcall | 无运行状态 | 调度器前；无队列/Timer | 不是真正业务启动 |
| `CM7/Application/app_tasks.c` | EventsInit→MessagesInit→Camera/USB/Control/IPC/Storage Create；记录准备/释放BOOT | create_attempted、tasks debug | main 创建；Control ReleaseBoot | 启动装配须完整承接 |
| `CM7/Application/app_events.c` | 绑定 RuntimeEvent 和 USB/Media callback，Attach/Wait/owner校验 | tasks、pending、initialized/prepared/failed/boot_ready | task/ISR 分开，BOOT前合并通知 | 启动屏障与运输适配耦合 |
| `CM7/Application/app_messages.c` | 五条普通通道、三条Lifecycle通道，权限检查/复制/唤醒 | 两组静态queue/storage/handle、attempted/ready | 各登记owner；Lifecycle运行期拒绝ISR/PRIMASK/BASEPRI | 队列不能替换为通知位 |
| `CM7/Application/app_status.c` | Camera/USB/Control发布→IPC快照和age | status/source_ticks/expired/snapshot_id | 源owner发布、IPC读取；task critical | 部分发布来自task debug，V2改为权威读取需审查 |
| `CM7/Application/video_config.c` | 静态产品配置；运行期更新拒绝 | const default/storage config | 只读，无Timer | 配置头依赖Storage业务类型 |
| `CM7/Application/app_camera.c` | Device查找/配置/Start，Capture初始化/推进，STOP协调 | s_camera：业务、ACK/FAULT、FPS、Qualification、轮次/停止请求结果 | Camera owner；初始配置装配期 | 权威只能保留一份，停止事实不是另一个控制器 |
| `CM7/Application/app_camera_lifecycle.c` | 身份校验、Pending、原Replay工具 | caller-owned参数对象，无独立全局业务状态 | 单所有者纯C，send回调 | 不等于已接入业务 |
| `CM7/Application/app_camera_lifecycle_replay.c` | 四方向受控Tx/Rx、候选、高水位、冻结回复 | caller-owned Tx/Rx和api_busy | 单所有者、非重入，无独立队列/Task | 不应重写第二套Replay |
| `CM7/Application/app_camera_qualification.c` | 两消费者资格、会话、代次、DECIDED/BLOCKED、资源Ops | App_Camera内core与两路Entry/Rx | wrapper强制Camera owner | 固定USB/Storage索引，内部访问耦合；退出证据未接入 |
| `CM7/Application/app_frame_dispatcher.c` | frame_id/timestamp/epoch→Storage wants/offer | Camera写epoch/next_frame；const service | Camera同步路径 | 非通用Graph，无格式转换 |
| `CM7/Application/app_uvc_bridge.c` | 双端Request/Offer/Return/ACK及SD-only；冷启动输出准备 | camera_bridge与usb_bridge分别单写；配置一次写；debug投影 | TO_CAMERA/TO_USB；PLATFORM Initcall | 混合配置、数据面与内部持有，必须拆开承接 |
| `CM7/Application/app_storage.c` | 四请求、帧槽、Mount/AVI/Snapshot/Finish/Recover | slots/commands/queue/session/offering；Storage写文件与业务计数 | StorageTask业务；Camera Retain/真实Release；PRIMASK+DMB | 同文件包含两个任务端，不能整体视作Storage-only |
| `CM7/Application/app_usb.c` | START Mode_Start；STOP在FinishRound；独立ACK/FAULT/FPS | s_usb | USB owner | UVC/Bridge推进仍在Task |
| `CM7/Application/Tasks/camera_task.c` | BOOT、消息、BeginRound/Camera/Bridge/FinishRound、诊断/回复 | TCB/stack/handle/debug | priority3，1536words，BOOT前无限/后20ms | 无资格消息业务接入；每轮至多一控制命令 |
| `CM7/Application/Tasks/usb_task.c` | SOF统计、业务/UVC/Bridge调度、诊断/回复 | TCB/stack/handle/debug | priority4，2048words，前无限/后20ms | 不可丢失Cancel/Process/Bridge顺序 |
| `CM7/Application/Tasks/storage_task.c` | BOOT Prepare、IsReady、Process、栈采样 | TCB/stack/handle，局部boot/tick | priority1，2048words，前无限/后100ms | IsReady不能替换为debug.ready |
| `CM7/Application/Tasks/control_task.c` | 旧Camera→USB串行启停、匹配ACK/FAULT/超时 | state/command/sent/sequence/tick/boot/status_fault | priority2，768words，100ms，期限5000ms | 不能直接改名为Supervisor |
| `CM7/Application/Tasks/ipc_task.c` | owner注入、BOOT RuntimeStart、应用快照/IPC周期推进 | TCB/stack/handle、局部application/tick/debug | IPC owner；vTaskDelay心跳 | 必须迁移owner和IPC状态，不得漏删必要功能 |

唯一无同名C的 `CM7/Application/app_camera_qualification_internal.h` 声明 App_CameraQualificationAccess，直接取得App_Camera权威core/Ops；V2不扩散此裸访问方式。App_Initcall没有独立文件：实际三个导出为 App_Init(APPLICATION)、App_LifecycleMessagesInit(APPLICATION)、App_UvcBridgePrepareOutput(PLATFORM)。应用源码未发现直接创建/推进Framework Timer；task tick和等待超时不是Timer对象。

## 真实运行链

启动：CM7 main→TasksCreate→Framework_InitRuntime→TasksRecordInitialization→调度器→ControlTask_Run→ReleaseBoot。ControlTask_Process 将 UVC和NONE都纳入 Camera START→USB START；STOP先USB后Camera，不自动协调Storage。

Camera：CameraTask_Process 的 BeginRound→Reply→至多一命令→App_CameraProcess→BridgeCameraProcess→FinishRound→Reply。START：HandleCommand→StartResource→BridgeBegin→InitStream→Prepare(Device_Find/configure、Capture_Init)→Device_Start→Capture_StartStream。失败点不同，回退可能调用AdvanceStop；轮次事实抑制同轮重复停止。普通STOP只登记，由FinishRound→AdvanceStop推进Capture_Stop→IsStopped→Device_Stop（READY可跳过），之后检查Bridge Camera Idle。BUSY保留；原错误ACK与后台恢复不混淆。Media ERROR即时保护仍在后端，非另一普通STOP入口。

USB：HandleCommand START→USB_Mode_Start，成功不表示PC已Configured。正常顺序Mode_Process→UVC_Process→BridgeUsbProcess；停止顺序Cancel→UVC_Process→BridgeUsbProcess(false)→UVC_Process→FinishRound(BridgeUsbIsIdle→Mode_Stop)。ACK失败不阻止独立FAULT发送。

Storage：四接口→Storage_Request命令环(4)→StorageTask每轮至多一命令/一帧。OnStart Mount/GetSpace→session++/accepting；首帧创建文件/AVI。JPEG Validate→CRC→Snapshot或Append→周期Sync→Storage_Release仅置RETURN_PENDING。Camera Bridge每轮先ProcessReturns→真实FIFO ReleaseLease→匹配generation/state后FREE。STOP成功入队后本地关accepting；OnStop设DRAINING/stop_pending；排队/offering清空后正常Finish→AVI Finish→Close→Unmount。IDLE和文件Close均不证明Lease已经归还。接收开启且5秒无帧可进入Storage_Fail；此错误路径不同于正常封尾，本轮不复现/修复。

USB帧：REQUEST(command_id)→Camera GetFrame→可选Dispatcher/SD Retain→OFFER(epoch/lease/command)→UVC不再持有→RETURN→真实ReleaseFrame→ACK入队→USB匹配消费后IDLE。重复Return由last_ack重放，错误身份不实际释放。SD-only用Camera内部local_only原始持有→SD Retain→释放内部原始持有；不能把usb_owned字段名当作USB业务grant。

## 间接入口、ISR与同步

| 注册/函数表 | 目标 | 上下文与约束 |
|---|---|---|
| USB_Device_SetEventCallback | AppEvents_UsbNotify | task/ISR按Exception分流，仅通知 |
| CameraCapture_SetEventCallback | AppEvents_CameraNotify | Media task/ISR回调，仅通知 |
| RuntimeEvent_Bind | AppEvents_Post/PostFromIsr | 两接口分离，ISR受FreeRTOS优先级门限约束 |
| xTaskCreateStatic | 五个Task_Run | 对应登记owner任务 |
| Diagnostic Observer | CameraTask_Observe、UsbTask_Observe | owner任务同步投影，不反向业务操作 |
| IPC_Service_SetRuntimeOwner | IpcTask_IsOwner | Service调用检查，非独立任务 |
| Initcall三个导出 | App_Init/BridgePrepareOutput/LifecycleMessagesInit | 调度器前；失败不释放BOOT |
| qualification_ops、storage_service | 资源Ops、Storage wants/offer | Camera同步上下文；released/idle=NULL |

App_Events的PRIMASK保存/恢复、BOOT前pending与BOOT广播临界区是不同边界；volatile不能代替同步。Storage的共享槽和命令环使用PRIMASK+DMB；文件操作在临界区外。Application不直接访问HAL或DCMI寄存器，仍依赖Platform同步API、Device/Media和USB Service公开入口。

## 风险与重复职责

SOURCE_CONFIRMED：main没有app_camera_consumer；Lifecycle队列存在但task没有真实消费者业务接入。released/idle=NULL；Graph不能补上退出证据。BridgeBegin忽略DispatcherBegin结果；多处Publish/通知结果被忽略；V2需明确错误策略，但静态发现不等于已复现生产故障。

NOT_PROVEN：Grant→Loan完整关联、稳定Admission Closure、分消费者退出闭环。Camera/USB/Control状态发布部分经Debug转写，Storage缓存由诊断结构拷贝；V2必须改为权威投影而不是产生新业务副本。协议与Media引用算法不是因文件复杂就可删除的重复职责。

## 全函数与头接口索引

索引覆盖全部Application C/H。直接调用者检索CM7/Common/CM4自有C，排除第三方Components及测试；静态缺少调用者不能证明可删除，需结合上表和后续链接验证。执行上下文、状态、队列继承逐文件表；Storage Offer/Returns明确属于Camera，Storage Process属于Storage，外部请求属于合法调用task。以下被调用者包含外部函数及Ops字段。

### `CM7/Application/app_camera.c`

头依赖：`app_camera.h`、`app_camera_qualification_internal.h`、`video_config.h`、`app_uvc_bridge.h`、`platform_time.h`、`camera_capture.h`、`ov5640_device.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `App_CameraConfigure` :71 | 校验sensor/捕获格式并一次提交配置 | CM7/Application/app_camera.c:199::App_CameraPrepare | `CameraCapture_ValidateStreamConfig`、`CameraConfig_IsVariableLength`、`Ov5640_ValidateConfig` |
| `App_CameraGetConfig` :94 | 读取状态/配置 | CM7/Application/app_camera.c:199::App_CameraPrepare<br>CM7/Application/app_camera.c:459::App_CameraSampleStatus | `VideoConfig_GetDefault` |
| `App_CameraInit` :100 | 首次初始化 | 无直接调用；须核对回调/导出/外部API | `App_CameraStart` |
| `App_CameraInitStream` :106 | 首次初始化 | CM7/Application/app_camera.c:508::App_CameraStartResource | `App_CameraStart` |
| `App_CameraStop` :112 | 仅登记普通停止意图并返回BUSY | 无直接调用；须核对回调/导出/外部API | 无 |
| `App_CameraAdvanceStop` :123 | 按轮次事实推进Media停止、Device停止 | CM7/Application/app_camera.c:245::App_CameraStart<br>CM7/Application/app_camera.c:365::App_CameraFinishRound | `CameraCapture_GetStopFacts`、`CameraCapture_IsStopped`、`CameraCapture_Stop`、`Device_Stop` |
| `App_CameraBeginRound` :169 | 记录本轮停止事实序号 | CM7/Application/Tasks/camera_task.c:112::CameraTask_Process | `CameraCapture_GetStopFacts` |
| `App_CameraRun` :180 | 推进Media，抑制同轮重复错误停止 | CM7/Application/app_camera.c:336::App_CameraProcess | `CameraCapture_GetStopFacts`、`CameraCapture_IsStopped`、`CameraCapture_Process` |
| `App_CameraPrepare` :199 | 前置准备与校验 | CM7/Application/app_camera.c:245::App_CameraStart | `App_CameraConfigure`、`App_CameraGetConfig`、`CameraCapture_Init`、`Device_Find`、`FRAMEWORK_CONTAINER_OF`、`VideoConfig_GetDefault`、`configure` |
| `App_CameraStart` :245 | 请求/执行启动，详见模块调用链 | CM7/Application/app_camera.c:100::App_CameraInit<br>CM7/Application/app_camera.c:106::App_CameraInitStream | `App_CameraAdvanceStop`、`App_CameraPrepare`、`CameraCapture_Start`、`CameraCapture_StartStream`、`Device_Start` |
| `App_CameraSetDiagnosticObserver` :278 | 诊断投影 | CM7/Application/Tasks/camera_task.c:71::CameraTask_Run | 无 |
| `App_CameraCanAcceptCommand` :284 | 只读命令准入 | CM7/Application/Tasks/camera_task.c:112::CameraTask_Process | 无 |
| `App_CameraHandleCommand` :290 | START/STOP/未知命令分支，保留ACK与停止记录 | CM7/Application/Tasks/camera_task.c:112::CameraTask_Process | `App_CameraObserve`、`App_CameraStartResource` |
| `App_CameraProcess` :336 | 推进所属模块工作/错误 | CM7/Application/Tasks/camera_task.c:112::CameraTask_Process | `App_CameraObserve`、`App_CameraRun`、`CameraCapture_GetLastResult` |
| `App_CameraCanOfferFrame` :359 | 只读能力/准入 | CM7/Application/Tasks/camera_task.c:112::CameraTask_Process | 无 |
| `App_CameraFinishRound` :365 | 唯一普通停止推进、真实结果冻结、统计 | CM7/Application/Tasks/camera_task.c:112::CameraTask_Process | `App_CameraAdvanceStop`、`App_CameraObserve`、`App_UvcBridgeCameraIsIdle`、`CameraCapture_GetStats`、`Platform_TimeGetMs` |
| `App_CameraGetPendingAck` :425 | 读取待交付记录 | CM7/Application/Tasks/camera_task.c:141::CameraTask_Reply | 无 |
| `App_CameraConfirmAckEnqueued` :434 | 匹配完成/发送确认 | CM7/Application/Tasks/camera_task.c:141::CameraTask_Reply | 无 |
| `App_CameraGetPendingFault` :440 | 读取待交付记录 | CM7/Application/Tasks/camera_task.c:141::CameraTask_Reply | 无 |
| `App_CameraConfirmFaultEnqueued` :453 | 匹配完成/发送确认 | CM7/Application/Tasks/camera_task.c:141::CameraTask_Reply | 无 |
| `App_CameraSampleStatus` :459 | 单独采样已应用配置，普通Getter无副作用 | CM7/Application/Tasks/camera_task.c:157::CameraTask_PublishStatus | `App_CameraGetConfig` |
| `App_CameraGetStatus` :475 | 读取状态/配置 | CM7/Application/app_camera.c:496::App_CameraObserve<br>CM7/Application/Tasks/camera_task.c:157::CameraTask_PublishStatus | 无 |
| `App_CameraGetLastResult` :490 | 读取状态/配置 | 无直接调用；须核对回调/导出/外部API | 无 |
| `App_CameraObserve` :496 | 诊断投影 | CM7/Application/app_camera.c:290::App_CameraHandleCommand<br>CM7/Application/app_camera.c:336::App_CameraProcess<br>CM7/Application/app_camera.c:365::App_CameraFinishRound | `App_CameraGetStatus`、`observer` |
| `App_CameraStartResource` :508 | Bridge新epoch与采集启动/失败状态记录 | CM7/Application/app_camera.c:290::App_CameraHandleCommand<br>CM7/Application/app_camera.c:552::App_CameraQualificationStart | `App_CameraInitStream`、`App_UvcBridgeCameraBegin`、`CameraCapture_GetLastResult`、`Platform_TimeGetMs` |
| `App_CameraResourceState` :536 | 从权威业务事实投影资源状态 | 无直接调用；须核对回调/导出/外部API | 无 |
| `App_CameraQualificationStart` :552 | 请求/执行启动，详见模块调用链 | 无直接调用；须核对回调/导出/外部API | `App_CameraStartResource` |
| `App_CameraStopIdentityMatches` :563 | 完整身份匹配 | CM7/Application/app_camera.c:573::App_CameraRequestStop<br>CM7/Application/app_camera.c:594::App_CameraReadStopResult<br>CM7/Application/app_camera.c:609::App_CameraConsumeStopResult | 无 |
| `App_CameraRequestStop` :573 | 请求/执行停止，详见模块调用链 | 无直接调用；须核对回调/导出/外部API | `App_CameraStopIdentityMatches` |
| `App_CameraReadStopResult` :594 | 请求/执行停止，详见模块调用链 | 无直接调用；须核对回调/导出/外部API | `App_CameraStopIdentityMatches` |
| `App_CameraConsumeStopResult` :609 | 请求/执行停止，详见模块调用链 | 无直接调用；须核对回调/导出/外部API | `App_CameraStopIdentityMatches` |
| `App_CameraQualificationAccess` :626 | 受控取得唯一core/Ops并拒绝不合法Prepare时机 | CM7/Application/app_camera_qualification.c:528::Qual_AppAccess | 无 |

### `CM7/Application/app_camera.h`

头依赖：`sys_result.h`、`camera_config.h`、`app_messages.h`、`app_camera_qualification.h`。

公开接口：`App_CameraConfigure`、`App_CameraGetConfig`、`App_CameraInit`、`App_CameraInitStream`、`App_CameraStop`、`App_CameraBeginRound`、`App_CameraRun`、`App_CameraSetDiagnosticObserver`、`App_CameraCanAcceptCommand`、`App_CameraHandleCommand`、`App_CameraProcess`、`App_CameraCanOfferFrame`、`App_CameraFinishRound`、`App_CameraGetPendingAck`、`App_CameraConfirmAckEnqueued`、`App_CameraGetPendingFault`、`App_CameraConfirmFaultEnqueued`、`App_CameraSampleStatus`、`App_CameraGetStatus`、`App_CameraGetLastResult`、`App_CameraQualificationPrepare`、`App_CameraQualificationReceive`、`App_CameraQualificationProcess`、`App_CameraQualificationFlush`、`App_CameraQualificationGetStatus`。

### `CM7/Application/app_camera_lifecycle.c`

头依赖：`app_camera_lifecycle.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `Lifecycle_Role` :11 | 合法角色校验 | CM7/Application/app_camera_lifecycle.c:17::App_LifecycleValidate | 无 |
| `App_LifecycleValidate` :17 | 参数/格式/协议校验 | CM7/Application/app_camera_lifecycle.c:112::App_LifecycleMatchesReply<br>CM7/Application/app_camera_lifecycle.c:131::App_LifecycleMatchesCurrent<br>CM7/Application/app_camera_lifecycle.c:140::App_LifecycleMakeReply<br>CM7/Application/app_camera_lifecycle.c:172::App_LifecycleStage<br>CM7/Application/app_camera_lifecycle.c:247::App_LifecycleReplayBegin<br>CM7/Application/app_camera_lifecycle_replay.c:39::Replay_Bound<br>CM7/Application/app_camera_qualification.c:99::App_QualificationReceive<br>CM7/Application/app_camera_qualification.c:120::Qual_Window<br>CM7/Application/app_messages.c:193::App_LifecycleMessagesSend | `Lifecycle_Role` |
| `Lifecycle_SameKey` :100 | 身份比较 | CM7/Application/app_camera_lifecycle.c:247::App_LifecycleReplayBegin | 无 |
| `App_LifecycleMatchesReply` :112 | 完整身份匹配 | CM7/Application/app_camera_lifecycle.c:140::App_LifecycleMakeReply<br>CM7/Application/app_camera_lifecycle.c:206::App_LifecycleAcceptReply<br>CM7/Application/app_camera_lifecycle.c:265::App_LifecycleReplayComplete<br>CM7/Application/app_camera_lifecycle_replay.c:164::App_LifecycleTxConfirmConsumed<br>CM7/Application/app_camera_lifecycle_replay.c:376::App_LifecycleRxFlushReply<br>CM7/Application/app_camera_qualification.c:155::Qual_Reply | `App_LifecycleValidate` |
| `App_LifecycleMatchesCurrent` :131 | 完整身份匹配 | 无直接调用；须核对回调/导出/外部API | `App_LifecycleValidate` |
| `App_LifecycleMakeReply` :140 | ACK/FAULT独立交付 | CM7/Application/app_camera_qualification.c:155::Qual_Reply | `App_LifecycleMatchesReply`、`App_LifecycleValidate` |
| `App_LifecycleAllocateIds` :160 | 单调编号分配不回绕 | CM7/Application/app_camera_lifecycle_replay.c:89::App_LifecycleTxReserveIds | 无 |
| `App_LifecycleStage` :172 | 原身份待发登记 | CM7/Application/app_camera_lifecycle_replay.c:106::App_LifecycleTxStage<br>CM7/Application/app_camera_lifecycle_replay.c:218::Replay_StageReply | `App_LifecycleValidate`、`memcmp` |
| `App_LifecycleFlush` :184 | 原身份运输/失败保留 | CM7/Application/app_camera_lifecycle_replay.c:129::App_LifecycleTxFlush<br>CM7/Application/app_camera_lifecycle_replay.c:376::App_LifecycleRxFlushReply | `send` |
| `App_LifecycleAcceptReply` :206 | ACK/FAULT独立交付 | CM7/Application/app_camera_lifecycle_replay.c:140::App_LifecycleTxAcceptReply | `App_LifecycleMatchesReply` |
| `App_LifecycleClear` :220 | 完成对象清理 | CM7/Application/app_camera_lifecycle_replay.c:164::App_LifecycleTxConfirmConsumed<br>CM7/Application/app_camera_lifecycle_replay.c:376::App_LifecycleRxFlushReply | `memset` |
| `App_LifecycleCancelReady` :231 | 只读能力/准入 | CM7/Application/app_camera_lifecycle_replay.c:183::App_LifecycleTxCancelReady | `memset` |
| `App_LifecycleReplayBegin` :247 | 新epoch初始化 | CM7/Application/app_camera_lifecycle_replay.c:317::App_LifecycleRxPromote | `App_LifecycleValidate`、`Lifecycle_SameKey`、`memcmp` |
| `App_LifecycleReplayComplete` :265 | 校验请求/回复对应并冻结不可变终结回复 | CM7/Application/app_camera_lifecycle_replay.c:363::App_LifecycleRxComplete | `App_LifecycleMatchesReply`、`memcmp` |
| `App_LifecycleReplayGetReply` :279 | 读取状态/配置 | CM7/Application/app_camera_qualification.c:174::Qual_Submit | 无 |
| `App_LifecycleReplayRetire` :290 | 占用记录禁止成功退休 | 无直接调用；须核对回调/导出/外部API | 无 |

### `CM7/Application/app_camera_lifecycle.h`

头依赖：`sys_result.h`。

公开接口：`App_LifecycleValidate`、`App_LifecycleMatchesReply`、`App_LifecycleMatchesCurrent`、`App_LifecycleMakeReply`、`App_LifecycleAllocateIds`、`App_LifecycleStage`、`App_LifecycleFlush`、`App_LifecycleAcceptReply`、`App_LifecycleClear`、`App_LifecycleCancelReady`、`App_LifecycleReplayBegin`、`App_LifecycleReplayComplete`、`App_LifecycleReplayGetReply`、`App_LifecycleReplayRetire`。

### `CM7/Application/app_camera_lifecycle_replay.c`

头依赖：`app_camera_lifecycle_replay.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `Replay_Overlap` :18 | 参数别名检查 | CM7/Application/app_camera_lifecycle_replay.c:89::App_LifecycleTxReserveIds<br>CM7/Application/app_camera_lifecycle_replay.c:151::App_LifecycleTxGetCompleted<br>CM7/Application/app_camera_lifecycle_replay.c:226::App_LifecycleRxReceiveRequest<br>CM7/Application/app_camera_lifecycle_replay.c:317::App_LifecycleRxPromote<br>CM7/Application/app_camera_lifecycle_replay.c:401::App_LifecycleRxGetStatus | 无 |
| `Replay_Domain` :25 | 合法方向校验 | CM7/Application/app_camera_lifecycle_replay.c:72::App_LifecycleTxInit<br>CM7/Application/app_camera_lifecycle_replay.c:201::App_LifecycleRxInit | 无 |
| `Replay_Bound` :39 | 请求编号域绑定校验 | CM7/Application/app_camera_lifecycle_replay.c:106::App_LifecycleTxStage<br>CM7/Application/app_camera_lifecycle_replay.c:226::App_LifecycleRxReceiveRequest<br>CM7/Application/app_camera_lifecycle_replay.c:317::App_LifecycleRxPromote | `App_LifecycleValidate` |
| `Replay_TxEnter` :46 | 初始化/非重入门禁 | CM7/Application/app_camera_lifecycle_replay.c:89::App_LifecycleTxReserveIds<br>CM7/Application/app_camera_lifecycle_replay.c:106::App_LifecycleTxStage<br>CM7/Application/app_camera_lifecycle_replay.c:129::App_LifecycleTxFlush<br>CM7/Application/app_camera_lifecycle_replay.c:140::App_LifecycleTxAcceptReply<br>CM7/Application/app_camera_lifecycle_replay.c:164::App_LifecycleTxConfirmConsumed<br>CM7/Application/app_camera_lifecycle_replay.c:183::App_LifecycleTxCancelReady | 无 |
| `Replay_RxEnter` :59 | 初始化/非重入门禁 | CM7/Application/app_camera_lifecycle_replay.c:226::App_LifecycleRxReceiveRequest<br>CM7/Application/app_camera_lifecycle_replay.c:317::App_LifecycleRxPromote<br>CM7/Application/app_camera_lifecycle_replay.c:363::App_LifecycleRxComplete<br>CM7/Application/app_camera_lifecycle_replay.c:376::App_LifecycleRxFlushReply | 无 |
| `App_LifecycleTxInit` :72 | 首次初始化 | 无直接调用；须核对回调/导出/外部API | `Replay_Domain`、`memset` |
| `App_LifecycleTxReserveIds` :89 | 受控编号预留 | 无直接调用；须核对回调/导出/外部API | `App_LifecycleAllocateIds`、`Replay_Overlap`、`Replay_TxEnter` |
| `App_LifecycleTxStage` :106 | 原身份待发登记 | 无直接调用；须核对回调/导出/外部API | `App_LifecycleStage`、`Replay_Bound`、`Replay_TxEnter`、`memcmp` |
| `App_LifecycleTxFlush` :129 | 原身份运输/失败保留 | 无直接调用；须核对回调/导出/外部API | `App_LifecycleFlush`、`Replay_TxEnter` |
| `App_LifecycleTxAcceptReply` :140 | ACK/FAULT独立交付 | 无直接调用；须核对回调/导出/外部API | `App_LifecycleAcceptReply`、`Replay_TxEnter` |
| `App_LifecycleTxGetCompleted` :151 | 读取状态/配置 | 无直接调用；须核对回调/导出/外部API | `Replay_Overlap` |
| `App_LifecycleTxConfirmConsumed` :164 | 完整身份确认后消费事务 | 无直接调用；须核对回调/导出/外部API | `App_LifecycleClear`、`App_LifecycleMatchesReply`、`Replay_TxEnter`、`memcmp` |
| `App_LifecycleTxCancelReady` :183 | 取消匹配READY，编号不回退 | 无直接调用；须核对回调/导出/外部API | `App_LifecycleCancelReady`、`Replay_TxEnter`、`memcmp` |
| `App_LifecycleRxInit` :201 | 首次初始化 | CM7/Application/app_camera_qualification.c:71::App_QualificationInit | `Replay_Domain`、`memset` |
| `Replay_StageReply` :218 | 固定回复放入Pending | CM7/Application/app_camera_lifecycle_replay.c:226::App_LifecycleRxReceiveRequest<br>CM7/Application/app_camera_lifecycle_replay.c:363::App_LifecycleRxComplete | `App_LifecycleStage` |
| `App_LifecycleRxReceiveRequest` :226 | 重复/旧请求/候选/隔离判定 | CM7/Application/app_camera_qualification.c:99::App_QualificationReceive | `Replay_Bound`、`Replay_Overlap`、`Replay_RxEnter`、`Replay_StageReply`、`memcmp` |
| `App_LifecycleRxPromote` :317 | 临时Replay成功后接替和推进高水位 | CM7/Application/app_camera_qualification.c:421::App_QualificationProcess | `App_LifecycleReplayBegin`、`Replay_Bound`、`Replay_Overlap`、`Replay_RxEnter`、`memset` |
| `App_LifecycleRxComplete` :363 | 固定终结回复，不负责运输 | CM7/Application/app_camera_qualification.c:174::Qual_Submit | `App_LifecycleReplayComplete`、`Replay_RxEnter`、`Replay_StageReply` |
| `App_LifecycleRxFlushReply` :376 | ACK/FAULT独立交付 | CM7/Application/app_camera_qualification.c:474::App_QualificationFlush | `App_LifecycleClear`、`App_LifecycleFlush`、`App_LifecycleMatchesReply`、`Replay_RxEnter`、`memcmp` |
| `App_LifecycleRxGetStatus` :401 | 读取状态/配置 | CM7/Application/app_camera_qualification.c:174::Qual_Submit | `Replay_Overlap` |

### `CM7/Application/app_camera_lifecycle_replay.h`

头依赖：`app_camera_lifecycle.h`。

公开接口：`App_LifecycleTxInit`、`App_LifecycleTxReserveIds`、`App_LifecycleTxStage`、`App_LifecycleTxFlush`、`App_LifecycleTxAcceptReply`、`App_LifecycleTxGetCompleted`、`App_LifecycleTxConfirmConsumed`、`App_LifecycleTxCancelReady`、`App_LifecycleRxInit`、`App_LifecycleRxReceiveRequest`、`App_LifecycleRxPromote`、`App_LifecycleRxComplete`、`App_LifecycleRxFlushReply`、`App_LifecycleRxGetStatus`。

### `CM7/Application/app_camera_qualification.c`

头依赖：`app_camera_qualification.h`、`app_camera.h`、`app_camera_qualification_internal.h`、`platform_sync.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `Qual_Overlap` :30 | 参数别名检查 | CM7/Application/app_camera_qualification.c:99::App_QualificationReceive<br>CM7/Application/app_camera_qualification.c:505::App_QualificationGetStatus | 无 |
| `Qual_Index` :37 | 消费者索引映射 | CM7/Application/app_camera_qualification.c:99::App_QualificationReceive<br>CM7/Application/app_camera_qualification.c:474::App_QualificationFlush<br>CM7/Application/app_camera_qualification.c:505::App_QualificationGetStatus | 无 |
| `Qual_Enter` :47 | 初始化/非重入门禁 | CM7/Application/app_camera_qualification.c:99::App_QualificationReceive<br>CM7/Application/app_camera_qualification.c:421::App_QualificationProcess<br>CM7/Application/app_camera_qualification.c:474::App_QualificationFlush | 无 |
| `App_QualificationNextGeneration` :60 | 单调代次分配不回绕 | 无直接调用；须核对回调/导出/外部API | 无 |
| `App_QualificationInit` :71 | 首次初始化 | CM7/Application/app_camera_qualification.c:537::App_CameraQualificationPrepare | `App_LifecycleRxInit` |
| `App_QualificationReceive` :99 | 资格方向/参数校验后进入对应Rx | CM7/Application/app_camera_qualification.c:546::App_CameraQualificationReceive | `App_LifecycleRxReceiveRequest`、`App_LifecycleValidate`、`Qual_Enter`、`Qual_Index`、`Qual_Overlap` |
| `Qual_Window` :120 | 事务窗口完整身份校验 | CM7/Application/app_camera_qualification.c:155::Qual_Reply<br>CM7/Application/app_camera_qualification.c:421::App_QualificationProcess | `App_LifecycleValidate` |
| `Qual_Decide` :139 | 固定不可变回复决策 | CM7/Application/app_camera_qualification.c:248::Qual_Execute | 无 |
| `Qual_Reply` :155 | ACK/FAULT独立交付 | CM7/Application/app_camera_qualification.c:174::Qual_Submit | `App_LifecycleMakeReply`、`App_LifecycleMatchesReply`、`Qual_Window` |
| `Qual_Submit` :174 | 冻结决策提交与缓存一致性核对 | CM7/Application/app_camera_qualification.c:421::App_QualificationProcess | `App_LifecycleReplayGetReply`、`App_LifecycleRxComplete`、`App_LifecycleRxGetStatus`、`Qual_Reply`、`memcmp`、`memset` |
| `Qual_OtherWork` :229 | 检查其他资格/事务工作 | CM7/Application/app_camera_qualification.c:248::Qual_Execute | 无 |
| `Qual_Execute` :248 | 四种操作资格决策及资源Ops | CM7/Application/app_camera_qualification.c:421::App_QualificationProcess | `Qual_Decide`、`Qual_OtherWork`、`consume_stop_result`、`idle`、`read_stop_result`、`released`、`request_stop`、`start`、`state` |
| `App_QualificationProcess` :421 | 推进所属模块工作/错误 | CM7/Application/app_camera_qualification.c:555::App_CameraQualificationProcess | `App_LifecycleRxPromote`、`Qual_Enter`、`Qual_Execute`、`Qual_Submit`、`Qual_Window` |
| `App_QualificationFlush` :474 | 原身份运输/失败保留 | CM7/Application/app_camera_qualification.c:564::App_CameraQualificationFlush | `App_LifecycleRxFlushReply`、`Qual_Enter`、`Qual_Index` |
| `App_QualificationGetStatus` :505 | 读取状态/配置 | CM7/Application/app_camera_qualification.c:573::App_CameraQualificationGetStatus | `Qual_Index`、`Qual_Overlap` |
| `Qual_AppAccess` :528 | 检查Camera任务owner再访问core | CM7/Application/app_camera_qualification.c:537::App_CameraQualificationPrepare<br>CM7/Application/app_camera_qualification.c:546::App_CameraQualificationReceive<br>CM7/Application/app_camera_qualification.c:555::App_CameraQualificationProcess<br>CM7/Application/app_camera_qualification.c:564::App_CameraQualificationFlush<br>CM7/Application/app_camera_qualification.c:573::App_CameraQualificationGetStatus | `App_CameraQualificationAccess`、`App_MessagesIsOwner`、`Platform_SyncGetException` |
| `App_CameraQualificationPrepare` :537 | 前置准备与校验 | 无直接调用；须核对回调/导出/外部API | `App_QualificationInit`、`Qual_AppAccess` |
| `App_CameraQualificationReceive` :546 | Camera owner包装的资格接收 | 无直接调用；须核对回调/导出/外部API | `App_QualificationReceive`、`Qual_AppAccess` |
| `App_CameraQualificationProcess` :555 | 推进所属模块工作/错误 | 无直接调用；须核对回调/导出/外部API | `App_QualificationProcess`、`Qual_AppAccess` |
| `App_CameraQualificationFlush` :564 | 原身份运输/失败保留 | 无直接调用；须核对回调/导出/外部API | `App_QualificationFlush`、`Qual_AppAccess` |
| `App_CameraQualificationGetStatus` :573 | 读取状态/配置 | 无直接调用；须核对回调/导出/外部API | `App_QualificationGetStatus`、`Qual_AppAccess` |

### `CM7/Application/app_camera_qualification.h`

头依赖：`app_camera_lifecycle_replay.h`。

公开接口：`App_QualificationNextGeneration`、`App_QualificationInit`、`App_QualificationReceive`、`App_QualificationProcess`、`App_QualificationFlush`、`App_QualificationGetStatus`。

### `CM7/Application/app_camera_qualification_internal.h`

头依赖：`app_camera_qualification.h`。

公开接口：`App_CameraQualificationAccess`。

### `CM7/Application/app_events.c`

头依赖：`app_events.h`、`usb_device.h`、`camera_capture.h`、`platform_sync.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `App_EventsInit` :49 | 首次初始化 | CM7/Application/app_tasks.c:18::App_TasksCreate | `CameraCapture_SetEventCallback`、`Platform_SyncGetException`、`RuntimeEvent_Bind`、`USB_Device_SetEventCallback`、`xTaskGetSchedulerState` |
| `App_EventsAttach` :80 | 绑定任务通知端点 | CM7/Application/Tasks/camera_task.c:47::CameraTask_Create<br>CM7/Application/Tasks/control_task.c:80::ControlTask_Create<br>CM7/Application/Tasks/ipc_task.c:35::IpcTask_Create<br>CM7/Application/Tasks/storage_task.c:21::StorageTask_Create<br>CM7/Application/Tasks/usb_task.c:51::UsbTask_Create | `Platform_SyncGetException`、`xTaskGetSchedulerState` |
| `App_EventsRecordInitialization` :102 | 首次初始化 | CM7/Application/app_tasks.c:82::App_TasksRecordInitialization | `Platform_SyncGetException`、`xTaskGetSchedulerState` |
| `App_EventsReleaseBoot` :131 | Control临界区一次广播BOOT及pending | CM7/Application/app_tasks.c:100::App_TasksReleaseBoot | `Platform_SyncGetException`、`taskENTER_CRITICAL`、`taskEXIT_CRITICAL`、`xTaskGetCurrentTaskHandle`、`xTaskGetSchedulerState`、`xTaskNotify` |
| `App_EventsWait` :162 | 合法owner按期限取通知并保留超时语义 | CM7/Application/Tasks/camera_task.c:71::CameraTask_Run<br>CM7/Application/Tasks/control_task.c:104::ControlTask_Run<br>CM7/Application/Tasks/ipc_task.c:63::IpcTask_Run<br>CM7/Application/Tasks/storage_task.c:31::StorageTask_Run<br>CM7/Application/Tasks/usb_task.c:75::UsbTask_Run | `Platform_SyncGetBasepri`、`Platform_SyncGetException`、`Platform_SyncGetPrimask`、`pdMS_TO_TICKS`、`xTaskGetCurrentTaskHandle`、`xTaskGetSchedulerState`、`xTaskNotifyWait` |
| `AppEvents_PreparePost` :192 | 前置准备与校验 | CM7/Application/app_events.c:217::AppEvents_Post<br>CM7/Application/app_events.c:238::AppEvents_PostFromIsr | `Platform_SyncDisableIrq`、`Platform_SyncGetPrimask`、`Platform_SyncRestoreIrq` |
| `AppEvents_Post` :217 | 线程唤醒或BOOT前pending合并 | 无直接调用；须核对回调/导出/外部API | `AppEvents_PreparePost`、`Platform_SyncGetBasepri`、`Platform_SyncGetException`、`Platform_SyncGetPrimask`、`xTaskNotify` |
| `AppEvents_PostFromIsr` :238 | 只读状态/owner检查 | 无直接调用；须核对回调/导出/外部API | `AppEvents_PreparePost`、`Platform_SyncGetException`、`Platform_SyncGetIrqPriority`、`portYIELD_FROM_ISR`、`xTaskNotifyFromISR` |
| `AppEvents_UsbNotify` :268 | USB事件task/ISR分流 | 无直接调用；须核对回调/导出/外部API | `Platform_SyncGetException`、`RuntimeEvent_Post`、`RuntimeEvent_PostFromIsr`、`else` |
| `App_EventsIsOwner` :281 | 只读状态/owner检查 | CM7/Application/app_frame_dispatcher.c:51::App_FrameDispatcherBegin<br>CM7/Application/app_frame_dispatcher.c:61::App_FrameDispatcherPublish<br>CM7/Application/app_frame_dispatcher.c:91::App_FrameDispatcherCanAcquire<br>CM7/Application/app_frame_dispatcher.c:97::App_FrameDispatcherNeedsStorage<br>CM7/Application/app_messages.c:30::App_MessagesIsOwner<br>CM7/Application/app_messages.c:193::App_LifecycleMessagesSend<br>CM7/Application/app_messages.c:213::App_LifecycleMessagesReceive<br>CM7/Application/app_status.c:32::App_StatusPublishCamera<br>CM7/Application/app_status.c:88::App_StatusPublishUsb<br>CM7/Application/app_status.c:119::App_StatusPublishControl<br>CM7/Application/app_status.c:149::App_StatusGetSnapshotAt<br>CM7/Application/app_storage.c:237::App_StorageOfferFrame<br>CM7/Application/app_storage.c:334::App_StorageProcessReturns<br>CM7/Application/app_storage.c:379::App_StorageProcess<br>CM7/Application/Tasks/ipc_task.c:119::IpcTask_IsOwner | `Platform_SyncGetBasepri`、`Platform_SyncGetException`、`Platform_SyncGetPrimask`、`xTaskGetCurrentTaskHandle`、`xTaskGetSchedulerState` |
| `AppEvents_CameraNotify` :290 | Media事件task/ISR分流 | 无直接调用；须核对回调/导出/外部API | `Platform_SyncGetException`、`RuntimeEvent_Post`、`RuntimeEvent_PostFromIsr`、`else` |

### `CM7/Application/app_events.h`

头依赖：`runtime_event.h`、`FreeRTOS.h`、`task.h`。

公开接口：`App_EventsInit`、`App_EventsAttach`、`App_EventsRecordInitialization`、`App_EventsReleaseBoot`、`App_EventsWait`、`App_EventsIsOwner`。

### `CM7/Application/app_frame_dispatcher.c`

头依赖：`app_frame_dispatcher.h`、`app_events.h`、`platform_time.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `App_FrameDispatcherValidateOutput` :34 | 参数/格式/协议校验 | CM7/Application/app_uvc_bridge.c:500::App_UvcBridgePrepareOutput | 无 |
| `App_FrameDispatcherBegin` :51 | 新epoch初始化 | CM7/Application/app_uvc_bridge.c:111::App_UvcBridgeCameraBegin | `App_EventsIsOwner` |
| `App_FrameDispatcherPublish` :61 | 状态缓存发布 | CM7/Application/app_uvc_bridge.c:137::App_UvcBridgeCameraProcess | `App_EventsIsOwner`、`Platform_TimeGetMs`、`offer`、`wants` |
| `App_FrameDispatcherRequestAi` :78 | 业务请求入队或明确拒绝 | 无直接调用；须核对回调/导出/外部API | 无 |
| `Dispatcher_StorageOffer` :84 | 同步调用StorageOffer，不转移原始帧 | 无直接调用；须核对回调/导出/外部API | `App_StorageOfferFrame` |
| `App_FrameDispatcherCanAcquire` :91 | 只读能力/准入 | CM7/Application/app_uvc_bridge.c:137::App_UvcBridgeCameraProcess | `App_EventsIsOwner` |
| `App_FrameDispatcherNeedsStorage` :97 | Camera上下文查询Storage是否需要帧 | CM7/Application/app_uvc_bridge.c:137::App_UvcBridgeCameraProcess | `App_EventsIsOwner`、`Platform_TimeGetMs`、`wants` |
| `Dispatcher_StorageWants` :104 | 调用Storage wants端口 | 无直接调用；须核对回调/导出/外部API | `App_StorageWantsFrame` |

### `CM7/Application/app_frame_dispatcher.h`

头依赖：`app_storage.h`、`video_config_types.h`。

公开接口：`App_FrameDispatcherValidateOutput`、`App_FrameDispatcherBegin`、`App_FrameDispatcherCanAcquire`、`App_FrameDispatcherNeedsStorage`、`App_FrameDispatcherPublish`、`App_FrameDispatcherRequestAi`。

### `CM7/Application/app_main.c`

头依赖：`app_main.h`、`framework_init.h`、`usb_mode.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `App_Init` :6 | 首次初始化 | 无直接调用；须核对回调/导出/外部API | `USB_Mode_GetMode` |

### `CM7/Application/app_main.h`

头依赖：`sys_result.h`。

公开接口：`App_Init`。

### `CM7/Application/app_messages.c`

头依赖：`app_messages.h`、`app_events.h`、`queue.h`、`platform_sync.h`、`framework_init.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `App_MessagesIsOwner` :30 | 只读状态/owner检查 | CM7/Application/app_camera_qualification.c:528::Qual_AppAccess<br>CM7/Application/app_messages.c:56::App_MessagesSend<br>CM7/Application/app_messages.c:85::App_MessagesReceive<br>CM7/Application/app_uvc_bridge.c:111::App_UvcBridgeCameraBegin<br>CM7/Application/app_uvc_bridge.c:129::App_UvcBridgeCameraIsIdle<br>CM7/Application/app_uvc_bridge.c:137::App_UvcBridgeCameraProcess<br>CM7/Application/app_uvc_bridge.c:268::App_UvcBridgeUsbProcess<br>CM7/Application/app_uvc_bridge.c:419::App_UvcBridgeUsbIsIdle<br>CM7/Application/Tasks/camera_task.c:112::CameraTask_Process<br>CM7/Application/Tasks/control_task.c:147::ControlTask_Process<br>CM7/Application/Tasks/control_task.c:268::ControlTask_Request<br>CM7/Application/Tasks/usb_task.c:118::UsbTask_Process | `App_EventsIsOwner` |
| `App_MessagesInit` :36 | 首次初始化 | CM7/Application/app_tasks.c:18::App_TasksCreate | `Platform_SyncGetException`、`xQueueCreateStatic`、`xTaskGetSchedulerState` |
| `App_MessagesSend` :56 | 通道/发送源权限校验后零等待复制并通知 | CM7/Application/app_uvc_bridge.c:268::App_UvcBridgeUsbProcess<br>CM7/Application/app_uvc_bridge.c:432::Bridge_CameraFlush<br>CM7/Application/app_uvc_bridge.c:484::Bridge_UsbReturn<br>CM7/Application/Tasks/camera_task.c:141::CameraTask_Reply<br>CM7/Application/Tasks/control_task.c:147::ControlTask_Process<br>CM7/Application/Tasks/control_task.c:268::ControlTask_Request<br>CM7/Application/Tasks/usb_task.c:169::UsbTask_Reply | `App_MessagesIsOwner`、`Platform_SyncGetException`、`RuntimeEvent_Post`、`xQueueSend` |
| `App_MessagesReceive` :85 | 接收owner校验、出队和对端重试唤醒 | CM7/Application/app_uvc_bridge.c:137::App_UvcBridgeCameraProcess<br>CM7/Application/app_uvc_bridge.c:268::App_UvcBridgeUsbProcess<br>CM7/Application/Tasks/camera_task.c:112::CameraTask_Process<br>CM7/Application/Tasks/control_task.c:147::ControlTask_Process<br>CM7/Application/Tasks/usb_task.c:118::UsbTask_Process | `App_MessagesIsOwner`、`RuntimeEvent_Post`、`else`、`xQueueReceive` |
| `Lifecycle_Task` :138 | 协议角色/任务映射 | CM7/Application/app_messages.c:193::App_LifecycleMessagesSend<br>CM7/Application/app_messages.c:213::App_LifecycleMessagesReceive | 无 |
| `Lifecycle_Receiver` :150 | 通道接收角色映射 | CM7/Application/app_messages.c:193::App_LifecycleMessagesSend<br>CM7/Application/app_messages.c:213::App_LifecycleMessagesReceive | 无 |
| `Lifecycle_Context` :162 | 上下文校验 | CM7/Application/app_messages.c:193::App_LifecycleMessagesSend<br>CM7/Application/app_messages.c:213::App_LifecycleMessagesReceive | `Platform_SyncGetBasepri`、`Platform_SyncGetException`、`Platform_SyncGetPrimask`、`xTaskGetSchedulerState` |
| `App_LifecycleMessagesInit` :169 | 首次初始化 | 无直接调用；须核对回调/导出/外部API | `Platform_SyncGetException`、`Platform_SyncGetPrimask`、`xQueueCreateStatic`、`xTaskGetSchedulerState` |
| `App_LifecycleMessagesSend` :193 | 格式/目标/运行上下文和发送owner检查后入队 | 无直接调用；须核对回调/导出/外部API | `App_EventsIsOwner`、`App_LifecycleValidate`、`Lifecycle_Context`、`Lifecycle_Receiver`、`Lifecycle_Task`、`RuntimeEvent_Post`、`xQueueSend` |
| `App_LifecycleMessagesReceive` :213 | 目标owner校验后出队并唤醒对端 | 无直接调用；须核对回调/导出/外部API | `App_EventsIsOwner`、`Lifecycle_Context`、`Lifecycle_Receiver`、`Lifecycle_Task`、`RuntimeEvent_Post`、`else`、`xQueueReceive` |

### `CM7/Application/app_messages.h`

头依赖：`runtime_event.h`、`camera_config.h`、`app_camera_lifecycle.h`。

公开接口：`App_MessagesInit`、`App_MessagesSend`、`App_MessagesReceive`、`App_MessagesIsOwner`、`App_LifecycleMessagesInit`、`App_LifecycleMessagesSend`、`App_LifecycleMessagesReceive`。

### `CM7/Application/app_status.c`

头依赖：`app_status.h`、`app_events.h`、`platform_time.h`、`ipc_config.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `App_StatusPublishCamera` :32 | 状态缓存发布 | CM7/Application/Tasks/camera_task.c:157::CameraTask_PublishStatus | `AppStatus_Format`、`AppStatus_MemoryValid`、`App_EventsIsOwner`、`IPC_Application_Validate`、`Platform_TimeGetMs`、`taskENTER_CRITICAL`、`taskEXIT_CRITICAL` |
| `App_StatusPublishUsb` :88 | 状态缓存发布 | CM7/Application/Tasks/usb_task.c:185::UsbTask_PublishStatus | `AppStatus_MemoryValid`、`App_EventsIsOwner`、`Platform_TimeGetMs`、`taskENTER_CRITICAL`、`taskEXIT_CRITICAL` |
| `App_StatusPublishControl` :119 | 状态缓存发布 | CM7/Application/Tasks/control_task.c:297::ControlTask_PublishStatus | `AppStatus_MemoryValid`、`App_EventsIsOwner`、`Platform_TimeGetMs`、`taskENTER_CRITICAL`、`taskEXIT_CRITICAL` |
| `App_StatusGetSnapshotAt` :149 | 读取状态/配置 | CM7/Application/app_status.c:238::App_StatusGetSnapshot<br>CM7/Application/Tasks/ipc_task.c:63::IpcTask_Run | `AppStatus_Age`、`AppStatus_MemoryValid`、`App_EventsIsOwner`、`Platform_TimeGetMs`、`taskENTER_CRITICAL`、`taskEXIT_CRITICAL` |
| `AppStatus_MemoryValid` :197 | 本地地址/共享区隔离校验 | CM7/Application/app_status.c:32::App_StatusPublishCamera<br>CM7/Application/app_status.c:88::App_StatusPublishUsb<br>CM7/Application/app_status.c:119::App_StatusPublishControl<br>CM7/Application/app_status.c:149::App_StatusGetSnapshotAt | 无 |
| `AppStatus_Format` :212 | 格式显式映射 | CM7/Application/app_status.c:32::App_StatusPublishCamera | 无 |
| `AppStatus_Age` :225 | 状态年龄与饱和过期 | CM7/Application/app_status.c:149::App_StatusGetSnapshotAt | 无 |
| `App_StatusGetSnapshot` :238 | 读取状态/配置 | 无直接调用；须核对回调/导出/外部API | `App_StatusGetSnapshotAt` |

### `CM7/Application/app_status.h`

头依赖：`ipc_application.h`、`camera_config.h`。

公开接口：`App_StatusPublishCamera`、`App_StatusPublishUsb`、`App_StatusPublishControl`、`App_StatusGetSnapshot`、`App_StatusGetSnapshotAt`。

### `CM7/Application/app_storage.c`

头依赖：`app_storage.h`、`app_events.h`、`video_config.h`、`platform_time.h`、`jpeg_frame.h`、`mjpeg_avi.h`、`camera_mjpeg_fifo.h`、`platform_sync.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `App_StoragePrepare` :156 | 前置准备与校验 | CM7/Application/Tasks/storage_task.c:31::StorageTask_Run | `Platform_SyncMemoryBarrier`、`Storage_PublishStatus` |
| `App_StorageIsReady` :174 | 只读状态/owner检查 | CM7/Application/Tasks/storage_task.c:31::StorageTask_Run | 无 |
| `App_StorageStartRecording` :180 | 记录准备/故障事实 | 无直接调用；须核对回调/导出/外部API | `Storage_Request` |
| `App_StorageRequestSnapshot` :183 | 业务请求入队或明确拒绝 | 无直接调用；须核对回调/导出/外部API | `Storage_Request` |
| `App_StorageStop` :186 | 请求/执行停止，详见模块调用链 | 无直接调用；须核对回调/导出/外部API | `Platform_SyncDisableIrq`、`Platform_SyncGetPrimask`、`Platform_SyncMemoryBarrier`、`Platform_SyncRestoreIrq`、`Storage_Request` |
| `App_StorageRecover` :203 | 提交恢复请求 | 无直接调用；须核对回调/导出/外部API | `Storage_Request` |
| `App_StorageGetStatus` :206 | 读取状态/配置 | 无直接调用；须核对回调/导出/外部API | `Platform_SyncDisableIrq`、`Platform_SyncGetPrimask`、`Platform_SyncMemoryBarrier`、`Platform_SyncRestoreIrq`、`Storage_Context` |
| `App_StorageWantsFrame` :221 | 受保护读取accepting、空槽和速率条件 | CM7/Application/app_frame_dispatcher.c:104::Dispatcher_StorageWants<br>CM7/Application/app_storage.c:237::App_StorageOfferFrame | `Platform_SyncDisableIrq`、`Platform_SyncGetPrimask`、`Platform_SyncMemoryBarrier`、`Platform_SyncRestoreIrq` |
| `App_StorageOfferFrame` :237 | 预留槽→Retain→提交或待归还 | CM7/Application/app_frame_dispatcher.c:84::Dispatcher_StorageOffer | `App_EventsIsOwner`、`App_StorageWantsFrame`、`CameraMjpegFifo_RetainFrame`、`Platform_SyncDisableIrq`、`Platform_SyncGetPrimask`、`Platform_SyncMemoryBarrier`、`Platform_SyncRestoreIrq`、`Platform_TimeGetMs`、`RuntimeEvent_Post` |
| `App_StorageProcessReturns` :334 | Camera实际ReleaseLease后匹配提交FREE | CM7/Application/app_uvc_bridge.c:137::App_UvcBridgeCameraProcess | `App_EventsIsOwner`、`CameraMjpegFifo_ReleaseLease`、`Platform_SyncDisableIrq`、`Platform_SyncGetPrimask`、`Platform_SyncMemoryBarrier`、`Platform_SyncRestoreIrq` |
| `App_StorageProcess` :379 | 推进所属模块工作/错误 | CM7/Application/Tasks/storage_task.c:31::StorageTask_Run | `App_EventsIsOwner`、`JpegFrame_Crc32`、`JpegFrame_Validate`、`MjpegAvi_AppendFrame`、`Platform_SyncDisableIrq`、`Platform_SyncGetPrimask`、`Platform_SyncMemoryBarrier`、`Platform_SyncRestoreIrq`、`Platform_TimeGetMs`、`RuntimeEvent_Post`、`StorageFS_Sync`、`Storage_CloseAvi`、`Storage_Fail`、`Storage_Finish`、`Storage_HandleEvent`、`Storage_OpenNew`、`Storage_PublishStatus`、`Storage_Release`、`Storage_Snapshot` |
| `Storage_Context` :567 | 上下文校验 | CM7/Application/app_storage.c:206::App_StorageGetStatus<br>CM7/Application/app_storage.c:573::Storage_Request | `Platform_SyncGetBasepri`、`Platform_SyncGetException`、`Platform_SyncGetPrimask` |
| `Storage_Request` :573 | 受保护命令入环并通知 | CM7/Application/app_storage.c:180::App_StorageStartRecording<br>CM7/Application/app_storage.c:183::App_StorageRequestSnapshot<br>CM7/Application/app_storage.c:186::App_StorageStop<br>CM7/Application/app_storage.c:203::App_StorageRecover | `Platform_SyncDisableIrq`、`Platform_SyncGetPrimask`、`Platform_SyncMemoryBarrier`、`Platform_SyncRestoreIrq`、`RuntimeEvent_Post`、`Storage_Context`、`VideoConfig_GetDefault` |
| `Storage_HandleEvent` :614 | 按四命令分派Start/Stop/Recover | CM7/Application/app_storage.c:379::App_StorageProcess | `Storage_OnRecover`、`Storage_OnStart`、`Storage_OnStop` |
| `Storage_OnStart` :638 | 挂载/预算/会话和接收状态 | CM7/Application/app_storage.c:614::Storage_HandleEvent | `Platform_SyncDisableIrq`、`Platform_SyncGetPrimask`、`Platform_SyncMemoryBarrier`、`Platform_SyncRestoreIrq`、`Platform_TimeGetMs`、`StorageFS_GetSpace`、`StorageFS_Mount`、`Storage_Fail`、`Storage_Transition` |
| `Storage_OnStop` :688 | 停止接收并登记排空 | CM7/Application/app_storage.c:614::Storage_HandleEvent | `Storage_Transition` |
| `Storage_OnRecover` :700 | 按文件系统状态恢复 | CM7/Application/app_storage.c:614::Storage_HandleEvent | `Platform_SyncDisableIrq`、`Platform_SyncGetPrimask`、`Platform_SyncMemoryBarrier`、`Platform_SyncRestoreIrq`、`StorageFS_Close`、`StorageFS_GetDiagnostics`、`StorageFS_Recover`、`StorageFS_Unmount`、`Storage_Transition` |
| `Storage_OpenNew` :772 | 有界文件号分配与建文件 | CM7/Application/app_storage.c:379::App_StorageProcess<br>CM7/Application/app_storage.c:828::Storage_Snapshot | `MjpegAvi_Begin`、`StorageFS_Open`、`VideoConfig_GetDefault`、`memcpy` |
| `Storage_Write` :810 | 分块写入检查部分写入 | CM7/Application/app_storage.c:828::Storage_Snapshot | `StorageFS_Write` |
| `Storage_Snapshot` :828 | 双Span保存、同步、回读及CRC | CM7/Application/app_storage.c:379::App_StorageProcess | `JpegFrame_Crc32`、`StorageFS_Close`、`StorageFS_GetSize`、`StorageFS_Open`、`StorageFS_Read`、`StorageFS_Sync`、`Storage_OpenNew`、`Storage_Write`、`memcmp`、`memcpy` |
| `Storage_CloseAvi` :900 | AVI Finish成功再Close并统计 | CM7/Application/app_storage.c:379::App_StorageProcess<br>CM7/Application/app_storage.c:920::Storage_Finish | `MjpegAvi_Finish`、`StorageFS_Close` |
| `Storage_Finish` :920 | 分别记录业务与清理结果并收尾 | CM7/Application/app_storage.c:379::App_StorageProcess<br>CM7/Application/app_storage.c:954::Storage_Fail<br>CM7/Platform/storage_fs.c:86::StorageFS_Mount<br>CM7/Platform/storage_fs.c:254::StorageFS_Open<br>CM7/Platform/storage_fs.c:309::StorageFS_Read<br>CM7/Platform/storage_fs.c:333::StorageFS_Write<br>CM7/Platform/storage_fs.c:403::StorageFS_Sync<br>CM7/Platform/storage_fs.c:418::StorageFS_Close<br>CM7/Platform/storage_fs.c:440::StorageFS_Seek<br>CM7/Platform/storage_fs.c:469::StorageFS_GetSize<br>CM7/Platform/storage_fs.c:488::StorageFS_GetSpace<br>CM7/Platform/storage_fs.c:509::StorageFS_Mkdir<br>CM7/Platform/storage_fs.c:525::StorageFS_Remove | `Platform_SyncDisableIrq`、`Platform_SyncGetPrimask`、`Platform_SyncMemoryBarrier`、`Platform_SyncRestoreIrq`、`StorageFS_Close`、`StorageFS_GetDiagnostics`、`StorageFS_Unmount`、`Storage_CloseAvi`、`Storage_Transition` |
| `Storage_Fail` :954 | 关入口、排队帧转待归还、错误收尾 | CM7/Application/app_storage.c:379::App_StorageProcess<br>CM7/Application/app_storage.c:638::Storage_OnStart | `Platform_SyncDisableIrq`、`Platform_SyncGetPrimask`、`Platform_SyncMemoryBarrier`、`Platform_SyncRestoreIrq`、`RuntimeEvent_Post`、`Storage_Finish` |
| `Storage_Release` :988 | WRITING转RETURN_PENDING，未真实归还 | CM7/Application/app_storage.c:379::App_StorageProcess | `Platform_SyncDisableIrq`、`Platform_SyncGetPrimask`、`Platform_SyncMemoryBarrier`、`Platform_SyncRestoreIrq`、`RuntimeEvent_Post` |
| `Storage_Transition` :1011 | 提交业务状态及投影 | CM7/Application/app_storage.c:638::Storage_OnStart<br>CM7/Application/app_storage.c:688::Storage_OnStop<br>CM7/Application/app_storage.c:700::Storage_OnRecover<br>CM7/Application/app_storage.c:920::Storage_Finish | 无 |
| `Storage_PublishStatus` :1018 | 状态缓存发布 | CM7/Application/app_storage.c:156::App_StoragePrepare<br>CM7/Application/app_storage.c:379::App_StorageProcess | `Platform_SyncDisableIrq`、`Platform_SyncGetPrimask`、`Platform_SyncMemoryBarrier`、`Platform_SyncRestoreIrq` |

### `CM7/Application/app_storage.h`

头依赖：`camera_config.h`、`storage_fs.h`、`camera_mjpeg_fifo.h`。

公开接口：`App_StoragePrepare`、`App_StorageIsReady`、`App_StorageStartRecording`、`App_StorageRequestSnapshot`、`App_StorageStop`、`App_StorageRecover`、`App_StorageGetStatus`、`App_StorageWantsFrame`、`App_StorageOfferFrame`、`App_StorageProcess`、`App_StorageProcessReturns`。

### `CM7/Application/app_tasks.c`

头依赖：`app_tasks.h`、`app_events.h`、`app_messages.h`、`Tasks/camera_task.h`、`Tasks/usb_task.h`、`Tasks/control_task.h`、`Tasks/ipc_task.h`、`Tasks/storage_task.h`、`platform_sync.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `App_TasksCreate` :18 | 静态创建并绑定owner | CM7/Core/Src/main.c:108::main | `App_EventsInit`、`App_MessagesInit`、`CameraTask_Create`、`ControlTask_Create`、`IpcTask_Create`、`Platform_SyncGetException`、`Platform_SyncGetPrimask`、`StorageTask_Create`、`UsbTask_Create`、`xTaskGetSchedulerState` |
| `App_TasksRecordInitialization` :82 | 首次初始化 | CM7/Core/Src/main.c:108::main | `App_EventsRecordInitialization` |
| `App_TasksReleaseBoot` :100 | 协议角色/任务映射 | CM7/Application/Tasks/control_task.c:104::ControlTask_Run | `App_EventsReleaseBoot` |

### `CM7/Application/app_tasks.h`

头依赖：`sys_result.h`。

公开接口：`App_TasksCreate`、`App_TasksRecordInitialization`、`App_TasksReleaseBoot`。

### `CM7/Application/app_usb.c`

头依赖：`app_usb.h`、`usb_mode.h`、`usb_uvc.h`、`app_uvc_bridge.h`、`platform_time.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `App_USBSetDiagnosticObserver` :34 | 诊断投影 | CM7/Application/Tasks/usb_task.c:75::UsbTask_Run | 无 |
| `App_USBCanAcceptCommand` :40 | 只读命令准入 | CM7/Application/Tasks/usb_task.c:118::UsbTask_Process | 无 |
| `App_USBHandleCommand` :46 | 业务START/STOP/未知命令和ACK记录 | CM7/Application/Tasks/usb_task.c:118::UsbTask_Process | `App_USBObserve`、`Platform_TimeGetMs`、`USB_Mode_Start`、`USB_UVC_GetCompletedFrameCount` |
| `App_USBCanTransfer` :93 | 只读能力/准入 | CM7/Application/Tasks/usb_task.c:118::UsbTask_Process | 无 |
| `App_USBIsStopping` :99 | 只读状态/owner检查 | CM7/Application/Tasks/usb_task.c:118::UsbTask_Process | 无 |
| `App_USBProcess` :105 | 推进所属模块工作/错误 | CM7/Application/Tasks/usb_task.c:118::UsbTask_Process | `App_USBObserve`、`USB_Mode_GetLastResult`、`USB_Mode_Process` |
| `App_USBFinishRound` :124 | Bridge空闲后Mode Stop及独立ACK/FAULT和FPS | CM7/Application/Tasks/usb_task.c:118::UsbTask_Process | `App_USBObserve`、`App_UvcBridgeUsbIsIdle`、`Platform_TimeGetMs`、`USB_Mode_Stop`、`USB_UVC_GetCompletedFrameCount`、`USB_UVC_GetStreamStatus` |
| `App_USBGetPendingAck` :176 | 读取待交付记录 | CM7/Application/Tasks/usb_task.c:169::UsbTask_Reply | 无 |
| `App_USBConfirmAckEnqueued` :185 | 匹配完成/发送确认 | CM7/Application/Tasks/usb_task.c:169::UsbTask_Reply | 无 |
| `App_USBGetPendingFault` :191 | 读取待交付记录 | CM7/Application/Tasks/usb_task.c:169::UsbTask_Reply | 无 |
| `App_USBConfirmFaultEnqueued` :204 | 匹配完成/发送确认 | CM7/Application/Tasks/usb_task.c:169::UsbTask_Reply | 无 |
| `App_USBGetStatus` :210 | 读取状态/配置 | CM7/Application/app_usb.c:228::App_USBObserve | 无 |
| `App_USBGetLastResult` :222 | 读取状态/配置 | 无直接调用；须核对回调/导出/外部API | 无 |
| `App_USBObserve` :228 | 诊断投影 | CM7/Application/app_usb.c:46::App_USBHandleCommand<br>CM7/Application/app_usb.c:105::App_USBProcess<br>CM7/Application/app_usb.c:124::App_USBFinishRound | `App_USBGetStatus`、`observer` |

### `CM7/Application/app_usb.h`

头依赖：`app_messages.h`。

公开接口：`App_USBSetDiagnosticObserver`、`App_USBCanAcceptCommand`、`App_USBHandleCommand`、`App_USBCanTransfer`、`App_USBIsStopping`、`App_USBProcess`、`App_USBFinishRound`、`App_USBGetPendingAck`、`App_USBConfirmAckEnqueued`、`App_USBGetPendingFault`、`App_USBConfirmFaultEnqueued`、`App_USBGetStatus`、`App_USBGetLastResult`。

### `CM7/Application/app_uvc_bridge.c`

头依赖：`app_uvc_bridge.h`、`app_frame_dispatcher.h`、`platform_time.h`、`video_config.h`、`usb_mode.h`、`framework_init.h`、`camera_capture.h`、`usb_uvc.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `App_UvcBridgeConfigure` :89 | 一次验证YUY2/JPEG并冻结Bridge格式 | CM7/Application/app_uvc_bridge.c:500::App_UvcBridgePrepareOutput | `CameraConfig_GetFrameSize` |
| `App_UvcBridgeCameraBegin` :111 | 新epoch初始化 | CM7/Application/app_camera.c:508::App_CameraStartResource | `App_FrameDispatcherBegin`、`App_MessagesIsOwner`、`App_UvcBridgeCameraIsIdle` |
| `App_UvcBridgeCameraIsIdle` :129 | 只读状态/owner检查 | CM7/Application/app_camera.c:365::App_CameraFinishRound<br>CM7/Application/app_uvc_bridge.c:111::App_UvcBridgeCameraBegin | `App_MessagesIsOwner` |
| `App_UvcBridgeCameraProcess` :137 | Camera归还/Request/Cancel/Return/SD-only/Offer | CM7/Application/Tasks/camera_task.c:112::CameraTask_Process | `App_FrameDispatcherCanAcquire`、`App_FrameDispatcherNeedsStorage`、`App_FrameDispatcherPublish`、`App_MessagesIsOwner`、`App_MessagesReceive`、`App_StorageProcessReturns`、`Bridge_CameraFlush`、`Bridge_CameraRelease`、`Bridge_SameLease`、`CameraCapture_StreamGetFrame`、`memset` |
| `App_UvcBridgeUsbProcess` :268 | USB Offer/ACK消费、UVC借用和Return | CM7/Application/Tasks/usb_task.c:118::UsbTask_Process | `App_MessagesIsOwner`、`App_MessagesReceive`、`App_MessagesSend`、`Bridge_SameLease`、`Bridge_UsbReturn`、`USB_UVC_CancelFrame`、`USB_UVC_GetStreamStatus`、`USB_UVC_SubmitFrame`、`USB_UVC_SubmitFrameView`、`memset` |
| `App_UvcBridgeUsbIsIdle` :419 | 只读状态/owner检查 | CM7/Application/app_usb.c:124::App_USBFinishRound | `App_MessagesIsOwner` |
| `Bridge_SameLease` :425 | epoch/lease/command匹配 | CM7/Application/app_uvc_bridge.c:137::App_UvcBridgeCameraProcess<br>CM7/Application/app_uvc_bridge.c:268::App_UvcBridgeUsbProcess | 无 |
| `Bridge_CameraFlush` :432 | 重试原ACK/Offer入队 | CM7/Application/app_uvc_bridge.c:137::App_UvcBridgeCameraProcess | `App_MessagesSend` |
| `Bridge_CameraRelease` :456 | 实际释放后生成ACK或终结SD-only | CM7/Application/app_uvc_bridge.c:137::App_UvcBridgeCameraProcess | `CameraCapture_StreamReleaseFrame` |
| `Bridge_UsbReturn` :484 | 提交原Lease的Return并重试 | CM7/Application/app_uvc_bridge.c:268::App_UvcBridgeUsbProcess | `App_MessagesSend` |
| `App_UvcBridgePrepareOutput` :500 | PLATFORM阶段验证/配置USB冷启动模式 | 无直接调用；须核对回调/导出/外部API | `App_FrameDispatcherValidateOutput`、`App_UvcBridgeConfigure`、`USB_Mode_SelectBootMode`、`USB_UVC_Configure`、`USB_UVC_ValidateConfig`、`VideoConfig_GetDefault` |

### `CM7/Application/app_uvc_bridge.h`

头依赖：`app_messages.h`。

公开接口：`App_UvcBridgeConfigure`、`App_UvcBridgeCameraBegin`、`App_UvcBridgeCameraProcess`、`App_UvcBridgeUsbProcess`、`App_UvcBridgeUsbIsIdle`、`App_UvcBridgeCameraIsIdle`。

### `CM7/Application/Tasks/camera_task.c`

头依赖：`camera_task.h`、`app_status.h`、`app_events.h`、`app_messages.h`、`app_camera.h`、`app_uvc_bridge.h`、`platform_time.h`、`platform_sync.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `CameraTask_Create` :47 | 静态创建并绑定owner | CM7/Application/app_tasks.c:18::App_TasksCreate | `App_EventsAttach`、`Platform_SyncGetException`、`xTaskCreateStatic`、`xTaskGetSchedulerState` |
| `CameraTask_Run` :71 | 任务等待与工作轮次 | 无直接调用；须核对回调/导出/外部API | `App_CameraSetDiagnosticObserver`、`App_EventsWait`、`CameraTask_Process`、`CameraTask_PublishStatus`、`Platform_TimeGetMs`、`configASSERT`、`uxTaskGetStackHighWaterMark` |
| `CameraTask_Process` :112 | 推进所属模块工作/错误 | CM7/Application/Tasks/camera_task.c:71::CameraTask_Run | `App_CameraBeginRound`、`App_CameraCanAcceptCommand`、`App_CameraCanOfferFrame`、`App_CameraFinishRound`、`App_CameraHandleCommand`、`App_CameraProcess`、`App_MessagesIsOwner`、`App_MessagesReceive`、`App_UvcBridgeCameraProcess`、`CameraTask_Reply` |
| `CameraTask_Reply` :141 | ACK/FAULT独立交付 | CM7/Application/Tasks/camera_task.c:112::CameraTask_Process | `App_CameraConfirmAckEnqueued`、`App_CameraConfirmFaultEnqueued`、`App_CameraGetPendingAck`、`App_CameraGetPendingFault`、`App_MessagesSend` |
| `CameraTask_PublishStatus` :157 | 状态缓存发布 | CM7/Application/Tasks/camera_task.c:71::CameraTask_Run | `App_CameraGetStatus`、`App_CameraSampleStatus`、`App_StatusPublishCamera` |
| `CameraTask_Observe` :180 | 诊断投影 | 无直接调用；须核对回调/导出/外部API | 无 |

### `CM7/Application/Tasks/camera_task.h`

头依赖：`sys_result.h`。

公开接口：`CameraTask_Create`。

### `CM7/Application/Tasks/control_task.c`

头依赖：`control_task.h`、`app_status.h`、`app_tasks.h`、`app_events.h`、`app_messages.h`、`video_config.h`、`platform_time.h`、`platform_sync.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `ControlTask_Create` :80 | 静态创建并绑定owner | CM7/Application/app_tasks.c:18::App_TasksCreate | `App_EventsAttach`、`Platform_SyncGetException`、`xTaskCreateStatic`、`xTaskGetSchedulerState` |
| `ControlTask_Run` :104 | 任务等待与工作轮次 | 无直接调用；须核对回调/导出/外部API | `App_EventsWait`、`App_TasksReleaseBoot`、`ControlTask_Process`、`ControlTask_PublishStatus`、`Platform_TimeGetMs`、`configASSERT`、`uxTaskGetStackHighWaterMark` |
| `ControlTask_Process` :147 | 旧串行Camera/USB编排与超时/错误 | CM7/Application/Tasks/control_task.c:104::ControlTask_Run | `App_MessagesIsOwner`、`App_MessagesReceive`、`App_MessagesSend`、`ControlTask_Command`、`ControlTask_RecordFault`、`Platform_TimeGetMs`、`VideoConfig_GetDefault` |
| `ControlTask_Command` :247 | 冻结控制命令身份和期限 | CM7/Application/Tasks/control_task.c:147::ControlTask_Process | `ControlTask_RecordFault`、`Platform_TimeGetMs` |
| `ControlTask_Request` :268 | 业务请求入队或明确拒绝 | CM7/Application/Tasks/control_task.c:285::ControlTask_RequestStop<br>CM7/Application/Tasks/control_task.c:291::ControlTask_RequestStart | `App_MessagesIsOwner`、`App_MessagesSend` |
| `ControlTask_RequestStop` :285 | 请求/执行停止，详见模块调用链 | 无直接调用；须核对回调/导出/外部API | `ControlTask_Request` |
| `ControlTask_RequestStart` :291 | 请求/执行启动，详见模块调用链 | 无直接调用；须核对回调/导出/外部API | `ControlTask_Request` |
| `ControlTask_PublishStatus` :297 | 状态缓存发布 | CM7/Application/Tasks/control_task.c:104::ControlTask_Run | `App_StatusPublishControl` |
| `ControlTask_RecordFault` :308 | 记录准备/故障事实 | CM7/Application/Tasks/control_task.c:147::ControlTask_Process<br>CM7/Application/Tasks/control_task.c:247::ControlTask_Command | 无 |

### `CM7/Application/Tasks/control_task.h`

头依赖：`sys_result.h`。

公开接口：`ControlTask_Create`、`ControlTask_RequestStop`、`ControlTask_RequestStart`。

### `CM7/Application/Tasks/ipc_task.c`

头依赖：`ipc_task.h`、`app_status.h`、`app_events.h`、`ipc_service.h`、`ipc_config.h`、`platform_time.h`、`platform_sync.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `IpcTask_Create` :35 | 静态创建并绑定owner | CM7/Application/app_tasks.c:18::App_TasksCreate | `App_EventsAttach`、`IPC_Service_SetRuntimeOwner`、`Platform_SyncGetException`、`xTaskCreateStatic`、`xTaskGetSchedulerState` |
| `IpcTask_Run` :63 | 任务等待与工作轮次 | 无直接调用；须核对回调/导出/外部API | `App_EventsWait`、`App_StatusGetSnapshotAt`、`IPC_Service_RuntimeProcess`、`IPC_Service_RuntimeStart`、`IPC_Service_SetApplication`、`Platform_TimeGetMs`、`configASSERT`、`pdMS_TO_TICKS`、`uxTaskGetStackHighWaterMark`、`vTaskDelay` |
| `IpcTask_IsOwner` :119 | 只读状态/owner检查 | 无直接调用；须核对回调/导出/外部API | `App_EventsIsOwner` |

### `CM7/Application/Tasks/ipc_task.h`

头依赖：`sys_result.h`。

公开接口：`IpcTask_Create`。

### `CM7/Application/Tasks/storage_task.c`

头依赖：`storage_task.h`、`app_storage.h`、`app_events.h`、`video_config.h`、`platform_time.h`、`platform_sync.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `StorageTask_Create` :21 | 静态创建并绑定owner | CM7/Application/app_tasks.c:18::App_TasksCreate | `App_EventsAttach`、`Platform_SyncGetException`、`xTaskCreateStatic`、`xTaskGetSchedulerState` |
| `StorageTask_Run` :31 | 任务等待与工作轮次 | 无直接调用；须核对回调/导出/外部API | `App_EventsWait`、`App_StorageIsReady`、`App_StoragePrepare`、`App_StorageProcess`、`Platform_TimeGetMs`、`VideoConfig_GetStorage`、`uxTaskGetStackHighWaterMark` |

### `CM7/Application/Tasks/storage_task.h`

头依赖：`sys_result.h`。

公开接口：`StorageTask_Create`。

### `CM7/Application/Tasks/usb_task.c`

头依赖：`usb_task.h`、`app_status.h`、`app_events.h`、`app_messages.h`、`app_usb.h`、`app_uvc_bridge.h`、`usb_mode.h`、`usb_device.h`、`usb_uvc.h`、`platform_time.h`、`platform_sync.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `UsbTask_Create` :51 | 静态创建并绑定owner | CM7/Application/app_tasks.c:18::App_TasksCreate | `App_EventsAttach`、`Platform_SyncGetException`、`xTaskCreateStatic`、`xTaskGetSchedulerState` |
| `UsbTask_Run` :75 | 任务等待与工作轮次 | 无直接调用；须核对回调/导出/外部API | `App_EventsWait`、`App_USBSetDiagnosticObserver`、`Platform_TimeGetMs`、`UsbTask_Process`、`UsbTask_PublishStatus`、`configASSERT`、`uxTaskGetStackHighWaterMark` |
| `UsbTask_Process` :118 | 推进所属模块工作/错误 | CM7/Application/Tasks/usb_task.c:75::UsbTask_Run | `App_MessagesIsOwner`、`App_MessagesReceive`、`App_USBCanAcceptCommand`、`App_USBCanTransfer`、`App_USBFinishRound`、`App_USBHandleCommand`、`App_USBIsStopping`、`App_USBProcess`、`App_UvcBridgeUsbProcess`、`USB_Device_GetSofSequence`、`USB_Mode_GetMode`、`USB_UVC_CancelFrame`、`USB_UVC_Process`、`UsbTask_Reply` |
| `UsbTask_Reply` :169 | ACK/FAULT独立交付 | CM7/Application/Tasks/usb_task.c:118::UsbTask_Process | `App_MessagesSend`、`App_USBConfirmAckEnqueued`、`App_USBConfirmFaultEnqueued`、`App_USBGetPendingAck`、`App_USBGetPendingFault` |
| `UsbTask_PublishStatus` :185 | 状态缓存发布 | CM7/Application/Tasks/usb_task.c:75::UsbTask_Run | `App_StatusPublishUsb` |
| `UsbTask_Observe` :197 | 诊断投影 | 无直接调用；须核对回调/导出/外部API | 无 |

### `CM7/Application/Tasks/usb_task.h`

头依赖：`sys_result.h`。

公开接口：`UsbTask_Create`。

### `CM7/Application/video_config.c`

头依赖：`video_config.h`。

| 函数:行 | 功能 | 直接调用者 | 被调用者 |
|---|---|---|---|
| `VideoConfig_GetDefault` :38 | 读取状态/配置 | CM7/Application/app_camera.c:94::App_CameraGetConfig<br>CM7/Application/app_camera.c:199::App_CameraPrepare<br>CM7/Application/app_storage.c:573::Storage_Request<br>CM7/Application/app_storage.c:772::Storage_OpenNew<br>CM7/Application/app_uvc_bridge.c:500::App_UvcBridgePrepareOutput<br>CM7/Application/Tasks/control_task.c:147::ControlTask_Process | 无 |
| `VideoConfig_RequestUpdate` :46 | 业务请求入队或明确拒绝 | 无直接调用；须核对回调/导出/外部API | 无 |
| `VideoConfig_GetStorage` :76 | 读取状态/配置 | CM7/Application/Tasks/storage_task.c:31::StorageTask_Run | 无 |

### `CM7/Application/video_config.h`

头依赖：`video_config_types.h`、`app_storage.h`。

公开接口：`VideoConfig_GetDefault`、`VideoConfig_RequestUpdate`、`VideoConfig_GetStorage`。

覆盖：39 个 C/H，210 个 C 函数定义。源码宏/函数指针入口另见上表，不能把未找到直接调用者当成删除依据。

## 补充：全局对象、宏与间接依赖审计

39文件/210函数仅是当前扫描统计，不表示依赖闭包已经验证。下面补充所有Application源码的文件级静态/全局对象与宏定义，包含静态TCB/栈、队列存储、函数表和诊断对象。引用函数集合来自同文件函数体，不能把“出现符号”直接当成写入，也不能排除传入地址后由被调用函数写入；实际写入权限按前文owner表和下文说明判断。后续删除仍需全target有效编译、链接Map和运行回归。

写入责任补充：s_camera和资格core由Camera owner（或初始化装配阶段）写；s_usb由USB owner写；Bridge双端分别单写；s_slots/s_queue/session/offering/accepting在Camera与Storage端通过PRIMASK+DMB协议协作；Storage文件/AVI对象只由StorageTask写。事件绑定/任务handle/队列对象只在启动阶段创建；BOOT后pending/广播遵循AppEvents的临界区。状态快照按源owner发布。g_*调试变量有跨模块写入和观察，不能用它们决定是否已释放资源。

### 宏定义

以下保留实际定义位置和文本，条件编译的两种分支均列出；这不是证明当前Keil选择了哪个宏分支。APP_VIDEO_TEST_MODE、APP_VIDEO_MJPEG_MODE的最终值必须连同工程预定义和编译命令确认。布局断言宏不是业务状态。

| 文件:行 | 宏 | 定义（规范化空白） |
|---|---|---|
| `CM7/Application/app_camera.h:2` | `CM7_APP_CAMERA_H` | `(空定义/头保护)` |
| `CM7/Application/app_camera_lifecycle.h:2` | `CM7_APP_CAMERA_LIFECYCLE_H` | `(空定义/头保护)` |
| `CM7/Application/app_camera_lifecycle.h:9` | `APP_LIFECYCLE_VERSION` | `1U` |
| `CM7/Application/app_camera_lifecycle.h:10` | `APP_LIFECYCLE_REQUEST` | `1U` |
| `CM7/Application/app_camera_lifecycle.h:11` | `APP_LIFECYCLE_REPLY` | `2U` |
| `CM7/Application/app_camera_lifecycle.h:12` | `APP_LIFECYCLE_CAMERA` | `1U` |
| `CM7/Application/app_camera_lifecycle.h:13` | `APP_LIFECYCLE_USB` | `2U` |
| `CM7/Application/app_camera_lifecycle.h:14` | `APP_LIFECYCLE_STORAGE` | `3U` |
| `CM7/Application/app_camera_lifecycle.h:15` | `APP_CONSUMER_USB` | `1U` |
| `CM7/Application/app_camera_lifecycle.h:16` | `APP_CONSUMER_STORAGE` | `2U` |
| `CM7/Application/app_camera_lifecycle.h:19` | `APP_LIFECYCLE_SESSION_QUERY` | `1U` |
| `CM7/Application/app_camera_lifecycle.h:20` | `APP_LIFECYCLE_ACQUIRE` | `2U` |
| `CM7/Application/app_camera_lifecycle.h:21` | `APP_LIFECYCLE_RELEASE` | `3U` |
| `CM7/Application/app_camera_lifecycle.h:22` | `APP_LIFECYCLE_STOP_IF_IDLE` | `4U` |
| `CM7/Application/app_camera_lifecycle.h:23` | `APP_LIFECYCLE_RELEASE_REQUEST` | `5U` |
| `CM7/Application/app_camera_lifecycle.h:24` | `APP_LIFECYCLE_FAULT_NOTICE` | `6U` |
| `CM7/Application/app_camera_lifecycle.h:27` | `APP_LIFECYCLE_INPUT_CLOSED` | `(1UL << 0)` |
| `CM7/Application/app_camera_lifecycle.h:28` | `APP_LIFECYCLE_WORK_QUIESCENT` | `(1UL << 1)` |
| `CM7/Application/app_camera_lifecycle.h:29` | `APP_LIFECYCLE_FRAMES_DRAINED` | `(1UL << 2)` |
| `CM7/Application/app_camera_lifecycle.h:30` | `APP_LIFECYCLE_FILE_FINISHED` | `(1UL << 3)` |
| `CM7/Application/app_camera_lifecycle.h:31` | `APP_LIFECYCLE_FILE_CLOSED` | `(1UL << 4)` |
| `CM7/Application/app_camera_lifecycle.h:32` | `APP_LIFECYCLE_VOLUME_RELEASED` | `(1UL << 5)` |
| `CM7/Application/app_camera_lifecycle.h:33` | `APP_LIFECYCLE_QUALIFICATION_RELEASED` | `(1UL << 6)` |
| `CM7/Application/app_camera_lifecycle.h:34` | `APP_LIFECYCLE_HARDWARE_STOPPED` | `(1UL << 7)` |
| `CM7/Application/app_camera_lifecycle.h:35` | `APP_LIFECYCLE_ALL_FLAGS` | `0xFFUL` |
| `CM7/Application/app_camera_lifecycle.h:36` | `APP_LIFECYCLE_CONSUMER_FLAGS` | `0x3FUL` |
| `CM7/Application/app_camera_lifecycle.h:63` | `APP_LIFECYCLE_ASSERT` | `(name, expression) typedef char name[(expression) ? 1 : -1]` |
| `CM7/Application/app_camera_lifecycle.h:74` | `APP_LIFECYCLE_TX_EMPTY` | `0U` |
| `CM7/Application/app_camera_lifecycle.h:75` | `APP_LIFECYCLE_TX_READY` | `1U` |
| `CM7/Application/app_camera_lifecycle.h:76` | `APP_LIFECYCLE_TX_WAIT_REPLY` | `2U` |
| `CM7/Application/app_camera_lifecycle.h:77` | `APP_LIFECYCLE_TX_COMMITTED` | `3U` |
| `CM7/Application/app_camera_lifecycle.h:78` | `APP_LIFECYCLE_TX_COMPLETE` | `4U` |
| `CM7/Application/app_camera_lifecycle.h:79` | `APP_LIFECYCLE_TX_SENDING` | `5U` |
| `CM7/Application/app_camera_lifecycle_replay.h:2` | `CM7_APP_CAMERA_LIFECYCLE_REPLAY_H` | `(空定义/头保护)` |
| `CM7/Application/app_camera_lifecycle_replay.h:6` | `APP_LIFECYCLE_CANDIDATE_EMPTY` | `0U` |
| `CM7/Application/app_camera_lifecycle_replay.h:7` | `APP_LIFECYCLE_CANDIDATE_HELD` | `1U` |
| `CM7/Application/app_camera_lifecycle_replay.h:8` | `APP_LIFECYCLE_CANDIDATE_QUARANTINED` | `2U` |
| `CM7/Application/app_camera_lifecycle_replay.h:11` | `APP_LIFECYCLE_RX_CANDIDATE` | `1U` |
| `CM7/Application/app_camera_lifecycle_replay.h:12` | `APP_LIFECYCLE_RX_DUPLICATE_RUNNING` | `2U` |
| `CM7/Application/app_camera_lifecycle_replay.h:13` | `APP_LIFECYCLE_RX_REPLAY` | `3U` |
| `CM7/Application/app_camera_lifecycle_replay.h:14` | `APP_LIFECYCLE_RX_STALE` | `4U` |
| `CM7/Application/app_camera_lifecycle_replay.h:15` | `APP_LIFECYCLE_RX_DUPLICATE_CANDIDATE` | `5U` |
| `CM7/Application/app_camera_lifecycle_replay.h:16` | `APP_LIFECYCLE_RX_CONFLICT` | `6U` |
| `CM7/Application/app_camera_lifecycle_replay.h:17` | `APP_LIFECYCLE_RX_QUARANTINED` | `7U` |
| `CM7/Application/app_camera_lifecycle_replay.h:67` | `APP_REPLAY_ASSERT` | `(name, expression) typedef char name[(expression) ? 1 : -1]` |
| `CM7/Application/app_camera_qualification.h:2` | `CM7_APP_CAMERA_QUALIFICATION_H` | `(空定义/头保护)` |
| `CM7/Application/app_camera_qualification.h:6` | `APP_QUAL_INACTIVE` | `0U` |
| `CM7/Application/app_camera_qualification.h:7` | `APP_QUAL_ACQUIRING` | `1U` |
| `CM7/Application/app_camera_qualification.h:8` | `APP_QUAL_ACTIVE` | `2U` |
| `CM7/Application/app_camera_qualification.h:9` | `APP_QUAL_RELEASING` | `3U` |
| `CM7/Application/app_camera_qualification.h:10` | `APP_QUAL_NONE` | `0U` |
| `CM7/Application/app_camera_qualification.h:11` | `APP_QUAL_DECIDED` | `1U` |
| `CM7/Application/app_camera_qualification.h:12` | `APP_QUAL_BLOCKED` | `2U` |
| `CM7/Application/app_camera_qualification.h:13` | `APP_RESOURCE_UNSTARTED` | `0U` |
| `CM7/Application/app_camera_qualification.h:14` | `APP_RESOURCE_PREPARING` | `1U` |
| `CM7/Application/app_camera_qualification.h:15` | `APP_RESOURCE_RUNNING` | `2U` |
| `CM7/Application/app_camera_qualification.h:16` | `APP_RESOURCE_STOPPING` | `3U` |
| `CM7/Application/app_camera_qualification.h:17` | `APP_RESOURCE_STOPPED_CONFIRMED` | `4U` |
| `CM7/Application/app_camera_qualification.h:18` | `APP_RESOURCE_FAULT_DRAINING` | `5U` |
| `CM7/Application/app_camera_qualification.h:112` | `APP_QUAL_ASSERT` | `(name, expression) typedef char name[(expression) ? 1 : -1]` |
| `CM7/Application/app_camera_qualification_internal.h:2` | `CM7_APP_CAMERA_QUALIFICATION_INTERNAL_H` | `(空定义/头保护)` |
| `CM7/Application/app_events.h:2` | `CM7_APP_EVENTS_H` | `(空定义/头保护)` |
| `CM7/Application/app_events.h:9` | `APP_EVENTS_WAIT_FOREVER` | `UINT32_MAX` |
| `CM7/Application/app_frame_dispatcher.h:2` | `APP_FRAME_DISPATCHER_H` | `(空定义/头保护)` |
| `CM7/Application/app_main.h:2` | `CM7_APP_MAIN_H` | `(空定义/头保护)` |
| `CM7/Application/app_messages.c:7` | `APP_MESSAGE_QUEUE_LENGTH` | `4U` |
| `CM7/Application/app_messages.c:110` | `APP_LIFECYCLE_QUEUE_LENGTH` | `4U` |
| `CM7/Application/app_messages.h:2` | `CM7_APP_MESSAGES_H` | `(空定义/头保护)` |
| `CM7/Application/app_status.h:2` | `CM7_APP_STATUS_H` | `(空定义/头保护)` |
| `CM7/Application/app_storage.c:12` | `SLOT_FREE` | `0U` |
| `CM7/Application/app_storage.c:13` | `SLOT_RESERVING` | `1U` |
| `CM7/Application/app_storage.c:14` | `SLOT_RETURN_PENDING` | `4U` |
| `CM7/Application/app_storage.c:15` | `SLOT_RETURNING` | `5U` |
| `CM7/Application/app_storage.c:16` | `SLOT_QUEUED` | `2U` |
| `CM7/Application/app_storage.c:17` | `SLOT_WRITING` | `3U` |
| `CM7/Application/app_storage.c:18` | `STORAGE_COMMANDS` | `4U` |
| `CM7/Application/app_storage.h:2` | `APP_STORAGE_H` | `(空定义/头保护)` |
| `CM7/Application/app_storage.h:8` | `APP_STORAGE_SLOT_COUNT` | `1U` |
| `CM7/Application/app_storage.h:9` | `APP_STORAGE_FRAME_LIMIT` | `70000U` |
| `CM7/Application/app_tasks.h:2` | `CM7_APP_TASKS_H` | `(空定义/头保护)` |
| `CM7/Application/app_usb.h:2` | `CM7_APP_USB_H` | `(空定义/头保护)` |
| `CM7/Application/app_uvc_bridge.h:2` | `CM7_APP_UVC_BRIDGE_H` | `(空定义/头保护)` |
| `CM7/Application/Tasks/camera_task.c:11` | `CAMERA_TASK_STACK_WORDS` | `1536U` |
| `CM7/Application/Tasks/camera_task.c:12` | `CAMERA_TASK_PRIORITY` | `3U` |
| `CM7/Application/Tasks/camera_task.c:13` | `CAMERA_TASK_WAIT_MS` | `20U` |
| `CM7/Application/Tasks/camera_task.c:16` | `CAMERA_TASK_STACK_SAMPLE_MS` | `1000U` |
| `CM7/Application/Tasks/camera_task.h:2` | `CM7_CAMERA_TASK_H` | `(空定义/头保护)` |
| `CM7/Application/Tasks/control_task.c:11` | `CONTROL_TASK_STACK_WORDS` | `768U` |
| `CM7/Application/Tasks/control_task.c:12` | `CONTROL_TASK_PRIORITY` | `2U` |
| `CM7/Application/Tasks/control_task.c:13` | `CONTROL_COMMAND_TIMEOUT_MS` | `5000U` |
| `CM7/Application/Tasks/control_task.c:16` | `CONTROL_TASK_STACK_SAMPLE_MS` | `1000U` |
| `CM7/Application/Tasks/control_task.h:2` | `CM7_CONTROL_TASK_H` | `(空定义/头保护)` |
| `CM7/Application/Tasks/ipc_task.c:10` | `IPC_TASK_STACK_WORDS` | `512U` |
| `CM7/Application/Tasks/ipc_task.c:11` | `IPC_TASK_PRIORITY` | `1U` |
| `CM7/Application/Tasks/ipc_task.c:14` | `IPC_TASK_STACK_SAMPLE_MS` | `1000U` |
| `CM7/Application/Tasks/ipc_task.h:2` | `CM7_IPC_TASK_H` | `(空定义/头保护)` |
| `CM7/Application/Tasks/storage_task.c:9` | `STORAGE_STACK_WORDS` | `2048U` |
| `CM7/Application/Tasks/storage_task.c:10` | `STORAGE_PRIORITY` | `1U` |
| `CM7/Application/Tasks/storage_task.h:2` | `STORAGE_TASK_H` | `(空定义/头保护)` |
| `CM7/Application/Tasks/usb_task.c:15` | `USB_TASK_STACK_WORDS` | `2048U` |
| `CM7/Application/Tasks/usb_task.c:16` | `USB_TASK_PRIORITY` | `4U` |
| `CM7/Application/Tasks/usb_task.c:17` | `USB_TASK_WAIT_MS` | `20U` |
| `CM7/Application/Tasks/usb_task.c:20` | `USB_TASK_STACK_SAMPLE_MS` | `1000U` |
| `CM7/Application/Tasks/usb_task.h:2` | `CM7_USB_TASK_H` | `(空定义/头保护)` |
| `CM7/Application/video_config.c:4` | `APP_VIDEO_TEST_MODE` | `0` |
| `CM7/Application/video_config.c:12` | `APP_VIDEO_MJPEG_MODE` | `0` |
| `CM7/Application/video_config.h:2` | `CM7_VIDEO_CONFIG_H` | `(空定义/头保护)` |

### 文件级静态/全局对象

| 文件:行 | 对象（定义位置） | 声明/静态初始化摘要 | 引用函数（不是写入证明） |
|---|---|---|---|
| `CM7/Application/app_camera.c:50` | `s_camera` | `static AppCameraContext_t s_camera;` | `App_CameraConfigure`、`App_CameraGetConfig`、`App_CameraStop`、`App_CameraAdvanceStop`、`App_CameraBeginRound`、`App_CameraRun`、`App_CameraPrepare`、`App_CameraStart`、`App_CameraSetDiagnosticObserver`、`App_CameraCanAcceptCommand`、`App_CameraHandleCommand`、`App_CameraProcess`、`App_CameraCanOfferFrame`、`App_CameraFinishRound`、`App_CameraGetPendingAck`、`App_CameraConfirmAckEnqueued`、`App_CameraGetPendingFault`、`App_CameraConfirmFaultEnqueued`、`App_CameraSampleStatus`、`App_CameraGetStatus`、`App_CameraGetLastResult`、`App_CameraObserve`、`App_CameraStartResource`、`App_CameraResourceState`、`App_CameraQualificationStart`、`App_CameraStopIdentityMatches`、`App_CameraRequestStop`、`App_CameraReadStopResult`、`App_CameraConsumeStopResult`、`App_CameraQualificationAccess` |
| `CM7/Application/app_camera.c:619` | `s_qualification_ops` | `static const AppQualificationResourceOps_t s_qualification_ops = { App_CameraResourceState, App_CameraQualificationStart, App_CameraRequestStop, App_CameraReadStopResult, App_CameraConsumeStopResult, NULL, NULL };` | `App_CameraQualificationAccess` |
| `CM7/Application/app_events.c:8` | `s_tasks` | `static TaskHandle_t s_tasks[RUNTIME_TARGET_COUNT];` | `App_EventsAttach`、`App_EventsRecordInitialization`、`App_EventsReleaseBoot`、`App_EventsWait`、`AppEvents_Post`、`AppEvents_PostFromIsr`、`App_EventsIsOwner` |
| `CM7/Application/app_events.c:11` | `s_pending` | `static uint32_t s_pending[RUNTIME_TARGET_COUNT];` | `App_EventsReleaseBoot`、`AppEvents_PreparePost` |
| `CM7/Application/app_events.c:14` | `s_initialized` | `static bool s_initialized;` | `App_EventsInit`、`App_EventsAttach`、`App_EventsRecordInitialization`、`AppEvents_PreparePost` |
| `CM7/Application/app_events.c:17` | `s_prepared` | `static bool s_prepared;` | `App_EventsRecordInitialization`、`App_EventsReleaseBoot` |
| `CM7/Application/app_events.c:20` | `s_failed` | `static bool s_failed;` | `App_EventsAttach`、`App_EventsRecordInitialization`、`App_EventsReleaseBoot`、`AppEvents_PreparePost` |
| `CM7/Application/app_events.c:23` | `s_boot_ready` | `static volatile bool s_boot_ready;` | `App_EventsReleaseBoot`、`AppEvents_PreparePost`、`AppEvents_Post` |
| `CM7/Application/app_events.c:41` | `s_event_port` | `static const RuntimeEventPort_t s_event_port = { .post = AppEvents_Post, .post_from_isr = AppEvents_PostFromIsr, .context = NULL };` | `App_EventsInit` |
| `CM7/Application/app_frame_dispatcher.c:15` | `s_epoch` | `static uint32_t s_epoch, s_next_frame;` | `App_FrameDispatcherBegin`、`App_FrameDispatcherPublish`、`App_FrameDispatcherCanAcquire` |
| `CM7/Application/app_frame_dispatcher.c:15` | `s_next_frame` | `static uint32_t s_epoch, s_next_frame;` | `App_FrameDispatcherBegin`、`App_FrameDispatcherPublish`、`App_FrameDispatcherCanAcquire` |
| `CM7/Application/app_frame_dispatcher.c:24` | `s_storage_service` | `static const FrameSinkService_t s_storage_service = { .wants = Dispatcher_StorageWants, .offer = Dispatcher_StorageOffer, .context = NULL };` | `App_FrameDispatcherPublish`、`App_FrameDispatcherNeedsStorage` |
| `CM7/Application/app_messages.c:10` | `s_message_queues` | `static StaticQueue_t s_message_queues[APP_CHANNEL_COUNT];` | `App_MessagesInit` |
| `CM7/Application/app_messages.c:13` | `s_message_storage` | `static uint8_t s_message_storage[APP_CHANNEL_COUNT][APP_MESSAGE_QUEUE_LENGTH * sizeof(AppMessage_t)] __attribute__((aligned(8)));` | `App_MessagesInit` |
| `CM7/Application/app_messages.c:17` | `s_message_handles` | `static QueueHandle_t s_message_handles[APP_CHANNEL_COUNT];` | `App_MessagesInit`、`App_MessagesSend`、`App_MessagesReceive` |
| `CM7/Application/app_messages.c:20` | `s_messages_attempted` | `static bool s_messages_attempted;` | `App_MessagesInit` |
| `CM7/Application/app_messages.c:23` | `s_message_receivers` | `static const RuntimeEventTarget_t s_message_receivers[APP_CHANNEL_COUNT] = { RUNTIME_TARGET_CAMERA, RUNTIME_TARGET_USB, RUNTIME_TARGET_CONTROL, RUNTIME_TARGET_CAMERA, RUNTIME_TARGET_USB };` | `App_MessagesSend`、`App_MessagesReceive` |
| `CM7/Application/app_messages.c:113` | `s_lifecycle_queues` | `static StaticQueue_t s_lifecycle_queues[APP_LIFECYCLE_CHANNEL_COUNT];` | `App_LifecycleMessagesInit` |
| `CM7/Application/app_messages.c:116` | `s_lifecycle_storage` | `static uint8_t s_lifecycle_storage[APP_LIFECYCLE_CHANNEL_COUNT] [APP_LIFECYCLE_QUEUE_LENGTH * sizeof(AppLifecycleMessage_t)] __attribute__((aligned(8)));` | `App_LifecycleMessagesInit` |
| `CM7/Application/app_messages.c:120` | `s_lifecycle_handles` | `static QueueHandle_t s_lifecycle_handles[APP_LIFECYCLE_CHANNEL_COUNT];` | `App_LifecycleMessagesInit`、`App_LifecycleMessagesSend`、`App_LifecycleMessagesReceive` |
| `CM7/Application/app_messages.c:123` | `s_lifecycle_attempted` | `static bool s_lifecycle_attempted;` | `App_LifecycleMessagesInit` |
| `CM7/Application/app_messages.c:126` | `s_lifecycle_ready` | `static bool s_lifecycle_ready;` | `App_LifecycleMessagesInit`、`App_LifecycleMessagesSend`、`App_LifecycleMessagesReceive` |
| `CM7/Application/app_status.c:8` | `s_status` | `static IpcApplicationStatus_t s_status;` | `App_StatusPublishCamera`、`App_StatusPublishUsb`、`App_StatusPublishControl`、`App_StatusGetSnapshotAt` |
| `CM7/Application/app_status.c:11` | `s_source_ticks` | `static uint32_t s_source_ticks[3];` | `App_StatusPublishCamera`、`App_StatusPublishUsb`、`App_StatusPublishControl`、`AppStatus_Age` |
| `CM7/Application/app_status.c:14` | `s_source_expired` | `static bool s_source_expired[3];` | `App_StatusPublishCamera`、`App_StatusPublishUsb`、`App_StatusPublishControl`、`AppStatus_Age` |
| `CM7/Application/app_status.c:17` | `s_snapshot_id` | `static uint32_t s_snapshot_id;` | `App_StatusGetSnapshotAt` |
| `CM7/Application/app_status.c:20` | `g_app_status_debug` | `volatile IpcApplicationStatus_t g_app_status_debug;` | `App_StatusGetSnapshotAt` |
| `CM7/Application/app_storage.c:39` | `s_slots` | `static volatile StorageSlot_t s_slots[APP_STORAGE_SLOT_COUNT];` | `App_StorageWantsFrame`、`App_StorageOfferFrame`、`App_StorageProcessReturns`、`App_StorageProcess`、`Storage_OnRecover`、`Storage_Fail`、`Storage_Release` |
| `CM7/Application/app_storage.c:48` | `s_queue` | `static volatile StorageQueueItem_t s_queue[APP_STORAGE_SLOT_COUNT];` | `App_StorageOfferFrame`、`App_StorageProcess`、`Storage_Fail` |
| `CM7/Application/app_storage.c:51` | `s_count` | `static volatile uint32_t s_head, s_count;` | `App_StorageOfferFrame`、`App_StorageProcess`、`Storage_OnRecover`、`Storage_Fail` |
| `CM7/Application/app_storage.c:51` | `s_head` | `static volatile uint32_t s_head, s_count;` | `App_StorageOfferFrame`、`App_StorageProcess`、`Storage_Fail` |
| `CM7/Application/app_storage.c:54` | `s_command_count` | `static volatile uint32_t s_commands[STORAGE_COMMANDS], s_command_head, s_command_count;` | `App_StorageProcess`、`Storage_Request` |
| `CM7/Application/app_storage.c:54` | `s_command_head` | `static volatile uint32_t s_commands[STORAGE_COMMANDS], s_command_head, s_command_count;` | `App_StorageProcess`、`Storage_Request` |
| `CM7/Application/app_storage.c:54` | `s_commands` | `static volatile uint32_t s_commands[STORAGE_COMMANDS], s_command_head, s_command_count;` | `App_StorageProcess`、`Storage_Request` |
| `CM7/Application/app_storage.c:57` | `s_config` | `static AppStorageConfig_t s_config;` | `App_StoragePrepare`、`App_StorageWantsFrame`、`App_StorageProcess`、`Storage_OnStart`、`Storage_OpenNew` |
| `CM7/Application/app_storage.c:60` | `s_state` | `static StorageState_t s_state;` | `App_StorageProcess`、`Storage_OnStart`、`Storage_OnStop`、`Storage_OnRecover`、`Storage_Transition` |
| `CM7/Application/app_storage.c:63` | `s_limit_reached` | `static bool s_limit_reached;` | `App_StorageProcess`、`Storage_OnStart` |
| `CM7/Application/app_storage.c:66` | `s_accepting` | `static volatile bool s_prepared, s_accepting, s_stop_pending, s_snapshot_taken;` | `App_StorageStop`、`App_StorageWantsFrame`、`App_StorageOfferFrame`、`App_StorageProcess`、`Storage_OnStart`、`Storage_OnStop`、`Storage_Finish`、`Storage_Fail` |
| `CM7/Application/app_storage.c:66` | `s_prepared` | `static volatile bool s_prepared, s_accepting, s_stop_pending, s_snapshot_taken;` | `App_StoragePrepare`、`App_StorageIsReady`、`App_StorageWantsFrame`、`App_StorageOfferFrame`、`App_StorageProcess`、`Storage_Request` |
| `CM7/Application/app_storage.c:66` | `s_snapshot_taken` | `static volatile bool s_prepared, s_accepting, s_stop_pending, s_snapshot_taken;` | `App_StorageWantsFrame`、`App_StorageOfferFrame`、`Storage_OnStart` |
| `CM7/Application/app_storage.c:66` | `s_stop_pending` | `static volatile bool s_prepared, s_accepting, s_stop_pending, s_snapshot_taken;` | `App_StorageProcess`、`Storage_OnStart`、`Storage_OnStop`、`Storage_Finish` |
| `CM7/Application/app_storage.c:69` | `s_last_offer` | `static volatile uint32_t s_session, s_offering, s_last_offer, s_next_file = 1U;` | `App_StorageWantsFrame`、`App_StorageOfferFrame` |
| `CM7/Application/app_storage.c:69` | `s_next_file` | `static volatile uint32_t s_session, s_offering, s_last_offer, s_next_file = 1U;` | `Storage_OpenNew` |
| `CM7/Application/app_storage.c:69` | `s_offering` | `static volatile uint32_t s_session, s_offering, s_last_offer, s_next_file = 1U;` | `App_StorageOfferFrame`、`App_StorageProcess`、`Storage_OnRecover` |
| `CM7/Application/app_storage.c:69` | `s_session` | `static volatile uint32_t s_session, s_offering, s_last_offer, s_next_file = 1U;` | `App_StorageOfferFrame`、`App_StorageProcess`、`Storage_OnStart` |
| `CM7/Application/app_storage.c:72` | `s_mounted` | `static volatile bool s_offer_seen, s_mounted;` | `App_StorageProcess`、`Storage_OnStart`、`Storage_OnStop`、`Storage_OnRecover`、`Storage_Finish` |
| `CM7/Application/app_storage.c:72` | `s_offer_seen` | `static volatile bool s_offer_seen, s_mounted;` | `App_StorageWantsFrame`、`App_StorageOfferFrame`、`Storage_OnStart` |
| `CM7/Application/app_storage.c:75` | `s_mode` | `static volatile AppStorageMode_t s_mode;` | `App_StorageWantsFrame`、`App_StorageOfferFrame`、`App_StorageProcess`、`Storage_OnStart`、`Storage_Finish` |
| `CM7/Application/app_storage.c:78` | `s_file` | `static StorageFile_t s_file;` | `App_StorageProcess`、`Storage_OnRecover`、`Storage_OpenNew`、`Storage_Write`、`Storage_Snapshot`、`Storage_CloseAvi`、`Storage_Finish` |
| `CM7/Application/app_storage.c:81` | `s_avi` | `static MjpegAvi_t s_avi;` | `App_StorageProcess`、`Storage_OpenNew`、`Storage_CloseAvi` |
| `CM7/Application/app_storage.c:84` | `s_free_budget` | `static uint32_t s_segment_tick, s_total_bytes, s_segments, s_free_budget;` | `App_StorageProcess`、`Storage_OnStart`、`Storage_OpenNew`、`Storage_CloseAvi` |
| `CM7/Application/app_storage.c:84` | `s_segment_tick` | `static uint32_t s_segment_tick, s_total_bytes, s_segments, s_free_budget;` | `App_StorageProcess` |
| `CM7/Application/app_storage.c:84` | `s_segments` | `static uint32_t s_segment_tick, s_total_bytes, s_segments, s_free_budget;` | `App_StorageProcess`、`Storage_OnStart`、`Storage_OpenNew` |
| `CM7/Application/app_storage.c:84` | `s_total_bytes` | `static uint32_t s_segment_tick, s_total_bytes, s_segments, s_free_budget;` | `App_StorageProcess`、`Storage_OnStart`、`Storage_CloseAvi` |
| `CM7/Application/app_storage.c:87` | `s_fps_frames` | `static uint32_t s_fps_tick, s_fps_frames, s_sync_tick;` | `App_StorageProcess` |
| `CM7/Application/app_storage.c:87` | `s_fps_tick` | `static uint32_t s_fps_tick, s_fps_frames, s_sync_tick;` | `App_StorageProcess` |
| `CM7/Application/app_storage.c:87` | `s_sync_tick` | `static uint32_t s_fps_tick, s_fps_frames, s_sync_tick;` | `App_StorageProcess`、`Storage_OnStart` |
| `CM7/Application/app_storage.c:90` | `s_input_tick` | `static volatile uint32_t s_input_tick;` | `App_StorageOfferFrame`、`App_StorageProcess`、`Storage_OnStart` |
| `CM7/Application/app_storage.c:93` | `s_status_snapshot` | `static AppStorageStatus_t s_status_snapshot;` | `App_StorageGetStatus`、`Storage_PublishStatus` |
| `CM7/Application/app_storage.c:96` | `s_readback` | `static uint8_t s_readback[512] __attribute__((aligned(32)));` | `Storage_Snapshot` |
| `CM7/Application/app_storage.c:99` | `s_business` | `static struct { ` | `App_StorageProcess`、`Storage_OpenNew`、`Storage_Snapshot` |
| `CM7/Application/app_storage.c:108` | `g_storage_status` | `volatile AppStorageStatus_t g_storage_status;` | `App_StoragePrepare`、`App_StorageOfferFrame`、`App_StorageProcessReturns`、`App_StorageProcess`、`Storage_Request`、`Storage_HandleEvent`、`Storage_OnStart`、`Storage_OnStop`、`Storage_OnRecover`、`Storage_OpenNew`、`Storage_Snapshot`、`Storage_CloseAvi`、`Storage_Finish`、`Storage_Fail`、`Storage_Release`、`Storage_Transition`、`Storage_PublishStatus` |
| `CM7/Application/app_tasks.c:12` | `g_app_tasks_debug` | `volatile AppTasksDebug_t g_app_tasks_debug;` | `App_TasksCreate`、`App_TasksRecordInitialization`、`App_TasksReleaseBoot` |
| `CM7/Application/app_tasks.c:15` | `s_create_attempted` | `static bool s_create_attempted;` | `App_TasksCreate` |
| `CM7/Application/app_usb.c:28` | `s_usb` | `static AppUsbContext_t s_usb;` | `App_USBSetDiagnosticObserver`、`App_USBCanAcceptCommand`、`App_USBHandleCommand`、`App_USBCanTransfer`、`App_USBIsStopping`、`App_USBProcess`、`App_USBFinishRound`、`App_USBGetPendingAck`、`App_USBConfirmAckEnqueued`、`App_USBGetPendingFault`、`App_USBConfirmFaultEnqueued`、`App_USBGetStatus`、`App_USBGetLastResult`、`App_USBObserve` |
| `CM7/Application/app_uvc_bridge.c:53` | `s_camera_bridge` | `static BridgeCamera_t s_camera_bridge;` | `App_UvcBridgeCameraBegin`、`App_UvcBridgeCameraIsIdle`、`App_UvcBridgeCameraProcess`、`Bridge_CameraFlush`、`Bridge_CameraRelease` |
| `CM7/Application/app_uvc_bridge.c:56` | `s_usb_bridge` | `static BridgeUsb_t s_usb_bridge;` | `App_UvcBridgeUsbProcess`、`App_UvcBridgeUsbIsIdle`、`Bridge_UsbReturn` |
| `CM7/Application/app_uvc_bridge.c:59` | `s_bridge_config` | `static CameraConfig_t s_bridge_config;` | `App_UvcBridgeConfigure`、`App_UvcBridgeUsbProcess` |
| `CM7/Application/app_uvc_bridge.c:62` | `s_bridge_frame_bytes` | `static uint32_t s_bridge_frame_bytes;` | `App_UvcBridgeConfigure`、`App_UvcBridgeUsbProcess` |
| `CM7/Application/app_uvc_bridge.c:65` | `s_bridge_configured` | `static bool s_bridge_configured;` | `App_UvcBridgeConfigure`、`App_UvcBridgeCameraBegin`、`App_UvcBridgeCameraProcess`、`App_UvcBridgeUsbProcess` |
| `CM7/Application/app_uvc_bridge.c:68` | `s_bridge_output_prepared` | `static bool s_bridge_output_prepared;` | `App_UvcBridgePrepareOutput` |
| `CM7/Application/app_uvc_bridge.c:71` | `g_bridge_debug` | `volatile AppBridgeDebug_t g_bridge_debug;` | `App_UvcBridgeCameraBegin`、`App_UvcBridgeCameraProcess`、`App_UvcBridgeUsbProcess`、`Bridge_CameraFlush`、`Bridge_CameraRelease`、`Bridge_UsbReturn` |
| `CM7/Application/Tasks/camera_task.c:19` | `s_camera_tcb` | `static StaticTask_t s_camera_tcb;` | `CameraTask_Create` |
| `CM7/Application/Tasks/camera_task.c:22` | `s_camera_stack` | `static StackType_t s_camera_stack[CAMERA_TASK_STACK_WORDS] __attribute__((aligned(8)));` | `CameraTask_Create` |
| `CM7/Application/Tasks/camera_task.c:25` | `s_camera_task` | `static TaskHandle_t s_camera_task;` | `CameraTask_Create` |
| `CM7/Application/Tasks/camera_task.c:28` | `g_camera_task_debug` | `volatile CameraTaskDebug_t g_camera_task_debug;` | `CameraTask_Run`、`CameraTask_Process`、`CameraTask_PublishStatus`、`CameraTask_Observe` |
| `CM7/Application/Tasks/control_task.c:30` | `s_control_tcb` | `static StaticTask_t s_control_tcb;` | `ControlTask_Create` |
| `CM7/Application/Tasks/control_task.c:33` | `s_control_stack` | `static StackType_t s_control_stack[CONTROL_TASK_STACK_WORDS] __attribute__((aligned(8)));` | `ControlTask_Create` |
| `CM7/Application/Tasks/control_task.c:36` | `s_control_task` | `static TaskHandle_t s_control_task;` | `ControlTask_Create` |
| `CM7/Application/Tasks/control_task.c:39` | `s_control_state` | `static ControlState_t s_control_state;` | `ControlTask_Process`、`ControlTask_Command` |
| `CM7/Application/Tasks/control_task.c:42` | `s_control_command` | `static AppMessage_t s_control_command;` | `ControlTask_Process`、`ControlTask_Command` |
| `CM7/Application/Tasks/control_task.c:45` | `s_control_command_sent` | `static bool s_control_command_sent;` | `ControlTask_Process`、`ControlTask_Command` |
| `CM7/Application/Tasks/control_task.c:48` | `s_control_sequence` | `static uint32_t s_control_sequence;` | `ControlTask_Command` |
| `CM7/Application/Tasks/control_task.c:51` | `s_control_command_tick` | `static uint32_t s_control_command_tick;` | `ControlTask_Process`、`ControlTask_Command` |
| `CM7/Application/Tasks/control_task.c:54` | `s_control_boot_started` | `static bool s_control_boot_started;` | `ControlTask_Process` |
| `CM7/Application/Tasks/control_task.c:57` | `g_control_task_debug` | `volatile ControlTaskDebug_t g_control_task_debug;` | `ControlTask_Run`、`ControlTask_Process`、`ControlTask_Command`、`ControlTask_PublishStatus` |
| `CM7/Application/Tasks/control_task.c:60` | `s_control_status_fault` | `static SysResult_t s_control_status_fault;` | `ControlTask_PublishStatus`、`ControlTask_RecordFault` |
| `CM7/Application/Tasks/ipc_task.c:17` | `s_ipc_tcb` | `static StaticTask_t s_ipc_tcb;` | `IpcTask_Create` |
| `CM7/Application/Tasks/ipc_task.c:20` | `s_ipc_stack` | `static StackType_t s_ipc_stack[IPC_TASK_STACK_WORDS] __attribute__((aligned(8)));` | `IpcTask_Create` |
| `CM7/Application/Tasks/ipc_task.c:23` | `s_ipc_task` | `static TaskHandle_t s_ipc_task;` | `IpcTask_Create` |
| `CM7/Application/Tasks/ipc_task.c:26` | `g_ipc_task_debug` | `volatile IpcTaskDebug_t g_ipc_task_debug;` | `IpcTask_Run` |
| `CM7/Application/Tasks/storage_task.c:13` | `s_tcb` | `static StaticTask_t s_tcb;` | `StorageTask_Create` |
| `CM7/Application/Tasks/storage_task.c:14` | `s_stack` | `static StackType_t s_stack[STORAGE_STACK_WORDS] __attribute__((aligned(8)));` | `StorageTask_Create` |
| `CM7/Application/Tasks/storage_task.c:15` | `s_task` | `static TaskHandle_t s_task;` | `StorageTask_Create` |
| `CM7/Application/Tasks/usb_task.c:23` | `s_usb_tcb` | `static StaticTask_t s_usb_tcb;` | `UsbTask_Create` |
| `CM7/Application/Tasks/usb_task.c:26` | `s_usb_stack` | `static StackType_t s_usb_stack[USB_TASK_STACK_WORDS] __attribute__((aligned(8)));` | `UsbTask_Create` |
| `CM7/Application/Tasks/usb_task.c:29` | `s_usb_task` | `static TaskHandle_t s_usb_task;` | `UsbTask_Create` |
| `CM7/Application/Tasks/usb_task.c:32` | `g_usb_task_debug` | `volatile UsbTaskDebug_t g_usb_task_debug;` | `UsbTask_Run`、`UsbTask_Process`、`UsbTask_PublishStatus`、`UsbTask_Observe` |
| `CM7/Application/video_config.c:20` | `s_default_config` | `static const VideoConfig_t s_default_config = { .camera = { .width = APP_VIDEO_MJPEG_MODE ? 320U : 160U, .height = APP_VIDEO_MJPEG_MODE ? 240U : 120U, .format = APP_VIDEO_MJPEG_MODE ? CAMERA_PIXEL_FORMAT_JPEG : CAMERA_PIXEL_FORMAT_YUV422 }, .output = APP_VIDEO…（完整初始化见源码）` | `VideoConfig_GetDefault` |
| `CM7/Application/video_config.c:57` | `s_storage_config` | `static const AppStorageConfig_t s_storage_config = { .record_fps = 25U, ` | `VideoConfig_GetStorage` |

### 函数指针、静态表与初始化偏序

| 类型/对象 | 创建/绑定位置 | 目标/间接执行 | 生命周期与风险 |
|---|---|---|---|
| RuntimeEventPort_t s_event_port | app_events.c静态初始化；App_EventsInit绑定 | post=AppEvents_Post，post_from_isr=AppEvents_PostFromIsr，context=NULL | 启动前一次绑定；task/ISR不同权限，不是业务结果通道 |
| USB设备事件callback | App_EventsInit→USB_Device_SetEventCallback | AppEvents_UsbNotify | 设备初始化或IRQ均可能触发；不能只搜索直接调用者 |
| Capture事件callback | App_EventsInit→CameraCapture_SetEventCallback | AppEvents_CameraNotify | Media持有回调；退役时必须解除旧指向并绑定新runtime |
| AppCameraDiagnosticObserver_t | CameraTask_Run→App_CameraSetDiagnosticObserver | CameraTask_Observe；由App_CameraObserve同步调用 | 不修改业务/发送消息/操作硬件；观察者不成为状态权威 |
| AppUsbDiagnosticObserver_t | UsbTask_Run→App_USBSetDiagnosticObserver | UsbTask_Observe；由App_USBObserve同步调用 | 同上，保留原诊断字段和时点，非ISR |
| AppLifecycleSendFn | App_LifecycleFlush、TxFlush、RxFlushReply参数 | 调用者注入send(context,message) | Queue满保留同身份；禁止send回调重入同Tx/Rx |
| AppQualificationResourceOps_t s_qualification_ops | app_camera.c静态表，经Access取得 | state/start/request_stop/read_stop_result/consume_stop_result；released/idle=NULL | Camera owner；request_stop不执行硬件；NULL不等于释放成功 |
| FrameSinkService_t s_storage_service | app_frame_dispatcher.c静态表 | wants=Dispatcher_StorageWants，offer=Dispatcher_StorageOffer，context=NULL | Camera同步调用，offer内部Retain独立SD Lease |
| DeviceLifecycleOps及Ov5640Ops（外部对象） | Device静态导出及App_CameraPrepare查找 | device lifecycle Start/Stop，camera ops configure | 不能因Application没有直接调用函数名就删除Device实现 |
| FreeRTOS task entry | 各Create→xTaskCreateStatic | 五个Task_Run | 静态TCB/栈不得二次实例化，Attach后由调度器进入 |
| IPC runtime owner | IpcTask_Create→IPC_Service_SetRuntimeOwner | IpcTask_IsOwner | Service保留函数指针；删除旧Task前迁移绑定 |

完整偏序：CM7底层/IPC EarlyInit、HSEM启动CM4、SDRAM/MPU→App_TasksCreate（Events回调和RuntimeEvent绑定→普通消息队列→五Task和Attach）→Framework_InitRuntime按PLATFORM/BUS/DEVICE/MEDIA/SERVICE/APPLICATION→记录初始化成功→调度器→Control释放BOOT→各任务开始业务。Framework同级导出按链接段顺序扫描，不能假设同级函数名决定先后。

PLATFORM的BridgePrepareOutput验证默认格式并调用USB_UVC_Configure、BridgeConfigure、USB_Mode_SelectBootMode；BUS的BSP准备、DEVICE的静态注册、MEDIA/SERVICE准备和APPLICATION的协议队列/模式检查是不同职责。实际低层导出顺序必须用目标ELF/Map确认，本轮未链接；这里只证明级别顺序和源码依赖。Camera真正START发生在BOOT后的Control命令，不是Initcall直接开启采集。

CM4入口使用独立Framework/Application；M4 OLED稳定延时属于用户已有硬件时序，本轮完整保留。HSEM是跨核启动/通知，不保护CM7共享帧引用；PRIMASK临界区不能替代跨核协议。

补充统计：宏定义条目107（含头保护/条件分支）；文件级数据对象索引100条。统计不替代预处理后有效配置、间接调用和链接依赖检查。

### 私有上下文和静态对象字段

以下从实际C内类型定义补充字段（不把这些类型扩散为V2共享上下文）。唯一写入者见前文owner表；指针/Observer字段指向外部对象，不因引用而转移其状态权威。

| 文件 | 私有类型 | 字段/函数表（源码声明） |
|---|---|---|
| `CM7/Application/app_camera.c` | `AppCameraContext_t` | `CameraConfig_t config; bool config_set; bool configured; bool initialized; Device_t *device; bool running; bool prepared; bool stopping; AppMessage_t ack; bool ack_pending; bool stop_command; bool fault_sent; SysResult_t fault; SysResult_t last_result; uint32_t command_id; uint32_t fps_tick; uint32_t fps_frames; uint32_t captured_frames; uint32_t dropped_frames; uint32_t measured_fps_milli; CameraConfig_t applied; bool applied_valid; AppCameraQualification_t qualifications; bool resource_preparing; bool resource_stop_confirmed; AppLifecycleIdentity_t stop_identity; AppQualificationStopResult_t stop_result; bool stop_result_valid; bool round_open; bool round_stop_attempted; SysResult_t round_stop_result; uint32_t round_media_sequence; AppCameraDiagnosticObserver_t observer;` |
| `CM7/Application/app_frame_dispatcher.c` | `FrameSinkService_t` | `bool (*wants)(void *context, uint32_t now); SysResult_t (*offer)(void *context, const CameraFrame_t *frame, const AppFrameMeta_t *meta); void *context;` |
| `CM7/Application/app_storage.c` | `StorageSlot_t` | `uint32_t state; uint32_t generation; uint32_t session; uint32_t size; uint32_t crc; AppFrameMeta_t meta; CameraFrame_t frame; CameraMjpegLease_t lease;` |
| `CM7/Application/app_storage.c` | `StorageQueueItem_t` | `uint32_t slot; uint32_t generation; uint32_t session;` |
| `CM7/Application/app_usb.c` | `AppUsbContext_t` | `bool started; bool stopping; bool stop_command; AppMessage_t ack; bool ack_pending; SysResult_t fault; bool fault_reported; SysResult_t last_result; uint32_t command_id; uint32_t fps_tick; uint32_t fps_frames; uint32_t completed_frames; uint32_t measured_fps_milli; AppUsbDiagnosticObserver_t observer;` |
| `CM7/Application/app_uvc_bridge.c` | `BridgeCamera_t` | `uint32_t epoch; uint32_t next_lease; bool held; bool local_only; bool request_pending; bool offer_pending; bool ack_pending; bool release_pending; uint32_t request_id; AppMessage_t offer; AppMessage_t ack; AppMessage_t last_ack;` |
| `CM7/Application/app_uvc_bridge.c` | `BridgeUsb_t` | `BridgeUsbState_t state; uint32_t next_request; bool request_sent; bool return_sent; bool cancel_sent; AppMessage_t lease;` |

匿名静态s_business保存saved_frames/file_number；常量产品配置与静态函数表字段已在相应表/来源登记。typedef类型本身不分配额外实例；static对象与其类型不可重复计RAM。
