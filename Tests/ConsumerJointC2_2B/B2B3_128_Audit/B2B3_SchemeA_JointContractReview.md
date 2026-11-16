# Scheme A Joint Contract Review

## 1. 范围、Git及结论

起点4c3df6ef35cbf6355bba318e424b433c7d699742，分支codex/stage-C2-2B-B2B6-v1-compat。开始仅原未跟踪output/。已正常push，远端由66d3a0b快进到4c3df6e；无reset/rebase/force。五份全文均逐份读取，18行场景逐项核对，不只看摘要。

生产审计版本为14bbdcc。git diff 14bbdcc 4c3df6e的CM7/CM4/Common/Board/MDK-ARM无差异；又以git show 14bbdcc逐文件字节核对本地Bridge、Qualification、Storage、FIFO和Capture关键链一致。66d3a0b与4c3df6e为后续设计文档版本，不是新的生产功能。

**结论：暂不具备正式进入C2.2C生产实施的完整前置条件。** 不是因为“必须先实现C2.2C才能批准C2.2C”，而是单份C复用、未提交取消退休及SD-only终结、local STOP边界等可在设计阶段解决的契约仍不闭合。应先修订设计，再由人工决定分步生产授权；实现后的运行证据是退出验收条件，不是要求现在违规运行测试。

本报告标记：PASS仅代表该审核条款设计方向或源码事实通过，不代表运行测试；DESIGN_GAP为文档内需先解决的歧义/缺口；SOURCE_NOT_PROVEN为现状源码或组合证明不支持；IMPLEMENTATION_REQUIRED为清楚的未来机制尚未实现；NOT_APPLICABLE为该场景不适用某特定分支。没有任何本轮运行PASS；未测试、编译或改生产，两个总体缺口仍NOT_PROVEN。

## 2. 五份逐项审核

| 文档 | 结果 | 审核依据 |
|---|---|---|
| MetadataLifecycle | DESIGN_GAP | U/C/F/last_ack_G单写划分清楚；但:43未提交本地退休与:59通用C/F/消费条件矛盾，且单份C同时承载USB终结证据与SD-only当前持有的复用条件未定 |
| AdmissionProof | DESIGN_GAP | 四线性化点区分正确，G再检查与已绑定排空有意义；local STOP跨任务/异步命令窗口、C复用和负向终结载体未闭合，P1–P5仅条件推导 |
| MediaReadOnlyIdentityAPI | IMPLEMENTATION_REQUIRED | 只读、分原始/SD owner、SD-only角色与旧M缺失不证明历史释放的限制明确；API未实现，G关联依Application全覆盖不变量，非JPEG路径仍未决 |
| ConcurrencyMatrix | DESIGN_GAP | 18项都列身份/owner/转换/拒绝/恢复，明确NOT_PROVEN；C14默认H<=F漏未提交取消；C18缺无USB D情况下独立终结/回收规则；多场景未冻结精确发送失败/复用顺序 |
| ImplementationReadiness | DESIGN_GAP | 未把NULL回调当完成、消息ABI保留、预算区分实测正确；API部分仍为省略号/签名意图，受C复用/冷启动边界等未定影响，220B不能作为批准的最终预算 |

逐份定位使用当前4c3df6e行号：Metadata:15–20、37–61、65；Admission:7–12、18–29、33–43；Media:7–27、31；Matrix:C01–C18；Readiness:17–26、32–48、62。

## 3. 四项重点风险与文档冲突

### R1 双端单写/发布：PASS（设计方向），IMPLEMENTATION_REQUIRED（现状）

U仅USB写；C/F及last_ack_G仅Camera写，Storage slot G只由Camera Offer写，这是合理的字段级单写设计，不存在被明确授权的双写字段。原slot.state由两任务分阶段写，不应错误改称单任务独占所有slot字段。跨Task共享读写需同一短PRIMASK/屏障规则和完整身份快照；队列调用锁外，不能用volatile代替同步。正常CM7单核任务/可屏蔽中断模型可建立完整发布；跨核或不遵守该协议的读取均不在证明范围。

队列成功可能先被接收者观察再写发送端记账，设计不依赖优先级排除，这一点PASS。失败保留原G/H、未发请求取消不回退编号也正确；但具体类型、发布phase值、错误返回和禁止out别名覆盖共享源等仍须精确化，未运行即不可声称部分可见问题已消除。

