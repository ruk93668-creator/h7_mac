# Camera API Contract Review — 源码级审核

基线2b0df5a。审核对象为B2B3_ContractApprovalAndAPIProposal.md第4节的示意API，不是已存在API的验收。本轮只新增文档，不运行测试，不修改任何代码、ABI、配置或历史状态。M4延时零修改。

## 结论

架构方向成立；**具体API草案尚不具备实施条件**。当前能确认协议/资格存在身份检查和保守阻塞，不能证明分消费者退出的完整闭环。关键阻塞：ACQUIRE grant身份与RELEASE事务身份的接口错位；USB Return ACK消费证明；关闭入口后不重开交接的一致性边界；Media消费者账本覆盖全部实际采集格式。不能用额外bool或Debug填补。

## 1. 完整身份与回调语义不一致

源码app_camera_qualification.h:39声明released(context, const AppLifecycleIdentity_t *grant)，:62标明grant_request为建立资格的ACQUIRE编号。app_camera_qualification.c:332–341在RELEASE分支核对grant_session/generation、进入RELEASING后，复制当前请求identity，再把grant.request_id改为e->grant_request，传给released。它接收的是grant身份，而非当前RELEASE请求编号。

上一版示意App_CameraCheckConsumerReleased(const AppLifecycleMessage_t *release_request, out)不能原样当作该回调：把两个request_id视为同一身份会错误拒绝合法退出或误用旧证明。建议保留既有Ops语义，明确两层：资源查询按完整grant身份核验其资源；Qualification/Rx以当前受理的RELEASE身份约束调用和决策窗口。若需要资源查询同时接收两份身份，应先提交最小Ops调整方案及关联规则，不能暗改参数或直接访问私有资格表拼接。具体取舍待审核。

当前Qual_Window/受控RxPromote与work_*绑定可保护执行窗口，Replay边界可阻止退休请求重新执行；这不自动给资源查询建立grant→frame关联。还须说明消费者generation对应哪个stream_epoch/loan，不能互相赋值替代。

## 2. 分消费者权威来源

| 来源 | 源码事实 | 能证明 | 不能证明 |
|---|---|---|---|
| Bridge Camera侧 | app_uvc_bridge.c:129–134，仅Camera owner，检查held/offer_pending/ack_pending/request_pending | 当前Camera侧部分在途标记 | USB已消费ACK、所有双端标记一致、指定grant身份的永久关闭 |
| Bridge USB侧 | :419–422，仅USB owner且state==IDLE | USB任务自己的状态条件 | Camera可跨Task直接调用、全链资源证明 |
| Storage槽位 | app_storage.c:350–394，Camera归还；s_offering/s_count/slot states为交接账本 | 真实成功返回后且generation/state匹配可FREE | 文件关闭/Storage IDLE等于归还；Debug计数等于账本 |
| Media loans | camera_mjpeg_fifo.c:377–446、840起，usb_owned/sd_owned独立，最后持有者归还才释放块 | 分消费者实际帧所有权与引用变化 | 全局HasLoan()==false足以证明对应事务退出；非MJPEG路径天然已覆盖 |

草案必须读取原权威字段而非建立第二份引用数。Camera检查USB资源时不能要求SD也无loan；SD检查也不能要求USB无loan。查询不得更新其他消费者状态、停止硬件或执行文件操作。

## 3. 在途、重复及错误owner

Storage从RESERVING/QUEUED/WRITING到RETURN_PENDING都不是完成。ProcessReturns要求Camera owner；进入RETURNING后调用真实ReleaseLease，成功且slot generation相同才FREE，失败重置RETURN_PENDING。读状态必须遵守原PRIMASK和内存屏障，不能将多个不同时点的字段拼成OK。

真实FIFO ReleaseLease按lease.session/id及sd_owned匹配；重复/旧凭据不能再次扣当前引用。错误Task的拒绝发生在Storage协调层；不能声称FIFO本身拥有同样的任务权限检查。USB的重复Return重放last_ack，不得重复实际Release；需结合Bridge_SameLease的epoch/lease_id/command_id，不能仅看全局released计数。

