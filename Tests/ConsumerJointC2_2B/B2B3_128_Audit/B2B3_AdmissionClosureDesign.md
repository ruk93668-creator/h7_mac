# Admission Closure Design

基线14bbdcc。以下线性化位置均为拟议未来改动；当前源码只有局部门控，不声称已实现稳定关闭。

## 原调用路径与风险清单

| 路径/交错 | 现有源码 | 设计处理 |
|---|---|---|
| 正常USB请求 | BridgeUsbProcess IDLE→FrameRequest→TO_CAMERA→CameraProcess取Frame | 按G准入，不只看Camera整体running |
| 已入队未执行请求 | app_messages.c独立FIFO；Camera每轮桥接出队 | 请求D/envelope保留旧G；关闭后只拒绝/终结，不取新Frame |
| offer_pending重试 | Bridge_CameraFlush在enabled判断前执行 | 属已承诺交接，关闭后允许排空，不视作新准入 |
| SD随USB/SD-only | Bridge两处DispatcherPublish→App_StorageOfferFrame | 分别按SD G决定Retain；SD拒绝不取消USB已有持有 |
| STOP与Offer/Retain竞入 | Storage Offer锁内RESERVING，锁外Retain，再锁内核对accepting/session | 已准入交接必须结算；STOP后Retain成功也转待归还，不能丢lease |
| RELEASE与帧队列并发 | Camera先QualificationProcess再Bridge；不同队列无全局FIFO | 关闭在受控RELEASE窗口内先发生；随后旧帧请求由G gate拒绝 |
| 通知后再执行 | AppEvents只Post事件，Task再次跑原入口 | 通知无授权载荷；每次实际准入仍读权威G/phase |
| 退出确认后迟到 | 旧Request/Return/ACK可能滞留 | 无匹配当前G/原关联只拒绝或重放原终结ACK，绝不再分配新Frame |

## 两个关闭时点，不能混称

L_stop_local：USB业务STOP将stopping置true使CanTransfer=false；Storage STOP关闭accepting并取消pending_start。它阻止本地新需求，但不是Camera已收到关闭，也不能撤销此前入队请求。

L_close_camera(G)：建议复用Qualification在合法RELEASE分支验证原grant后，将该entry.phase置RELEASING的既有写点（qualification.c:338）作为Camera准入关闭线性化位置。所有未来TryAdmit必须读取同一权威entry，要求G完全匹配且phase ACTIVE；不另维护一份gate bool。R经Replay/work窗口验证，不由任意消息或Debug触发关闭。该点在同Camera任务中先于后续Bridge新准入。

目前Bridge并未读取此条件，因此当前不变量仍NOT_PROVEN。若要在用户本地STOP瞬间就禁止Camera处理所有此前排队请求，需要额外异步关闭协商；本推荐只承诺本地STOP阻止新发、Camera受理RELEASE后全局禁止G新准入，之前已准入则排空，不新增未经审核的STOP消息。

## Offer受理线性化位置

L_admit(G,H)：在Camera任务中，检查当前权威grant G且ACTIVE，并把本次handoff H绑定G及占用原在途结构的那次提交。必须在取Media Frame/Retain前完成，并保持检查与提交之间没有可重入业务、没有任务让出；ISR不得改Qualifier。USB为Camera接受带已验证G的FRAME_REQUEST并登记request_pending/当前关联；Storage为Camera同步调用链内、原slot锁窗口将FREE→RESERVING并绑定G的提交。

消费者只是请求，不在发送成功时即取得Frame。若资源暂不可用，H仍明确pending且受限，不无限新增计数。若在同一Camera轮次准备取frame时G已关闭，未取得Frame的请求必须拒绝/取消并清理元数据；不得凭先前ACTIVE采样在下一轮分配。已经取得Frame并形成Offer_pending的交接属于已承诺工作，允许在关闭后仅排空；必须清楚区分“请求占位”与“已持Frame”。

Qualifier和Bridge均由Camera任务推进，Storage共享slot提交保持原PRIMASK/屏障。G的权威检查不由StorageTask裸读；Camera调用适配时传一致G，Storage读取本地接受状态仍受原同步约束。不同消费者G的门互不影响。

## 在途统计与排空

不新增第二套引用计数。交接是否在途来自原request_pending/held/offer_pending/ack_pending/release_pending、USB REQUEST/HELD/LOANED/RETURN、Storage s_offering/s_count和slot states，以及真实Media owner位。身份标签标出这些既有记录属于哪个G。

REQUEST尚未取得frame可负向终结；已Offer/持帧必须真实取消/传输完成、Return与Camera实际Release；ACK入队不是消费。USB对匹配ACK消费后才本地IDLE/End，若未持过Frame可无Return ACK。Storage RETURN_PENDING/RETURNING都阻塞核验，文件Close/Unmount成功不代替FREE。USB已归还但SD仍持同帧时，仅USB G可完成资源证明，块仍保留。SD-only暂时legacy持有归Camera内部，不挂错误USB G。

关闭后不允许丢掉D、清slot或清loan来使统计变零。失败返回保持原可重试状态；异常文件结果独立保留，超时不强回收。

## 迟到与核验稳定性

使用方案A时，旧command_id查无不可变D或D.G不等于权威G，不能用当前grant补齐；方案B直接从envelope取旧G再拒绝。重复Return仅按原B/M及其G关联重放原ACK，不重复扣引用。资格已INACTIVE或新grant已ACTIVE时，旧G永远不重新开放；新grant只能经合法新ACQUIRE，编号/代次不回绕。

L_verify(G,R)：只有Camera关闭已线性化、G下无未结算交接/真实loan且必要ACK消费已有可靠顺序证明，才能返回资源核验充分。查询到冻结决策之间不调用会开放G的函数、不让出、不重入；Qualifier当前受控R工作窗口与grant保持一致才撤销。RxComplete失败不撤回已发生归还，也不重新开放G，保留DECIDED/BLOCKED事实。

需要证明不变量：I1所有获得源Frame/Retain路径都经过G准入；I2关闭后G永不回ACTIVE；I3在途记录只能通过真实完成结算；I4迟到消息不能按当前G重新归属；I5核验到决策同所有者无重入；I6未来所有DMA不再访问源Frame。任一未覆盖则NOT_PROVEN，不标成功。

## 可执行设计验证矩阵（未运行）

分别在D发布前/入队后/Camera取出后/GetFrame后/Offer待发/Retain锁外/文件写中/RETURN_PENDING/RETURNING/ACK入队后未消费/RELEASE Promote后/资格确认后注入合法STOP或迟到消息；保存G/R/B/M/slot键、所有者、返回值及阶段快照。负对照：绕gate、旧G标成当前、ACK未消费清记录、重复扣引用、关闭后新Frame准入均应被门禁拒绝。

若未来涉及多在途网络，需更强交接描述管理与DMA完成证据，不能直接推广当前单槽A方案。本轮不实施C2.2C、网络消费者、配置或API。
