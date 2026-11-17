# Phase4 Legacy/真实Backend最小适配门禁

所有内容DESIGN_PROPOSED，未修改生产路径，未执行补丁。生产审计来源为9179dde分支实际文件；不假设C2.2B另一分支已合入。只读补丁Phase4_Legacy_ReadOnly_Proposal.diff是零上下文、ASCII新增hunk，由实际文件生成，保留旧编码；尚未编译/审批，不可据此直接git apply或启用硬件。

## 1. 已确认的调用链

- camera_task.c::CameraTask_Process：BOOT/owner检查→App_CameraBeginRound→消息/ACK处理→App_CameraProcess→App_UvcBridgeCameraProcess(App_CameraCanOfferFrame())→App_CameraFinishRound→ACK/FAULT交付。
- app_camera.c::App_CameraPrepare：Ov5640Ops.configure→CameraCapture_Init；App_CameraStart：Device_Start→CameraCapture_StartStream（或Start）；部分启动失败回退App_CameraAdvanceStop。
- App_CameraRun→CameraCapture_Process；已有轮次Stop事实防止START回退后同轮再次进入错误重试。
- App_CameraFinishRound→App_CameraAdvanceStop→CameraCapture_Stop→CameraCapture_IsStopped→Device_Stop；只有Stop成功才检查Bridge Idle。不得改变这些先后关系或新建第二普通Stop入口。
- app_uvc_bridge.c::App_UvcBridgeCameraProcess最先App_StorageProcessReturns，再处理Bridge Flush/Release/请求；不能在新返回适配中再归还一次Storage。
- App_CameraRequestStop/ReadStopResult/ConsumeStopResult为private资格回调，只接受USB/Storage资格身份；s_qualification_ops的released、idle仍NULL。V2执行身份不能伪装成其中一个消费者。

## 2. 文件级最小差异及证明条件

| 文件 | 建议差异 | 限制/门禁 |
|---|---|---|
| CM7/Media/Inc/camera_capture.h、Src/camera_capture.c | 只读CameraCapture_QueryRawLayout，复用CalcSize/ValidateStreamConfig/已有容量、对齐、DMA word常量；草案补丁提供具体hunk | 无Start/Stop/Init副作用；不改DMA参数；不解决Raw M身份 |
| CM7/Media/Inc/camera_mjpeg_fifo.h、Src/camera_mjpeg_fifo.c | QueryMaxFrameBytes直接读已有MJPEG_MAX_FRAME_BYTES；QueryLoan按session/id复制frame epoch/范围/original_owned/sd_owned | Camera owner调用，PRIMASK保存/恢复与屏障保持；不Get/Retain/Release、不改refs。原始持有可能是SD-only内部用途，不能仅因usb_owned字段名就关联USB G |
| CameraCapture Raw内部元数据 | 完整Raw原生token只读快照仍需审查跨Start/slot复用代次 | 现有slot sequence可能重置/回绕，没有已证明的独立非复用source epoch。只加只读接口不足以证明Raw ABA；新增非回绕epoch或其他边界必须先审批，不能用G或Frame地址造M |
| Common/Inc新中立camera_execution_identity.h、Application/app_camera.h/.c | 提议CameraExecutionIdentity_t={session,request,module,operation}及Scoped Request/Read/Consume接口 | 目前无这些接口，不能把V2类型向下硬塞Legacy或把module映射USB consumer。必须统一现有单Stop结果记录的domain-tagged身份，保留资格身份和V2执行身份；不新增第二Stop状态机 |
| app_camera.c::Request/Read/ConsumeStopResult及FinishRound | 单一记录可接Qualification域或Execution域，冲突BUSY；R2唯一AdvanceStop推进与首次失败冻结不变 | 只能登记/读取/消费；真实Stop仍FinishRound；未准备时不得初始化，完成结果hardware_stopped不能凭空置1。现阶段只有接口方案，不能称为已验证可应用的停止补丁 |
| app_uvc_bridge.c/.h | G/H/D受保护发布、取得真实M后BindBridge、Camera端匹配B/ACK消费证据 | Get→分配B顺序不变；不得先Get再额外Get；旧52B协议不变，元数据/旧消息拒绝及T退休必须先审 |
| app_storage.c/.h | 指定S/session/generation与G/M对应、Camera实际ReleaseLease后的归还证明公开口 | 不改FatFS、写入/Finish/Close策略；Storage IDLE/RETURN_PENDING/文件关闭均不等于归还 |
| CameraTask/旧BOOT与Keil | 后续选择唯一活动Camera owner入口和V2运输适配 | 本轮NOT_RUN/未修改；不得新建第二CameraTask或让新旧控制同时驱动Capture |

Scoped控制接口草案（未实施）：App_CameraRequestExecutionStop(const CameraExecutionIdentity_t*)；App_CameraReadExecutionStopResult(id, result, hardware_stopped)；App_CameraConsumeExecutionStopResult(id)。新增中立身份只能是本地值，不改AppMessage/Lifecycle消息。现有Qualification接口必须通过自己的tag保留generation语义，禁止把两个request_id域混同。Activate的Prepare/配置状态同样需要正式owner适配：V2 Configure不写sensor；真正Activate前只对相同冻结配置准备一次，不能盲重调原App_CameraConfigure或越过私有状态。

## 3. 证据不能由只读快照自动产生

Mjpeg session/id来源s_loan_session、loans[].id，完整token必须与原G/H绑定；同session查不到已知token也不能对任意未知token伪造Release成功。旧session应拒绝，不能因“查无”就释放新会话。QueryLoan的original_owned对应原生原始用途，USB或SD-only归属依赖已批准D/G关联元数据。Raw epoch未解决前Native Raw Source Port保持不可用/BUSY。

Media、Bridge、Slot快照必须在Camera owner稳定关闭窗口核验，不能裸读USB端State拼接。ACK入队/USB消费/T退休各需原队列证据。QueryExitEvidence=0只有真实R身份及MR可信Backend证明皆有效才有意义；此只读补丁本身不接Qualification released/idle，不关闭两个NOT_PROVEN。

## 4. 保护性Stop与真实恢复

VCamera Native process/finish端口必须复用现有BeginRound、App_CameraProcess、Bridge、FinishRound，保留保护性Stop及失败序号。Startup失败的清理若已尝试，不能再同轮无条件调用BSP。后台恢复后原IO不改写。Device ERROR自动复位/重新Init没有本轮授权；真实ERROR_PROCESS若无现有安全恢复能力，应返回不支持或保留BLOCKED，不以Mock成功替代。

运行中独立Media FAULT后的LC自动ERROR迁移需要Phase3公共Fault通知契约，不能直接写LCContext.state或改写已冻结Activate回复；本轮记录当前media_result、历史故障及物理停止事实，不宣称该自动迁移已实现。

## 5. 执行前审批

上述每组差异需独立审核、O0/O2真实旧模块联合测试、ABI/PRIMASK/Map/栈检查及实板故障注入。USB Core/UVC、Media refs、SDMMC/FatFS、原任务参数和M4延时仍禁止顺手修改。直到完成身份/关闭/ACK/DMA证明，真实释放回调仍不得补SYS_OK。