### R2 退休：DESIGN_GAP — 单份C复用与两种退休终点

**G1（阻塞）C证据生命期冲突。** Metadata:16与Readiness:18只预算一份Camera C；Media:25/C18又要求SD-only内部持有写C。USB真实释放、ACK成功入队后，Bridge held=false，下一轮可无USB请求而SD需求继续；app_uvc_bridge.c:137→SD-only分支:216起可取下一帧。若USB未消费ACK/尚未读C TERMINAL，Camera将C改为SD内部G/H/M，USB的同U.G/H==C.G/H退休条件不再成立，可永久等待；若强行当已退休又丢掉身份完整性。此为拟议新增C的设计反例，不是已实现生产缺陷。

需选择并冻结：保留USB终结证据直到观察USB已退休，期间禁止任何角色覆盖该记录；或另设有界终结receipt/独立内部持有记录。不能只禁止USB新D而允许SD-only改同C；不能让USB退出阻塞永久损害仍活动的Storage。选择会影响RAM和恢复/活性证明，不能在本审核擅自新增字段实现。

**G2（阻塞）未提交取消例外。** Metadata:43允许全send失败且未接受时本地RETIRED；:59却要求Camera同G/H终结、F和本端终结消息消费。未提交H没有C/ACK，不满足通用条件。需明确LOCAL_UNCOMMITTED退休与COMMITTED双端退休两个谓词；不得为了统一而伪造Camera receipt。Matrix C14的旧H<=F只适用于Camera已终结H，不能覆盖未提交H；后者依下一D.H不匹配且无曾入队事实拒绝，不应让USB代写F。

Replay仍需旧**生命周期请求**固定回复/编号边界，但不依赖D正文。不能因D退休清Replay。Frame F是新增设计的独立拒旧边界，必须注明consumer/domain、UINT32_MAX拒绝及cold-reset队列重建范围；普通Stop/Grant换代不能重置。H单调不复用加G比较可防逻辑ABA，指针/槽索引本身不能防ABA。Warm restart、对象Init或F损坏恢复策略仍SOURCE_NOT_PROVEN，不允许自己扩展恢复。

### R3 关闭：DESIGN_GAP / SOURCE_NOT_PROVEN

四点方向兼容：local STOP阻止本地新需求，Camera合法R使G→RELEASING关闭全局新准入，实际GetFrame前复核，已绑定继续排空，再核验冻结。SOURCE_CONFIRMED原Qualifier写点及单Camera owner；原Bridge并未检查G，所有门都属IMPLEMENTATION_REQUIRED。

**G3（阻塞）local STOP表述需修正。** app_storage.c:204 App_StorageStop先Storage_Request(3)再独立短锁accepting=false，Storage_Context:597只排除ISR/屏蔽状态，并不只允许StorageTask。后来Process的pending_start/Storage_StartGranted:688→722可写accepting=true，真正Storage_OnStop:739才取消pending_start。不能只按StorageTask的stopping流程定义唯一L_stop_local。需区分外部请求成功后的临时关闭与命令处理后单调关闭；枚举待授权START/STOP队列顺序。此窗口发生在Camera权威关闭之前未必违反G关闭安全，但不满足文档对local关闭的简单表述。

**G4（阻塞）拒绝请求的终结载体与覆盖规则。** Metadata:47说不同H时可BUSY/负向完成且不覆盖C，但唯一C如何记录该H的终结、是否推进F、是否会与合法U当前H混淆尚未规定。合法单请求生产者通常不产生该冲突；仍需明确这是协议违例拒绝（不为其生成可消费新receipt）还是合法取消完成，不能混在一个宽泛负向终结规则。Camera ACTIVE检查前失败、M查询失败但已取资源等分支也需错误/保留/回收合同，不能统一说“失败留现场”就当实现可闭合。

offer_pending关闭后重试不是绕门，前提是已取得真实Frame且C.G/B/M不可变；未取得Frame的request占位必须在实际GetFrame前再检查。当前Camera SD-only内部取帧、普通USB取帧、Dispatcher→Storage Retain两层准入均需实际接入，任一旧公开Offer旁路不能保留。独立帧/生命周期队列无全局FIFO，通知不作为授权。

### R4 Media快照：PASS（只读语义），SOURCE_NOT_PROVEN（最终退出证明）

