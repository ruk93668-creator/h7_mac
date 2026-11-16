# Grant–Lease Association Design

基线14bbdcc034ec371f3b5a7f0faf542a05ba9fc5fc。只作设计，不修改生产/测试或ABI，不运行测试。所有新增字段/入口均为建议，非当前实现。两种方案都以原Media账本为唯一引用权威，不增加第二套引用计数。

## 真实路径与断点

1. app_camera_consumer.c Begin递增generation并预留4个编号；QUERY匹配消费后Stage ACQUIRE first+1。
2. app_camera_qualification.c Qual_Execute ACQUIRE成功存grant_session/grant_generation/grant_request；consumer收到匹配回复才granted=1。
3. app_usb.c Process资格成功后ModeStart；Storage_StartGranted在资格成功后挂载/accepting=true。业务上下文有grant但Frame消息没有。
4. app_uvc_bridge.c CameraProcess :220/:243 GetFrame，:226/:252 DispatcherPublish；Camera Offer由Bridge epoch/next_lease及请求command_id命名。
5. camera_mjpeg_fifo.c GetFrame生成Media loan.id并置usb_owned；RetainFrame生成SD lease{loan session,id}，StorageSlot记录lease与本地session/generation。当前frame中不含grant。
6. USB Return经Bridge_SameLease三元组到StreamReleaseFrame；SD ProcessReturns由Camera owner调用真实ReleaseLease，成功且slot generation匹配才FREE。
7. ConsumerEnd Stage RELEASE first+2；Qualifier受控RELEASE窗口关联原grant，并把released回调身份的request_id改成原ACQUIRE编号。生产released/idle NULL，真实核验尚未完成。

G=(session, ACQUIRE_request, consumer, generation)；R=(session, RELEASE_request, consumer, generation)；B=(stream_epoch, Bridge lease_id, frame_request command_id)；M=(Media loan.session,id)；S=(Storage local session,slot,generation)。这些身份不能互相替换；同一G可关联多帧，同一M也可分别被两个不同G持有。

## 方案A：保持消息ABI，限定在途交接的不可变关联描述

建议在现有业务/Bridge资源层补充小型、静态、只存身份的交接元数据；Camera核验统一按consumer身份路由到资源提供方，不按USB/Storage写死核心判断。当前仅两种适配，未来Network另审，不注册第三消费者。

### A1 发送端与准入

USB在现有唯一REQUEST链旁记录不可变描述D={frame_request command_id, G, publication phase}。G来自App_USB原消费者上下文，但Camera消费请求时还必须与自身Qualification的权威G及ACTIVE状态核对。不能仅信USB发布的G。D必须先在明确的短同步窗口发布，再允许把原52B FrameRequest入队；队列满保持同一D/编号重试，未提交取消可回收。Camera出队按command_id查询D，取一致快照并验证G；缺记录/冲突/旧G直接拒绝，禁止“找不到就使用当前grant”。

当前Bridge每端仅一条在途REQUEST，可以用一个描述槽，不需要无限映射表；槽不得在Camera尚可能消费旧请求或USB尚未消费终结Offer/Return ACK时重用。失败/取消各分支必须明确两端何时退休。重复迟到旧request若不匹配现存D，只拒绝且不取Frame。这里有新的同步/退休不变量，未实施前NOT_PROVEN。

Storage通过现有同步Camera→Dispatcher→Offer调用链传递G给资源适配（拟议内部签名，不改Frame描述ABI）；在RESERVING线性化时将G附在原StorageSlot，再Retain得到M。若不能修改内部调用签名，也可由Camera授权适配在该同步调用栈提供显式const G参数；不允许Storage临时读取跨任务camera上下文推导G。

### A2 取得帧后的绑定

Camera在准入后绑定G→B→M，元数据附于现有Bridge当前交接，直到真实Return后并消费对应终结消息才退休；last_ack的历史重放需要保留所属G防止混淆。Media当前USB路径只公开ReleaseFrame，尚无公开M读取接口；建议增加Camera owner只读的当前loan身份查询，直接读原账本，不分配新loan、不改引用算法。这是未来需批准的Media API变更，不能假称已有接口。

SD在原slot内保存G和Retain返回M，用原slot state/generation判断在途与回收，不增加独立outstanding计数。USB归还时仍用原B验证并调用原ReleaseFrame，SD归还仍用M；关联描述只证明归属，不代替实际owner位或Release结果。SD-only临时legacy持有应标为Camera内部交接，不能伪标USB业务G。

### A3 风险和边界

不改变52B AppMessage、56B LifecycleMessage或CameraFrame描述；需要新增生产私有元数据、资源层查询和内部参数/适配。完整G通过本机受同步保护的D/调用参数传递，不跨普通帧队列传G；这不是第二条生命周期请求通道，不执行资格决策。最重要风险是D重用、清理及消息队列顺序，必须冻结全部失败/取消/重放路径。

内存仅估算：D为G16+command4+phase4约24B；Camera当前关联G16+M8约24B；last_ack标签G约16B；每StorageSlot约G16B，共约64+16*S字节（S=APP_STORAGE_SLOT_COUNT，当前1则约80B）。仅列必要身份字段的原始字节预算，不含对齐、额外版本/平台同步元数据；不是实际Map增量。不得为凑预算删身份。Gate复用原Qualification phase，不新增第二份ACTIVE/CLOSING真源。

## 方案B：显式带grant的类型化Frame交接封装

新增受审的Frame交接envelope包含完整G与原Frame消息；请求、Offer、Return和ACK都携带G，Camera每次核对G与B，资源slot同样记录G/M。消息和资源身份显式关联，排队迟到消息无需查外部D即可拒绝。Lifecycle仍保持原56B Tx/Rx/Replay，不新增Replay配额。

建议使用新内部类型/通道适配而不是偷用frame_id/timestamp或command_id编码grant。若直接将现有52B AppMessage扩展G16变68B，会改变相关队列复制和静态存储ABI；若新Frame envelope类型则新通道/类型登记也须单独审核。两者都超出本阶段实现授权，不能先改代码。每Q个消息槽至少多16Q字节（如两方向各L槽则32L），还需每资源私有标签及可能接收栈增长；必须ARMCC5实测后定预算，当前不编译。

该方案仍复用原Media引用权威，不能只因报文G匹配就认定DMA终止或真正Release。更适合未来多在途/多消费者，但当前两路有限交接下成本较大。

## 两方案共同的RELEASE与核验

Qualification验证R当前窗口及其关联G；资源提供方以G查询所有实际交接/loan。R.request_id与G.request_id不相等。资格撤销只在G已关闭且真实归还充分后发生；回复失败不能重新执行资源操作。元数据退休必须晚于真实归还/消息交接闭合；资格退休不能清Frame引用。未来Network需新资源适配和真实DMA终止证明，不复用USB owner位。

## 设计结论

A与B为有条件可实施的设计候选，不是现状证明。A要求严格单在途、描述不可变及退休规则，可避免消息ABI变化；B通过显式身份降低跨队列归属推理难度但影响ABI/队列/栈。建议A的身份标签和准入设计先行评审；若无法完整证明D生命周期，应停止A而重新审B，不退回“无映射默认当前grant”。原两个NOT_PROVEN保持，需经实现审查与专项证据才能关闭。
