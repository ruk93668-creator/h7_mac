# Scheme A Metadata Lifecycle — Exact Contract Draft

方案A已获人工设计批准，生产实施未授权。源码基线为66d3a0b545d1b2bf80d215878964d349f830100b；14bbdcc是前次源码审计起点，14bbdcc→66d3a0b只有三份设计文档，生产代码树相同。本轮未运行测试；SOURCE_CONFIRMED表示源码事实，DESIGN_DEDUCED表示附带前提的设计推导，RUNTIME_PROVEN本轮没有。

## 1. 身份和记录分离

G={session_id, acquire_request_id, consumer_id, consumer_generation}，R含独立release_request_id，B={stream_epoch, bridge_lease_id, frame_request command_id}，M={Media loan.session,id}，S={Storage local session,slot,generation}。D不是引用账本；它把既有有限交接与G绑定，不拥有Frame，不可据其TERMINAL自行Release。

SOURCE_CONFIRMED：app_uvc_bridge.c BridgeUsb_t只有一条REQUEST/lease，next_request单调且到UINT32_MAX拒绝；BridgeCamera_t持有原offer/ack/last_ack；app_camera_consumer.c资格G源于QUERY/ACQUIRE，RELEASE使用first_request+2；app_camera_qualification.c released回调接收ACQUIRE grant键，不是R编号。

## 2. D物理设计：单写字段，不是双任务共享phase

建议新增私有结构，确切C布局待审核：

- U（USB唯一写）：不可变G、H=frame request command_id、publication_phase、request_committed、consumed_H。G/H在PUBLISHED期间至RETIRED禁止改写。phase只管理本端EMPTY/PUBLISHED/RETIRED，不由Camera修改。
- C（Camera唯一写）：复制的G/H、camera_phase、B、M、terminal_flags。表示接受/绑定/归还进度及本次终结事实，不改变原Media所有权。所有读取都取得完整一致快照。
- F（Camera唯一写）：highest_terminal_frame_request，单调、拒绝已终结H重新受理；不是Replay高水位，不修改生命周期编号域，不提供配额。当前需要它是因为退休后不再保存所有D正文，而迟到Frame请求仍可能可达。无此原帧请求隔离边界，D不能安全复用。
- last_ack_G（Camera唯一写）：为原Bridge last_ack补所属G标签；只服务重复Return重放，不产生新Lease。保留原last_ack三元组。

U由USB写、Camera读；C/F由Camera写、USB只读；不能让USB清C或Camera清U。所有跨任务共享读写使用同一短PRIMASK保存/屏蔽/内存屏障/恢复协议；不调用队列、UVC、Media、文件操作，不等待，不跨函数异步持锁。仅限CM7任务间；CM4不得写，不能声称PRIMASK跨核有效。ISR不写U/C，任务代码不得在异常上下文调用。

## 3. 候选状态评估与逻辑状态机

八个候选名保留为逻辑阶段，不直接照搬成一个共享enum。RETURN_PENDING与Storage同名但不是同一存储字段。

| 逻辑阶段 | 权威记录及唯一写者 | 进入条件/后继 |
|---|---|---|
| EMPTY | U初始，USB | 从未发布；不能每轮Init回到EMPTY |
| PUBLISHED | USB完整写G/H后最后发布phase | 可尝试send；队列满保持；未提交可本地取消 |
| REQUEST_COMMITTED | USB request_committed=1；实际队列入队是提交事实 | 不能取消/清D，只能走Cancel/Offer/Return；等待C |
| CAMERA_ACCEPTED | Camera C.H=H及ACCEPTED | 真正出队、D一致、G权威ACTIVE、H合法后登记 |
| FRAME_BOUND | Camera C.B/M已固定且真实GetFrame成功 | 允许Offer发送/重试，必须真实归还 |
| RETURN_PENDING | Camera C归还/等待ACK记账；USB既有RETURN状态亦有独立含义 | Release失败保留；成功但ACK未入队仍不能终结 |
| TERMINAL | Camera C终结H和flags发布 | 真实资源侧已完成（或从未取Frame），对应终结消息已成功入队 |
| RETIRED | USB验证终结与消费后写U phase | 下一H可覆盖U；旧H由F/当前匹配拒绝重新准入 |

阶段由U/C当前H一致的快照推导。Camera可能在USB尚未写request_committed前接受：xQueueSend成功后可发生调度，不依赖优先级排除此情况。Camera已真实收到队列项且U完整PUBLISHED就是提交证据，不因commit字段暂未写而拒绝，也不修改USB字段。相同H的C进度优先于滞后的USB本地记账，绝不回退。