快照只复制真实loan身份/owner，设计上不创建/释放/改引用；104B为字段算术，不是ARMCC5结果。原SOURCE：camera_mjpeg_fifo.c:325 GetFrame、:405 ReleaseFrame、:424 ReleaseLease、Mjpeg_ReleaseLoan；USB/SD位并非两个独立块引用数，最后owner才释放帧块。

同Camera任务无让出/无重入链内，loan字段可保持到下一次明确Media操作；PRIMASK快照不提供跨未来操作永久有效的证书。查询后有其他Camera操作、slot复用、Start换session或scratch重入覆盖必须使旧视图失效。需要明确调用点和out别名/静态scratch生命周期，不能靠“读过一次无loan”完成稍后的退出。

快照不带G，完整关联取决于C/slot标签和所有入口覆盖；发现无关联owner必须拒绝。M不在快照只表示当前未持有，不证明曾成功Release。SD-only原始位需内部角色标签，且其C生命周期受G1影响。

**G5 非JPEG范围待决。** camera_capture.c:1164 StreamGetFrame对非JPEG使用CameraStream_GetFrame，Release也分流。当前仅JPEG快照不足以核验该路径；直接拒绝未支持虽保守，但可能改变已支持USB格式的业务行为，必须明确阶段范围或补等价设计，不能顺手回归成“不支持”。该点为DESIGN_GAP，非同意本轮扩展Media实现。

## 4. 18项并发场景逐项审核

以下PASS仅限规则审核，所有新D/F/gate/查询均未运行；整体仍SOURCE_NOT_PROVEN。每行都已对照Matrix原完整内容。

| ID | 结果 | 核对结论及缺口 |
|---|---|---|
| C01 | DESIGN_GAP | 同G/H重试合理，但未提交本地取消须G2例外，不能等不存在的ACK |
| C02 | IMPLEMENTATION_REQUIRED | 超时保留D/资源、Cancel或Return合理；后续错误无强回收 |
| C03 | DESIGN_GAP | 已绑定排空正确；必须区分USB本地STOP、Camera关闭及Storage G3窗口 |
| C04 | DESIGN_GAP | R/Retain同Camera不可重入；可并发本地STOP来源并不限StorageTask，需G3实际调用者/调度说明 |
| C05 | PASS | 已绑定Offer重试是排空，不是新GetFrame；仍需实现身份标签与门 |
| C06 | PASS | ACK满只重试原ACK、不再次Release，资源完成与消息终结分离正确 |
| C07 | DESIGN_GAP | 未消费不退休正确；但需G1确保C不被SD-only覆盖 |
| C08 | IMPLEMENTATION_REQUIRED | 迟到ACK按当前state/B/H拒错方向合理，非法被删除的消息不能假装可恢复 |
| C09 | PASS | last_ack及其G仅重放、不再Release；需冻结覆盖与过旧拒绝规则 |
| C10 | IMPLEMENTATION_REQUIRED | 旧generation与权威G不符拒绝，禁止用当前G补齐 |
| C11 | PASS | 文件错误与源读取结束、帧真实归还分开，文件失败不伪造完成 |
| C12 | PASS | 真Release失败保留owner/待归还，不成功ACK、不推进F |
| C13 | PASS | Stop失败不清D/Media/资格，原失败结果保持；具体恢复沿原策略 |
| C14 | DESIGN_GAP | 已Camera终结H可用F；未提交退休H未必<=F，须分支区分 |
| C15 | IMPLEMENTATION_REQUIRED | H/B单调及不回绕可防旧ACK命中新交接，不能只靠slot地址；需新D门 |
| C16 | DESIGN_GAP | 已识别ACK消费先于发送后记账，但同C跨角色复用仍未防止，见G1 |
| C17 | PASS | Storage错误owner由App层拒绝，不能错说Media自带同样owner校验 |
| C18 | DESIGN_GAP | 角色分离方向对；无USB U/H/Return ACK时的内部C终结/复用未定义，不能套USB退休谓词 |

计数：PASS=7，DESIGN_GAP=7，IMPLEMENTATION_REQUIRED=4，共18；它们是设计审核分类，不是测试PASS计数。SOURCE_NOT_PROVEN适用于全部新机制的实现/运行层，不另冒充用例执行。NOT_APPLICABLE仅用于C18“不适用USB Return ACK”、C01未入队“不适用Camera消费ACK”等局部条件，不能用于整项免验。

## 5. 两个NOT_PROVEN的关闭条件

### Grant→Frame Lease

