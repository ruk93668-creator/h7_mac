# C2.2B Contract Approval Record 与接口设计前置核对

来源：用户本轮人工审批；审批基线aa0edbb。本文记录批准范围，不批准生产实施，不改既有33项状态。分支codex/stage-C2-2B-B2B6-v1-compat；仅新增本设计文档。

## 1. 审批对象对齐

| 对象 | 本次状态 | 未授权内容 |
|---|---|---|
| Replay事务安全边界 | APPROVED — B | 改容量、退休条件、接替规则或新增复用配额 |
| Camera分消费者资源核验方向 | APPROVED | 具体API、结构、消息适配和生产代码 |
| 具体API与生产实施 | PENDING | 未经审核不得实现 |
| B2/23、B2/31原矩阵契约 | CONTRACT_REVIEW_REQUIRED保持 | 本次审批不能自动批准STOP后START BUSY及故障报告复位映射 |
| C2.2B最终验收/C2.2C | 未完成/尚未进入 | 不合并、不推进生产阶段 |

上一轮矩阵两项CONTRACT_REVIEW_REQUIRED实际是B2/23、31，不是Replay和核验接口。本记录保留用户两项架构批准，同时明确不将不同对象合并成已审批。同样，21项真实账本证据仍待补，不由新增接口方向批准自动变充分。

## 2. Replay与现有实现的差异分析

源码：app_camera_lifecycle_replay.c App_LifecycleTxStage、RxReceiveRequest、RxPromote；app_camera_lifecycle.c App_LifecycleAllocateIds、ReplayRetire。

现有固定容量当前Replay加候选槽，四编号域独立；Stage成功推进issued_through，READY取消不回退。RxPromote先检查候选HELD、旧事务completed、final_reply_enqueued和待发已空，再在临时next中构造，成功才推进highest_retired_request_id并替换当前记录。QUARANTINED不提升。编号分配按UINT32_MAX减sequence检查耗尽，不回绕。ReplayRetire不开放成功路径。

“有限”必须区分：现有允许在同一编号域中安全接替多次，固定内存不增长；限制是单调request_id的有限32位域及其他原有身份边界，不是每用满缓存槽就永久停用，也不是新增一个未审查的N次复用额度。已退休请求不得重执行，当前请求仍可重放冻结回复；拒绝不能破坏当前记录。

因此本次批准若指“按现有编号/身份边界耗尽即拒绝”，与C2.1.1方向一致，建议保持实现。若“允许复用额度”指额外次数预算，当前未发现该预算设计，属于待确认差异；不得自行新增配额或更改缓存。耗尽测试须分别覆盖request_id耗尽、候选占用/冲突、单未完成事务及会话/generation边界，不能统称缓存耗尽。

建议补证（设计，未执行）：沿用真实Tx/Rx/Replay运行器，独立隔离四域，测试最后合法编号、下一次分配拒绝且当前记录字节保持；合法重放和同ID不同正文；已退休/跳号迟到拒绝；合法后继接替与回滚；SESSION_QUERY session0及跨session/generation不清编号。测试数值边界可在独立工具对象的合法初始化边界设置起点，但不得修改生产业务对象/资格表或绕过请求流程伪造资格恢复；初始化接口不支持时需先提测试夹具方案。全部现有断言保留。

## 3. 源码确认的核验缺口

- App_UvcBridgeUsbIsIdle只允许USB owner，当前仅判state==BRIDGE_USB_IDLE；Camera不能直接拿它作为自己的安全核验。
- App_UvcBridgeCameraIsIdle由Camera owner读取held/offer_pending/ack_pending/request_pending；没有证明USB端已消费Return ACK，也不是Media按消费者账本查询。
- BridgeCamera_t另有release_pending，USB侧有request_sent/return_sent/cancel_sent等；需逐字段分析各状态下意义，不能只加一个通用bool。
- App_StorageProcessReturns由Camera owner读取槽位，RETURN_PENDING→RETURNING→真实ReleaseLease→同generation核对后FREE；文件状态与归还结果分离。
- Storage s_accepting/s_offering/s_count/slots和当前pending_start属于真实交接状态；Debug.pending_returns/Storage IDLE不足以证明清退。
- FIFO现有HasLoan为全局；真实loans分别保存usb_owned/sd_owned，最后消费者归还才释放块引用。需要按消费者查询而非复制引用计数。

## 4. 建议API契约（草案，待审核，未实现）

### 4.1 Camera统一核验入口

建议 `SysResult_t App_CameraCheckConsumerReleased(const AppLifecycleMessage_t *release_request, AppCameraConsumerReleaseEvidence_t *out)`。

调用者：Qualification released适配器；仅CameraTask，在本轮Bridge处理及Storage实际归还推进后查询。传完整已受控Rx接纳的RELEASE请求，不接收匿名consumer_id布尔查询替代事务核对。匹配session/request/consumer/generation及对应资格RELEASING。接口只读，不执行Media Release、文件操作、UVC取消、Device_Stop或推进资格。不得从Debug/App_Status取得证明。