## 4. 发布与提交线性化

USB只能在前D已退休且原Bridge允许新请求时构造新H，完整G来自其已匹配的GRANTED；D先锁内发布，之后锁外执行原52B xQueueSend。成功入队是REQUEST_COMMITTED的线性化点，不是D发布。失败无队列项，保留同一G/H重试；不能重新分配请求号。

未提交取消必须在同USB任务确认本次和此前所有send均失败、没有队列提交，且C未接受H后进行。锁内写本地RETIRED；H仍消耗不回退。Camera无可能合法接收此H，之后查不到匹配D或无已提交消息就拒绝。若对send结果未知或C已接受，则不得走此捷径。

## 5. Camera接受与绑定

真实FrameRequest出队后读取U一致副本，确认H匹配、G完整等于Qualification权威grant、phase ACTIVE。若H<=F且不是当前尚未终结H的合法重复，则只拒绝。不同H在当前交接未终结时BUSY/负向完成，不覆盖C；相同H不同D内容是协议错误。已有pending重复不再GetFrame。

接受请求只是有界占位；每次准备GetFrame还须再检查G ACTIVE。关闭后未取Frame的请求生成原错误Offer、lease_id=0，不获取资源；已取Frame的C固定G/B/M后只能排空。M必须来自未来只读Media查询，不能从Bridge lease_id推算。Stage失败或身份不一致留现场，禁止默认归属当前G。

## 6. 终结、消费和安全退休

有帧：真实StreamReleaseFrame成功，原Media owner位已撤销；Camera构造Return ACK后成功入队，才把C置TERMINAL并推进F。ACK入队失败保留C和原ack_pending；禁止再Release一次来重建ACK。USB只在现有RETURN/return_sent/SameLease/result OK条件下消费ACK，然后记录consumed_H=H。

无帧：Camera未取资源的负向Offer成功入队为终结消息；USB现有REQUEST/command_id匹配消费该错误Offer后记consumed_H。不存在Return ACK时不得伪造一个ACK。已提交Cancel必须等Camera负向Offer或实际Offer后归还，不能假设Camera未取Frame。

抢占重点：Camera xQueueSend成功可能立即让USB消费ACK，但Camera尚未写C TERMINAL/F。USB虽可把原Bridge置IDLE，**仍必须阻止新D发布和新FrameRequest**，直到看到同H的Camera终结记录。USB本地ConsumerEnd也需等待该D终结/退休条件，而不是仅看BridgeIdle。Camera完成终结记录后通知USB重新检查，丢通知依周期重试，不新增平行ACK消息。

USB退休条件为：同一U.G/H与C.G/H；Camera证明本次资源完成或从未获取；C终结标志及F覆盖H；本端已匹配消费对应终结消息；USB无payload访问/Return工作，且没有尚未提交的同H发送；此时USB锁内写RETIRED。Camera终结前必须保证其后不再按U指针读取旧D（使用C副本/last_ack标签），否则不能退休。

RETIRED不表示所有旧队列消息物理消失，而是它们不能重新取得权限：FrameRequest按F或新D.H不匹配拒绝；旧ACK按USB state/B/H不匹配拒绝；重复Return只匹配原last_ack及其G重放、无资源副作用；新last_ack覆盖后旧Return拒绝。next_request永不回绕、不因新grant重置。F和元数据不能因普通Stop/Grant切换重新Init。

## 7. Storage对应契约

Camera同步Dispatcher→StorageOffer显式传G，不让StorageTask裸读Camera资格。原slot FREE→RESERVING窗口将G与S绑定；Retain成功后写真实M，同session/accepting检查决定QUEUED或RETURN_PENDING。slot G由Camera Offer写，FREE后下一次Camera Offer才覆盖；Storage只推进原slot状态/业务，不改G。Camera Return保存G/S/M一致副本，成功且generation匹配后FREE；FREE之前不清身份。无需另造D或inflight引用计数。

SOURCE_CONFIRMED为app_storage.c OfferFrame/Process/ProcessReturns的现有槽位算法；G参数/标签为DESIGN_DEDUCED新增建议，未实现。核验以原s_offering/s_count/slot states和真实Media owner为准，Debug.pending_returns不是权威。

## 8. 证明状态

该设计给出发布、提交、终结消费和拒旧闭环；其正确性依赖所有新门均实际接入、单写字段及错误路径完整。原源码没有D/F/G标签或只读M查询，故两个全局缺口继续NOT_PROVEN；没有本轮RUNTIME_PROVEN结果，不据设计直接批准生产。
