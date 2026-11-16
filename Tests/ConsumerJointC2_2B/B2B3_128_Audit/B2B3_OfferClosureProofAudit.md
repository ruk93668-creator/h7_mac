# Offer Closure Proof Audit

基线9cf537d。审计不等于动态验收；所有路径仅据现有生产源码，两个核心缺口仍NOT_PROVEN。本轮无测试执行。

## 全部已定位的生产Frame交接入口

检索CM7/Application与CameraCapture调用点，并追踪函数体，不按active=false作结论。

| 路径 | 调用链/所有者 | 关闭与排空 |
|---|---|---|
| USB正常申请 | UsbTask→BridgeUsbProcess(enabled) IDLE→FRAME_REQUEST TO_CAMERA→Camera Bridge→GetFrame→Offer TO_USB→USB HELD/LOANED | USB CanTransfer=false后不从IDLE新发请求；已请求/Offer必须继续取消或Return |
| USB延迟Offer | Camera offer_pending→Bridge_CameraFlush→TO_USB→UsbProcess接Offer | Flush在本轮enabled判断之前；关闭入口不撤销已成立的持有，必须完成Return/ACK |
| SD随USB帧 | Camera成功GetFrame→DispatcherPublish→StorageOffer→真实Retain | Storage自己的accepting/slot门控，不影响USB原持帧 |
| SD-only取帧 | Camera Bridge无USB请求/held且整体enabled、DispatcherNeedsStorage→GetFrame/local_only→Publish→本地Release | 没有USB协议Lease，不伪造USB Return ACK；SD独立持有继续 |
| Storage公开Offer入口 | App_StorageOfferFrame(frame,meta)要求Camera owner/已准备/合法metadata，再锁内检查accepting与slot | 当前生产调用者为Dispatcher_StorageOffer；未来新增调用点必须重新纳入审计 |
| 通知/异步完成触发 | USB/Media回调仅RuntimeEvent通知Task→原Process链 | ISR不直接Offer；通知合并只影响调度，不创建新的独立入口 |
| 队列延迟/重复帧消息 | Frame request/cancel/Return/Offer/ACK独立队列出队 | 跨生命周期队列无全局FIFO；迟到消息需按真实state/lease校验，不能视队列已空 |

## USB关闭时序

关闭者：UsbTask中的App_USBHandleCommand STOP设置stopping；CanTransfer=started&&!stopping。Task依原Cancel→UVC Process→Bridge(false)→必要再次UVC Process→FinishRound推进。Bridge在REQUEST未入队时可撤销至IDLE；已入队时Cancel Request；HELD直接Return；LOANED须UVC无frame_owned或原Class失效条件才Return；RETURN收到匹配成功ACK消费后IDLE。FinishRound等待BridgeIdle/ModeStop成功后ConsumerEnd，RELEASE Stage后phase变化避免重复事务。

局部证明SOURCE_CONFIRMED：同USB任务正常STOP后不会在enabled=false时从IDLE新发frame request；ACK消费先于该已持帧路径的End。CancelFrame仅登记CANCELLING，不是DMA已停；UVC Process后续Abort成功/完成确认才释放Payload，保持源Frame引用直到原真实条件。

未闭合部分：Camera端收到FRAME_REQUEST只检查command_id/held/request_pending等，不核验grant代次；整体enabled只检查Camera running/stopping。其他消费者仍活动时enabled可继续true。既有USB状态机是否足以排除所有旧Request/Cancel/Offer在独立队列之间交错后重新建立该grant交接，需穷尽证明，当前NOT_PROVEN。不能将此缺证明写成已复现生产缺陷。

## Storage关闭与重新开放枚举

- Storage_OnStop:739起先清pending_start；非FAULT时accepting=false，置stop_pending并可能DRAINING。FAULT分支提前返回，其安全性须结合Storage_Fail/清理先前关闭入口，不孤立推导所有FAULT必已关闭。
- Process:568在stop_pending时关闭接收；Snapshot首次接受后:328关闭；失败/清理路径:978、1015关闭。App_StorageWantsFrame只提供需求判断，真正Offer仍要二次检查。
- Offer:274起锁内RESERVING/generation/offering；锁外Retain；再次锁内若STOP或session变化则转RETURN_PENDING。已Retain帧不可直接FREE。排空须offering=0、队列空、每slotFREE，并由Camera完成实际归还。
- 唯一已定位accepting=true赋值位于Storage_StartGranted:722，受StorageTask执行；调用者Process在pending_start且Consumer GRANTED时执行。OnStart通过ConsumerBegin及本地state/mount/pending门控，新的同grant请求不能直接绕过Begin。
- Storage_LifecycleFinish:1083起检查pending_start为空、phase GRANTED及quiet/所有slotFREE再End。它是消费者侧条件，不是独立Camera核验；锁外End至未来Camera核验期间的身份化关闭链还未实现。

局部SOURCE_CONFIRMED：STOP/Offer竞入的已Retain lease会保留为待归还；成功归还后才FREE；新accepting只在授权START路径开启。全局NOT_PROVEN：缺grant绑定与Camera核验API，尚未证明所有旧事务/新排队命令/本地session组合不会在核验窗口重新开放同一grant。仅从true赋值少不能证明整个生命周期闭环。

## 核验窗口与退出确认

当前CameraTask先QualificationProcess，再Media/Bridge归还，再FinishRound；本轮刚完成归还通常下一轮才被资格核验观察。普通STOP只由FinishRound推进；资格released/idle NULL意味着尚无真正成功核验窗口。不能把不存在的API返回OK或消费者grant清零作为现状。

目标（DESIGN_REQUIREMENT）：核验绑定G=ACQUIRE grant和R=当前RELEASE，G入口单调关闭；同Camera所有者核对Bridge/Media，Storage共享交接字段按原临界区一致读取；从核验到冻结撤销决策不能重入开放G。USB私有侧不裸读，依可靠顺序及必要的权威适配证明。查询失败/信息未知继续等待，超时不强制回收。只有真实核验、资格撤销、匹配回复消费分别完成才有相应退出事实，最后活动消费者退出还需原硬件安全门。

## 已知缺证和最小后续设计

至少补：grant到frame关联；Camera侧分消费者Offer可准入条件；独立队列迟到交错；ACK入队/消费差别；SD-only临时legacy持有；Stop/Retain返回/槽位回收交错；非JPEG路径。保持原账本，若需新增关联/关闭事实先提结构和owner，不在此新增第二套引用计数。Network仅冻结约束，不作为新入口接入。
