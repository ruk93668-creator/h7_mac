# Scheme A Admission Proof — Exact Design Obligations

源码基线66d3a0b。SOURCE_CONFIRMED：当前App_CameraCanOfferFrame仅running&&!stopping，Bridge无G检查；Qualification合法RELEASE分支ACTIVE/RELEASING匹配原grant后置RELEASING；released/idle仍NULL。以下是设计规则和条件证明，非已运行证明。

## 四个线性化位置

- L_stop_local：USB HandleCommand设stopping关闭CanTransfer；Storage OnStop取消pending_start并关闭accepting（FAULT提前返回须结合失败路径）。它不是Camera已关闭。
- L_close_camera(G)：Qualifier在受控R窗口精确匹配原grant后写entry.phase=RELEASING。新准入必须要求同G且ACTIVE，不再复制一份open/closed真源。
- L_admit(G,H)：Camera真实出队、D快照匹配、G ACTIVE并登记C ACCEPTED的提交；Storage为Camera同步栈核对G后在原锁内FREE→RESERVING并绑定G/S。排队请求不等于拥有Frame。
- L_verify(G,R)：受控R仍匹配原grant且G已关闭，所有G交接终结/消费、真实owner不再持源后只读核验充分；与冻结撤销决策处于同Camera无重入执行链。

R.request_id不同于G.acquire_request_id。现有released回调仅拿G，R合法性继续由Qualifier work/Rx窗口负责；不能将示例R参数原样塞进回调。建议保持Ops函数签名，资源回调只查询G，关闭来自先前资格phase写点。若未来需要显式传R，另审Ops变更，不暗改。

## 十条入口逐项要求

| 路径及源码 | 未来准入/排空规则 | 条件证明 |
|---|---|---|
| USB FrameRequest，BridgeUsbProcess/CameraProcess | USB发布D先于入队；Camera匹配G/H且ACTIVE | 关闭后新请求不能ACCEPTED；旧H无D或被F拒绝 |
| Camera GetFrame，Bridge :220/:243 | 实际取帧前再查当前G/需求；SD-only以SD G和内部角色记录 | ACCEPTED等待资源跨轮不能保留过期准入许可 |
| Bridge Offer，:252附近 | GetFrame成功先原位固定G/B/M再发布offer_pending | 已绑定资源只能排空，不能改归新G |
| offer_pending，CameraFlush | 关闭后仍发送已绑定Offer或负向终结 | 这不是新准入，USB停用后走Return，不直接丢弃 |
| Storage DispatcherPublish | 对SD目标独立取得权威G并传同步参数 | USB准入不代表SD自动有资格，SD拒绝不回收USB |
| Storage Retain | 原RESERVING提交时绑定G/S，锁外Retain后记录M | Camera任务中G关闭动作不与其重入；Storage STOP可竞入但只转待归还 |
| Storage Return | 始终允许关闭G下Camera执行真实归还 | 关闭不得阻止释放旧资源；成功才FREE |
| USB Cancel | 未提交本地退休；已提交等待负Offer或真实Return/ACK | 不以cancel函数返回当作DMA不再访问源 |
| 延迟队列/通知 | 每次实际执行都按G/H及原队列身份检查 | 通知无授权；不能为旧请求套当前G |
| RELEASE与Offer并发 | 同Camera owner先RELEASING后新准入检查；已绑定交接继续 | 不依赖两个独立队列全局FIFO |

所有生产获取/Retain路径必须被纳入：CameraTask→Bridge普通USB及SD-only两处GetFrame，Dispatcher→StorageOffer；未来新增入口未登记即不允许上线。公开API不能存在绕G的替代调用。非MJPEG分支必须有等价身份方案或明确拒绝未定义核验，不将JPEG证明推广至所有格式。

## 条件性安全推导

P1：Qualifier是G状态唯一写者，旧G一旦RELEASING不返回ACTIVE；失败/回复重试不重新开放，新的ACQUIRE产生不同generation/请求。需逐路径确认，不修改原Replay。

P2：所有取源Frame/Retain都实际检查当前G，检查与同任务提交不执行可重入业务/让出；共享slot使用原临界区。这使L_close后新取资源不成立。若检查仅在REQUEST时一次执行、下一轮GetFrame不复查，证明不成立。

P3：已绑定的G/B/M/S不可变，在真实归还并终结消费前不覆盖；拒旧规则阻止D退休后旧请求重开。旧Return可以无资源重放ACK，不能再Release；USB ACK已消费但C尚未发布终结时要等待，不许发布新D。

P4：关闭与核验中所有状态从权威来源取得，Camera不裸读USB私有业务结构；USB自己的消费事实用受同步保护的单写consumed_H，Camera只读同H且C已经终结。Storage快照按原锁，不把Debug拼接为结论。

P5：G关闭后核验到冻结决策期间没有重开G操作，Query只读无回调，Qualifier禁止重入。未知/身份矛盾为BUSY/错误，不撤销资格。另一消费者继续ACTIVE不影响目标归还，最后退出还需idle/硬件停止条件。

P1–P5满足才形成DESIGN_DEDUCED证明。目前D/F、新G门、消费屏障、Media查询均未实现，整体保持NOT_PROVEN。仅检查active=false或accepting=false不能建立P2/P3。

## 资源访问与异常

Copy发送须所有读取/复制DMA真正完成才Return；zero-copy须所有源指针的异步DMA完成或Abort确认。USB_UVC_CancelFrame仅置CANCELLING，原UVC_Process/Abort/ReleaseFrame条件仍保留，不用D终结替代。MediaRelease失败保持原持有及RETURN_PENDING，不使C TERMINAL；CameraStop失败只保留停止事实，不能重建Frame账本或清D。

Storage写失败可在停止读取后进入真实Return；文件错误不得被归还成功覆盖。StorageOnStop与锁外Retain竞入时，原二次session/accepting校验负责待归还，不额外改变引用算法。

## 核验结果范围

查询G只能证明指定grant的交接闭合；不能宣布R回复已被消费、全部消费者退出、卷释放或硬件已停。释放资格和资源查询分离；成功记录只对该不可变G及当前受控R有效，旧记录不得用于新generation。当前released/idle=NULL继续使生产闭环阻塞。B2/23有界BUSY、ID31阶段二成功重启仍按原人工决策保留待验。
