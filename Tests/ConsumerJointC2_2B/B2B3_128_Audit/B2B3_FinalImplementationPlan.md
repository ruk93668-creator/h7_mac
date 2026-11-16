# Final Implementation Plan — Design Only

基线b6541af，生产14bbdcc。以下是需要单独授权的最小生产变更计划，不表示本轮实施。本轮不编译、不测试、不改ABI或任务参数，M4延时不动。统一契约以FinalContractClosure和FinalLifecycleModel为准。

## 1. 文件、函数、字段和所有者

| 文件 | 具体未来变更 | 字段/写者 |
|---|---|---|
| CM7/Application/app_uvc_bridge.c/.h | UsbProcess/CameraProcess/CameraFlush/CameraRelease/UsbReturn接入D、T、pending_G、真实M；USB回复重试与SD服务分段不互相堵塞 | U由USB写；live C/T/pending_G/F/last_ack_G由Camera写 |
| CM7/Application/app_camera.c/.h | 私有权威G查询/准入核验和按consumer身份路由证据；不新增固定CameraUsb/Storage专用核心API | 不复制running/qualification/refcount |
| CM7/Application/app_camera_qualification.c/.h | 增加不重入、Camera owner的完整grant只读访问；原RELEASE phase作门；保持Rx/DECIDED/BLOCKED和R/G职责 | 原Qualifier entry仍唯一真源，不改Replay |
| CM7/Application/app_frame_dispatcher.c/.h | Publish到Storage的同步适配携const G，逐目标独立准入，不默认USB G等于SD G | 原epoch/frame id保持；无引用计数 |
| CM7/Application/app_storage.c/.h | Storage_Request/Stop同锁stop_epoch；命令标签/pending标签；OnStart/StartGranted二次epoch核对；Offer的G标签及ProcessReturns核验；原旧入口迁移为受控适配，不留绕门 | epoch仅Storage_Request路径受锁写；command标签入队不可变；pending由StorageTask；slot G由Camera Offer写 |
| CM7/Media/Inc/camera_mjpeg_fifo.h及Src/camera_mjpeg_fifo.c | ReadLoanIdentities只读真实loan，原Get/Retain/Release引用算法保持 | 不新增owner位/refcount |
| CM7/Media/Inc/camera_capture.h及Src/camera_capture.c | 类型化只读源快照；CameraStream安全Start/session及成功GetFrame/acquisition身份元数据 | Camera取得/安全初始化路径写，原ISR不写grant身份 |
| CM7/Application/Tasks/camera_task.c、usb_task.c、storage_task.c | 仅必要适配调用；保持原消息接收数量、BOOT、优先级/栈/等待、Media→Bridge→FinishRound及ACK独立交付 | 不跨Task直接Release或文件收尾 |

Snapshot专用采集不参加Stream交接，但原CameraCapture模式互斥与停止事实必须回归，不能被新Stream身份初始化覆盖。UVC/Core/SDMMC/FatFS/BSP无需为此顺手重写；原完成/Abort条件作为资源证明依赖。

## 2. 内部签名意图

GrantKey={session, acquire_request_id, consumer, generation}。拟议App_CameraReadActiveGrant、App_CameraCheckAdmission(G)、BridgePublishRequestGrant(G,H)、BridgeTryRetire(H)、BridgeReadTerminal(T)、DispatcherOfferToStorage(frame,meta,G)等仅为设计接口意图；最终名字可按代码风格调整，不得改语义。

released(context, grant)可保留现有Ops签名：Qualifier负责当前R窗口、原G以及先关闭；资源适配只读G交接和真实源事实，不能再次Qual_Enter重入。idle将来查询全资源安全状态，不以target消费者退出代替全局条件。当前两个回调为NULL，绑定是未来生产实施步骤，不在本文发生。

CameraCapture_ReadSourceOwnership(out)为类型化资源视图：JPEG委托只读FIFO，非JPEG读IN_USE/实际DMA索引/身份。非JPEGM为{kind, stream_session, acquisition_id, buffer_index}；current_original语义是源持有，不默认业务USB。输出不得别名覆盖私有源记录、不得跨资源操作复用旧快照；空参数/不合规上下文/内部矛盾失败，不能伪造empty-success。所有身份均不进入52B/56B消息，不修改Frame Lease消息三元组。

## 3. 精确RAM预算方法与候选字段算术

以下每字为uint32、原始预算，必须后续ARMCC5 sizeof/offsetof和Map验证；不是本轮实测。新增Stream标识是私有观察元数据，不是第二套Loan。

| 项目 | 原始字节 |
|---|---:|
| U: G16/H4/phase4/committed4/consumed_H4/retired_H4 | 36 |
| live C: G16/H4/phase4/B12/类型化M16/flags4 | 56 |
| T: G16/H4/B12/M16/result4/kind4/phase4 | 60 |
| pending_G附原request_id/request_pending | 16 |
| F | 4 |
| last_ack_G | 16 |
| 每StorageSlot G | 16*S |
| Storage stop_epoch4 + 每命令start_epoch4*K + pending_epoch4 | 8+4*K |
| 非JPEG stream_session/next_acquisition_id | 8 |
| 静态类型化快照scratch: kind4 + max(JPEG104,Stream视图) | 候选108 |