先解决G1/G2/G4和非JPEG范围，冻结G/R/B/M/S映射、每个记录的持有及退休期。然后实际接入所有Frame/Retain入口的G验证和只读M读取；证明每个真实owner都能映射到唯一业务G或明确内部角色，不能存在默认当前G或无标签路径。以真实FIFO、实际Camera归还链采集身份/owner与返回/ACK消费，覆盖USB+SD同帧与SD-only以及新旧代次。新负对照必须能拒绝错误关联。原global HasLoan/Debug/入口返回0均不够。

### 稳定关闭

冻结local STOP与Camera关闭的不同范围及G3路径；所有准备GetFrame/Retain的路径复核权威G，关闭后只允许原持有排空；不存在可重开G或用新G归属旧请求的分支。核验到Qual_Decide同owner无重入，快照未跨资源变更；确认RELEASE匹配当前R及原G、消费后grant才清。测试需覆盖全队列/延迟/抢占窗口及重复/迟到消息，不以实现存在代替运行证明。

## 6. 最小生产变更建议（不实施）

设计缺口关闭且获得单独授权后，最小候选范围为app_uvc_bridge.c/.h（U/C/F/G标签、单写读写函数、D退休门、Camera准入和跨角色证据保留）、app_camera/qualification只读grant访问与资源适配（保留G与R职责及Replay）、app_frame_dispatcher与app_storage内部G参数/slot标签及完整旧入口迁移、camera_mjpeg_fifo.c/.h只读M快照。若选择额外终结receipt需重算RAM；如非JPEG纳入则另审CameraCapture只读身份范围，不自动扩大。

Tasks仅必要调度调用点，原优先级/栈/等待/BOOT/ACK顺序保留；不能把设计建议理解为允许改变Task配置。消息52B/56B保持，Replay实现/规则不动，不建第二套引用数。当前released/idle依旧NULL，绑定它们是未来实施内容，不是本轮已完成。

## 7. 测试与实板建议（本轮未执行）

修订后的D两种退休分支、USB终结未消费时SD-only继续、同C写入隔离、ACK入队前后抢占、send已入队但commit字段滞后、旧D/旧generation/不同H冲突、待授权START与外部Stop请求窗口、真实M与双owner、无帧负向终结、非JPEG政策边界，均需O0/O2独立正反对照。原C1/C1.1/C2.1.1/Q/R2/B1等历史失败和断言保留，不能为新契约改写历史结果。

ARMCC5实际sizeof/offset/Map/RAM/栈与ABI核对，在生产修改获授权之后进行，不将目前字段预算当结果。V1应测真实UVC取消/Abort失败、SD读写错误、双路单独停止、CPU/DMA源访问终止、长运行栈水位；软件快照不能代替真实硬件时序验收。所有建议NOT_RUN。

## 8. C2.2C准入判断

当前：DESIGN_GAP，暂不建议直接批准生产实施。先闭合G1–G5并人工确认，再决定是否授权按风险分步实施，不能在缺契约时一次性接released/idle。正常的IMPLEMENTATION_REQUIRED不应形成循环门槛，但设计反例不能等实施后才决定。两个总体NOT_PROVEN保持；未把设计审核PASS写成运行或最终验收PASS。

## 9. 本轮读证哈希及范围

| 文档 | SHA256 |
|---|---|
| B2B3_SchemeA_MetadataLifecycle.md | 7d590a211109d6a4c63dceeacd06fb085e129ad407ccc37a896e10dbd7e5f6dd |
| B2B3_SchemeA_AdmissionProof.md | c2259872856d68a203e9d47dc456e0af6528e5332629afce21e5bec781be28a9 |
| B2B3_MediaReadOnlyIdentityAPI.md | 303b187f7f7a060d8ffa878e7e1d74227b40d165b5f87047093c3613ba6f9590 |
| B2B3_SchemeA_ConcurrencyMatrix.csv | b447e2878ad04a26826be51579ac882a9abc623853affd0e5b5c282486af35cb |
| B2B3_SchemeA_ImplementationReadiness.md | 03c39dc6fce27525dcc8c20f7f7ffcc9a81792448264612dd4b9d6c548eaeedd |

本次只新增本联合审核报告，不修改被审核五份、原判定、生产/测试/ABI/Replay/任务/M4延时；没有运行测试或编译，没有进入C2.2C或合并main。原output完整保留。完成提交推送后停止，等待人工审核；最终SHA及远端状态在交付消息给出。