返回建议：SYS_OK仅表示此时该身份的帧交接归还证明充分，不表示资格已经INACTIVE或硬件停止；SYS_ERR_BUSY表示有在途工作/证明未完成；身份错误按现有错误码返回且无副作用。未知/缺源/状态矛盾必须拒绝，不返回OK。完整错误码表需要后续逐API源码审查冻结。

out是当次权威状态的只读证据视图（字段提案非最终布局）：所查完整身份、数据来源标记、入口已关闭与是否仍有交接/待Return/待ACK、Media对应消费者是否仍持loan、失败/矛盾原因。它不是第二份可变账本，不缓存第二套引用数。跨任务状态不能无锁拼接成成功快照；必须由下述所有者边界建立稳定关闭条件。

### 4.2 模块内部来源建议

- Camera侧Bridge专用查询：仅CameraTask，检查Camera拥有的held、请求/Offer、Return处理和ACK投递记录；不能直接读USB任务私有结构。USB先关闭入口、完成取消/传输、消费Return ACK后才能经原生命周期事务提出RELEASE。如何证明这一程序顺序及迟到消息不重开入口，须绑定完整身份和已有epoch/lease交接，不能仅信消费者声明。若现有字段不能证明，保持BUSY，提交最小握手适配设计，不临时新增消息。
- Storage交接查询：由实际资源所属层提供Camera owner只读函数，原PRIMASK/屏障保护下读取s_accepting=false、s_offering=0、队列空且所有槽FREE、无合法待启动/新交接。逐槽generation和lease与真实归还关联；排除RESERVING/WRITING/RETURN_PENDING/RETURNING。不得由Camera跨Task调用文件清理。
- Media分消费者loan查询：在Media提供Camera owner只读检查，直接遍历原loans的usb_owned或sd_owned并记录对应身份，不复制引用算法/计数。全局HasLoan不作为单消费者结论。需覆盖非MJPEG路径或明确拒绝其未定义核验；不能因为JPEG无loan就默认其他格式安全。

上述模块API名称、参数与数量均待审核；当前只规定语义，不把示意写成已存在函数。优先复用原权威字段和临界区，不新增通用框架或消费者状态机。需要新增状态时必须先给出必要性、唯一所有者、更新点、身份和RAM预算，不能用不一致的跨任务快照填补。

### 4.3 稳定性与使用顺序

消费者明确STOP关闭新帧入口→各自处理在途→原CameraTask真实归还→双方完成既有Return/ACK→消费者提交RELEASE→Camera在受控Rx身份下阻断对应新交接并核验真实来源→证据充分才冻结资格撤销决策→原RxComplete/Flush保证回复。DECIDED/BLOCKED规则不变；回复缓存/运输失败不能重复资源操作。

查询OK不是永久通行证；只有关闭入口在对应generation期间单调保持、且Camera不再向其分发，才可紧接着作资格决策。发现新请求/迟到Offer/身份不符须拒绝或继续BUSY，不能凭旧快照完成。资格撤销、事务回复、硬件停止分别记事实。最后资格退出后还需原安全停止条件，普通停止仍仅FinishRound推进。

## 5. 专项测试计划（本轮未执行）

核验正向：USB单独退出；Storage单独退出；双路运行；一方退出另一方继续（只撤销目标资格，另一方loan/采集不变）；最后消费者退出后仅提交既有停止意图，真实硬件完成另验。

拒绝/异常：目标仍持lease；RETURN_PENDING/RETURNING；错误owner无实际Release；重复Return不重复扣引用；旧session/generation/lease迟到；Offer处于RESERVING与Return交错；文件关闭失败但帧已真实归还；文件关闭成功却仍持loan；RxComplete BUSY保留决策；USB ACK已入队但未消费；FAULT保护停止后仍持loan。缺真实核验接口时只能明确标记核心Ops注入测试，不能称生产适配通过。

软件必须执行真实FIFO并记录正确消费者账本及双方交接；未来采集方法沿用RuntimeEvidenceDesign。新核验API未审核/未实现前，可先补只读运行轨迹，但不能伪造生产released/idle成功。本阶段不改任何生产代码、接口、ABI、任务/队列/优先级，不覆盖历史证据，M4延时保留。

## 6. 下一步审核点

1. 确认Replay“复用额度”指现有有限身份域，还是额外预算（后者必须另审）。
2. 审核Camera入口只读语义、完整身份、分消费者来源和稳定关闭边界；尤其USB ACK消费与RELEASE顺序的可证明性。
3. 单独决定B2/23与31，不能由本次方向批准自动结案。
4. API和适配证明方案通过后，再按单独授权实施；C2.2B最终验收保持未完成，C2.2C尚未进入。