固定不含slot/命令标签部分为304B；合计312+4*K+16*S。当前K=STORAGE_COMMANDS=4、S=1，候选344B（含108B scratch）。手工加和必须与字段定义核对，不沿用上一版220B；若实际需要更多状态/对齐则报告实际差异，不能隐藏进其他全局。

静态consumer-provider路由建议const表，当前两项各{id32,fnptr32}原始16B RO；若工具链把重定位对象放RW，应在Map实际计入。不是未来Network注册实现。结构若含uint64/新union增加对齐需单独列出，不把4字节估算当ARMCC保证。

既有Camera/USB/Storage栈配置不改；大快照使用Camera单owner静态scratch，不允许嵌套重入。临时G/U/T副本及函数深度会增加栈，要求HTM/调用图/Map及实板水位核验，含间接调用的不完全栈上界必须保留风险。不得因为预算大而删身份或降低断言。

## 4. 同步和任务顺序

跨Task记录完整读写均原PRIMASK保存/禁中断/屏障/发布/屏障/恢复；队列与USB/Media/FS调用锁外，所有重试非阻塞、每轮有限。U/T独立单写，retired_H不在下一D重置。T PREPARED可先保存真实结果，终结入队后再ENQUEUED/F；USB先消费的抢占只导致等待，不使C被复用而丢证。

Camera每轮原资格Receive/Process在Media/Bridge之前：若R核验暂忙，Bridge排空，下一轮再核验；不强改成同轮同步等待。Bridge内部先有限处理USB控制/回收receipt，再处理Frame请求/实际资源与SD-only，ACK发送失败不能return跳过全部Storage服务。每个实际GetFrame/Retain前再查G ACTIVE；已经绑定继续归还。

Storage原顺序保留：出队一条命令→消费资格回复→处理该命令→审pending_start→帧/文件处理→Finish/缓存发布。epoch检查在这些点加入，不把已到达的GRANTED丢弃。START失败/取消后仍持grant时，只有真实quiet后End，不强清WAIT_REPLY。

## 5. 分阶段提交及依赖

| 阶段 | 变更内容 | 顺序/并行与可运行边界 |
|---|---|---|
| P0 | 人工复审四文档与精确错误/字段布局，冻结旧结果 | 所有生产工作之前；不是实施提交 |
| P1 | 私有身份类型、只读FIFO/Capture查询、Stream元数据与静态检查；测试fixture | 两资源适配可并行开发，不改变现有消息/停止路径；先验证无副作用 |
| P2 | Storage fence及标签/同步参数，Bridge U/T/pending/C/F机制 | 可分别开发，但须各自专项；不得只部分打开准入导致旧旁路可用 |
| P3 | Camera完整G访问、所有获取/Retain准入与关联迁移 | 依P1/P2；发布时同一受审组合提交所有入口，不能让无G路径回退到当前grant |
| P4 | 真正released/idle适配与资格撤销/停止闭环 | 必须等待前面组合证明；属于后续单独C2.2C生产授权范围，不在本阶段自动允许 |
| P5 | 全部回归、O0/O2、双核构建、实板 | 编码获授权后执行，记录原失败，不自动合并 |

不得通过每阶段第二套状态机或临时强清表实现“阶段可运行”。API/资源接口变化须相应编译登记；失败可保留前一提交，不能改写历史。P4前安全闭环回调仍不报告成功，不用测试桩冒充生产退出。

## 6. O0/O2与历史验收

对应FinalAcceptanceMatrix逐项真实运行，不用重播旧JSON作新执行。核心新负对照：覆盖未消费T、旧H错配新D、未提交取消假Camera ACK、旧generation变当前、epoch过期却accepting=true、非JPEG缓冲仍为DMA target就释放、JPEG另一owner仍在时释放块、混淆内部角色。

原C1/C1.1/C2.1.1/Qualification/R2/B1/B2/B3/Consumer/Joint/ID7/ID17/格式能力、USB异步Abort等回归保留全部断言与原FAIL；B1 37/39与Consumer42/44的既有分类不改。B2 ID23有界BUSY；ID31当前未退出时拒绝START，未来合法成功START故障复位目标仍单独验证，不造成功START。

双核ARMCC5完整Rebuild、AppMessage52B/Lifecycle56B/CameraFrame原布局、Map/RW/ZI/结构偏移、任务栈和RO路由预算逐项对比14bbdcc生产树；固件SHA与报告绑定。没有本轮编译或测试结果。

## 7. 实板建议及最终授权状态

V1分别JPEG/MJPEG/YUY2 UVC，CameraStream RGB565/Y8原支持路径；双路工作、停止一路另一继续、USB不消费ACK时Storage继续（受控任务/队列故障注入而非删除证据）、SD-only、实际DMA与Abort/Close失败、CameraStop失败、跨会话旧完成回调与连续运行栈水位。软件账本和真实硬件时序分别验收；未执行均RUNTIME_NOT_PROVEN/NOT_RUN。

五项设计缺口已有唯一规则，实施准备规范完成但生产授权仍未取得。本次只新增四份文件，等待人工审核与C2.2C单独授权，不改main/不合并。
