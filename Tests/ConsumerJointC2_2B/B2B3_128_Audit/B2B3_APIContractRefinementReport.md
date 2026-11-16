# API Contract Refinement Report

基线c3350b01698867939be456d1ef5e94aebc2985b8；分支codex/stage-C2-2B-B2B6-v1-compat。开始时仅原未跟踪output/。本地既有提交完整保留，首次远端查询超时，不把缓存当远端确认；结束另行推送核验。

## 交付与完成检查

| 检查 | 结论 |
|---|---|
| ACQUIRE与RELEASE身份 | 已追踪创建、传播、验证、退休；明确request_id不同；当前related_request_id未显式关联ACQUIRE |
| Frame Lease | 已追踪Bridge与Media/Storage两类凭据，不等同生命周期编号 |
| 全部身份完整关联 | NOT_PROVEN：grant到具体Bridge/Media loan/slot缺显式映射或完整时序不变量 |
| ACK入队与消费 | 已分别定位CameraFlush入队和USB匹配分支消费；业务ACK/Return ACK/生命周期回复分开 |
| 跨任务所有权 | Camera/USB两侧独立，Storage共享槽分阶段写，Media Task与ISR块回调边界明确 |
| 核验期间重新开放 | NOT_PROVEN：Camera端通用enabled不含分消费者grant门；需穷尽迟到帧队列与生命周期交错 |
| B2人工决策 | ID23=A有界BUSY，ID31=B拆分原成功重启目标；均非生产授权 |
| Replay | 不新增配额，原编号/退休/接替规则不动 |
| 生产API可实施性 | 尚不能确认；先补身份适配、稳定关闭与动态证据设计，不自行实施 |

## 源码索引

- CM7/Application/app_camera_consumer.c：Begin/End/Process/Consumer_Stage；生成generation、预留4个编号、匹配消费回复、清grant与STOP_IF_IDLE。
- CM7/Application/app_camera_qualification.c：Qual_Window/Qual_Execute/Qual_OtherWork/Process；grant键、RELEASE窗口、released回调及真实停止门。
- CM7/Application/app_camera_lifecycle.c与app_camera_lifecycle_replay.c：Validate/MatchesReply/AllocateIds/Tx/Rx/Promote；完整事务与退休边界。
- CM7/Application/app_uvc_bridge.c：CameraBegin/CameraProcess/UsbProcess、Bridge_SameLease/CameraRelease/CameraFlush/UsbReturn；帧请求、归还和ACK消费。
- CM7/Application/app_storage.c：OfferFrame/Process/ProcessReturns/Storage_LifecycleFinish；真实slot和文件业务分离。
- CM7/Media/Src/camera_mjpeg_fifo.c：Start/GetFrame/RetainFrame/ReleaseFrame/ReleaseLease/Mjpeg_ReleaseLoan；两owner及块引用。
- CM7/Application/app_frame_dispatcher.c：Begin/Publish；Camera epoch和frame id传播到Storage meta，不承载grant身份。
- CM7/Application/Tasks/camera_task.c、usb_task.c、storage_task.c：所属任务队列与每轮顺序。
- CM7/Application/app_messages.c：帧、业务、生命周期静态队列及发送/接收owner校验。
- CM7/Application/app_events.c：SetEventCallback适配、Wait和RuntimeEvent任务通知；CM7/Platform/usb_device.c与CM7/Service/usb_uvc_class.c提供USB事件/完成链。

五份文档仅静态源码分析与设计，不运行测试/构建，不变更ABI、任务设置、M4 //等OLED稳定 或 HAL_Delay(500);，不修改main/不合并PR/不进入C2.2C。Git提交仅新增这五份审核文档。

## 后续最小设计范围

保持现有released回调的grant语义，同时明确Qualification的RELEASE窗口责任；若要双身份接口先提Ops适配审核。首先建立基于同任务ACK消费→RELEASE发送的现有顺序证明，再核验Camera侧帧消息迟到/新Offer门和grant到loan关联。可用原账本时不新增状态，无法证明时NOT_PROVEN，不通过跨任务快照或Debug拼接成功。真实接口设计经人工复审后才能单独授权生产实施。