旧凭据与新槽位交错、RETURNING时旧请求到达、USB ACK已入队未消费等仍缺完整动态证明，本轮不推测通过。超时不释放、不重Init、不清Replay。

## 4. USB双端一致性阻塞

Camera Bridge ack_pending清除只证明回复成功入队，不证明USB消费。App_UvcBridgeCameraIsIdle没有release_pending字段的显式判断，字段间可能存在代码不变量，但不能凭此独立断言缺陷或安全；需逐路径证明它不可能在其他idle条件全真时仍保留实际释放工作。last_ack可用于重复Return，但不等于对端消费证明。

建议协议顺序：USB关闭接收、完成取消和实际Return、消费原ACK后才发RELEASE；Camera受理对应事务时停止对该grant新交接，并核对Camera侧和Media侧。如果此顺序可以从真实任务和队列链证明，不新增平行ACK协议；若现有权威字段不能表达，则保持BUSY，先设计最小跨任务证明适配。不能让Camera直接读USB私有内存或把消费者自报FRAMES_DRAINED当证明。

## 5. 一致性/原子性契约

只读核验本身不能关闭入口。应由既有业务STOP关闭自身入口，Camera按资格RELEASING拒绝新Offer；“关闭动作由谁、在哪个既有状态执行”必须先定位为真实调用链。当前released/idle为NULL（app_camera.c:629起），尚无生产适配，不能描述这条关闭链已经实现。

核验期间由单Camera所有者读取Camera/Media账本，Storage共享交接状态在原同步窗口读取；对USB侧使用可靠顺序或所有者发布的稳定事实，不使用随时变化的跨任务裸快照。证据有效范围必须绑定grant及当前RELEASE窗口；查询OK至冻结决策之间不得重开新帧入口。若无法证明此单调关闭条件，返回BUSY/错误，绝不撤销资格。

out仅当次视图，非业务真源；错误、字段矛盾、身份缺失、未知格式均拒绝。查询无硬件/文件/Release副作用；重复查询和RxComplete BUSY不触发额外资源操作。没有可审查的稳定性协议前，不能冻结具体结构布局或返回码全集。

## 6. 七项要求的证明状态

| 要求 | 当前源码/设计状态 | 待补条件 |
|---|---|---|
| 正确消费者 | 核心USB/Storage分entry；FIFO两owner位 | Bridge/slot到grant的完整绑定与格式覆盖 |
| 正确事务 | Replay/work窗口及grant键已分离 | 新API明确ACQUIRE与RELEASE双身份职责 |
| 未完成不判完成 | 槽位成功后FREE；NULL released不返回成功 | 多源一致性证明、真实执行证据 |
| 旧请求不可绕过 | 退休高水位/代次/窗口限制 | 与新核验记录的身份关联，不能用旧快照 |
| USB退出不动SD | FIFO独立owner位提供基础 | 新查询无跨消费者副作用、真实联合测试 |
| SD退出不动USB | 同上；最后持有者才释放块 | 同上，不能要求USB引用也为零 |
| 最后退出前不停Camera | Qual_OtherWork与STOP_IF_IDLE安全门、FinishRound唯一普通推进 | idle真实证明仍NULL，不能宣称完整关闭通过 |

## 7. 最小后续设计与实施门禁

先明确Ops身份适配；逐路径证明Bridge双端ACK/迟到消息/关闭入口不重开；明确Storage同步读取及Media各格式账本；给出唯一所有者与必要字段RAM预算（如无新增字段则说明复用来源）。之后才冻结API签名与码表并请求实施授权。不得为了API好实现改Replay、Frame Lease、任务配置或增加通用管理器。原21项补证、两项重启契约、C2.2B验收分别保留，不因本审核结案。
