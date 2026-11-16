# Final Contract Closure — Scheme A

## 0. 基线、授权、证明等级

文档基线b6541af452452548fd1be6a2641be01ea18472ad，生产源码审计基线14bbdcc。后续提交为文档变化，生产代码仍相同；本轮只新增指定四份设计文件，无生产/测试/ABI/Replay/任务设置修改，无测试或构建。M4 //等OLED稳定 与HAL_Delay(500);零改动，output和历史结果保留。

SOURCE_CONFIRMED=已读取的原源码事实；DESIGN_PROPOSED=本统一契约选定的未来规则，待人工批准和实施；SOURCE_NOT_PROVEN=现状未提供完整机制；IMPLEMENTATION_REQUIRED=所选设计需要新增代码；RUNTIME_NOT_PROVEN=没有本轮运行证据。设计级消歧不等于生产/运行缺口关闭。

## 1. 五项最终选择

| 缺口 | 统一选择 | 本阶段是否闭合 |
|---|---|---|
| G1 C跨USB/SD-only覆盖 | 独立单槽Terminal Receipt T；C只管当前原始持帧，T不持Frame；USB待消费不阻塞SD-only服务 | DESIGN_PROPOSED：消除角色/证据寿命冲突；待批准/实施 |
| G2 两种退休 | LOCAL_UNCOMMITTED只本端；COMMITTED必须Camera终结入队+USB匹配消费+本端退休；另有Camera回收T | DESIGN_PROPOSED：明确三类动作，不伪造ACK |
| G3 本地Stop/Start | STOP成功入队与即时关闭同一锁提交；私有stop_epoch使此前START/pending_start失效，保持FIFO处理顺序 | DESIGN_PROPOSED：稳定关闭前置；需授权后实施，原源码尚非此行为 |
| G4 冲突负终结 | 仅合法当前已提交D可有终结receipt；非法/旧/冲突帧请求拒绝且无receipt、不推进F | DESIGN_PROPOSED：不让错误H覆盖合法身份 |
| G5 非JPEG | JPEG FIFO与CameraStream分格式资源适配，使用类型化私有M；保留所有原支持格式 | DESIGN_PROPOSED：明确资源和DMA条件，非JPEG不再默认拒绝退出 |

五项设计歧义在本文选定一套规则，不再留“两方案任选”的执行分叉。人工尚未批准这些具体规则，SOURCE_NOT_PROVEN和RUNTIME_NOT_PROVEN两层均未关闭；不能报告Grant→Lease或Stable Closure生产验收PASS。

## 2. G1：独立Terminal Receipt，不延迟SD-only

原源码app_uvc_bridge.c BridgeCameraProcess:137及SD-only分支、Bridge_CameraRelease:456：释放原始Media后held=false，SD-only可取得后续帧。只留单C无法同时保存USB待消费终结证据与新的内部持有；联合审核指出的问题成立于拟议结构。

**选择独立单槽T。** T={完整G,H,B,类型化M,真实release结果,终结类别,发布phase}，仅Camera写，USB只读。T不含Frame指针/像素、不增加引用、不延长源Frame访问权；它只保留已结束交接的事实。C继续只表当前live handoff（USB或INTERNAL_CAMERA）。Camera维护待执行USB请求的pending_G标签（复用原request_pending/request_id，新增G标签），不能用它覆盖内部C。

T.phase：EMPTY→PREPARED→ENQUEUED→EMPTY。资源真实完成或从未取得时形成PREPARED；成功提交原终结消息后才ENQUEUED并更新F。USB消费可早于Camera发送后记账，但D退休必须等ENQUEUED。T.G/H/B/M一旦PREPARED即不变，仅phase推进。T满时不得覆盖；合法USB最多一个D，旧D未退休就不得新发下一请求，因此一槽足够。Camera用USB单写、持久的retired_H确认旧T已被退休后清空T，才能受理下一D终结；不是清Media。

U新增retired_H（单调、跨D保留）以解决USB退休后马上发布下一D、Camera尚未观察旧U phase的窗口；当前D的consumed_H可重置，但retired_H不能回退。它是原帧交接消费握手，不是Lifecycle Replay水位/配额。H不回绕，普通Stop/新grant不重置。

C在真实Media释放后可移交终结事实给T并结束本次live handoff，不等USB消费。SD-only无USB T/ACK，内部C在本地真实Release完成后结束；SD slot仍由其独立G/M管理。Camera Bridge应将USB控制回复重试与SD-only服务拆为有限工作段：ACK队列满只阻止新USB D/USB终结，不得在函数开头return而永久跳过SD-only，不能因为USB不消费而让Storage依赖它。每轮有限尝试，不忙循环，原任务顺序/优先级保持。失败的内部Media Release仍占用唯一原始loan，此时不能取下一帧，是实际资源未排空，不是T阻塞。

比较：延迟C复用可少一个T，但USB停滞将阻塞SD-only，违背本轮活性目标，故不推荐；独立T增加有界身份事实RAM，取消USB消费对Storage进度的依赖。活性仍以Camera/Storage得到调度、卡与硬件可用为前提，不保证故障硬件无条件前进。

## 3. G2：三种不同回收动作

### LOCAL_UNCOMMITTED

U完整发布但原FrameRequest从未成功入队；所有send均明确失败且Camera无匹配接收记录，同USB任务不可能同时另发该H。USB锁内将D本地退休并推进自身retired_H；不要求T、Camera ACK或F，不造Camera事实；H消耗不回退。结果未知、已入队或Camera已接受，严禁此分支。

### COMMITTED

成功xQueueSend是提交点，提交字段可滞后于接收。Camera按D/G/H核对并记录接受；取得资源则真实绑定/归还，无资源的合法取消/关闭则形成负Offer。二者终结都进入同H的T。原消息成功入队后T ENQUEUED/F推进；USB按现有state/B/command/result条件消费对应终结消息，记录consumed_H，再验证T同G/H且ENQUEUED、无源访问/待发送工作后退休D并推进retired_H。

### T reclaim

Camera观察retired_H覆盖T.H后回收T；若U已变下一D，retired_H仍可确认旧终结。回收不清last_ack及其G，不操作任何Media owner；重复旧Return只由原last_ack重放。清T可在下次Bridge轮次完成，无需新增消息/平行ACK协议。

F仅记录Camera已成功入队终结的H，不覆盖本地未提交取消的H。旧请求由F、D.H/G精确匹配和单调H联合拒绝。D/T复用依身份，不依地址。正常运行不能重Init；冷启动仅在全部生产者/硬件/队列统一重建后清零，运行期未知半复位继续拒绝，不设计自动恢复捷径。Lifecycle Replay一字不改。

## 4. G3：STOP fence与Pending START

源码事实：app_storage.c:204 App_StorageStop先Storage_Request(3)，再另锁关闭accepting；:597允许合法普通任务调用；Process:395先出队一条命令，再ConsumerProcess，再HandleEvent，然后pending_start若GRANTED进入Storage_StartGranted；:722可重新accepting=true，OnStop:739清pending_start。这意味着“授权回复已到达”不能直接推出业务应该启动，外部STOP可能已成功排队。

选择新增私有单调stop_epoch E及每命令的start_epoch标签、pending_start_epoch。**成功STOP入队、E递增及accepting=false在同一原请求锁内提交**；队列满则保留原失败行为，不假装STOP已受理。E只在受保护的请求提交函数写，可能由不同合法Task调用，但唯一写入路径明确；不是另一个资格状态机。普通START/SNAPSHOT排队捕获当前E。

保留FIFO，不删除旧命令。StorageTask处理START时，标签落后当前E表示被后续已成功提交的STOP废止，记录业务结果BUSY/本地取消原因，不调用ConsumerBegin或Mount；pending_start若已发QUERY/ACQUIRE，先完整处理并消费原事务回复，不丢ACK/不取消WAIT_REPLY。授权成功但其epoch已旧时清pending_start，不执行StartGranted，待quiet后按真实流程End。若尚无grant，保留原事务到终结，不强清Tx。

StartGranted前检查E；Mount/准备可被外部Stop竞入，最终accepting=true的原锁内提交必须再次检查同E。E变更则不重新开入口，由StorageTask沿原Stop/清理路径收尾；清理失败保留真实文件/卷错误，不计正常文件，不把取消当录像成功。若原frame已受理，照常完成源访问并真实Return。

STOP命令实际执行仍清pending_start并设置draining/finish等原业务效果；后续START（排在STOP之后，持新E）可处理，但ConsumerBegin若旧grant/RELEASE未完成仍BUSY，不能跳过资格。E到UINT32_MAX后不回绕：拒绝新的START/SNAPSHOT，STOP仍可排队并保持关闭（E饱和不再变，新START已被禁止），不引入Replay配额。

“START ACK已到达”须区分Camera ACQUIRE回复、命令入队成功与本地录像成功。Storage四个API返回入队结果，不新增Storage ACK协议。ACQUIRE回复合法则正常消费，不能因本地取消忽略其grant；业务不再启动，资源安全归还后才RELEASE。

G关闭之前：E保证本地不重开被STOP废止的START，已受理帧排空；G仍可能ACTIVE，不能宣称Camera已关闭。G关闭之后：Camera准入必须拒绝同G新取帧/Retain，Storage本地E不是替代Camera门。新G只能由合法新事务建立。

## 5. G4：精确拒绝/终结表

| 请求类型 | 回复 | 进入T | 推进F | 当前合法请求 | 重试 |
|---|---|---|---|---|---|
| 未入队本地取消 | 无Camera回复 | 否 | 否 | 只退休本D | 若再START用新H，不复用 |
| 已提交当前D合法Cancel，未取Frame | 原负Offer，同H/lease0 | 是，无M | 成功入队时 | 终结本D | 保留原消息重试 |
| 当前合法D在关闭G后到达，未取Frame | 校验D/G/H后负Offer | 是，无M | 同上 | 终结本D | 同上 |
| 已取得Frame的合法Cancel | 继续Offer/Return/ACK | 真实归还后是 | 终结入队时 | 继续排空 | 原身份重试 |
| 正在处理的相同H/相同D重复Request | 不另造回复，不再GetFrame | 不新增 | 不变 | 不覆盖，原路径最终回复 | 等原路径 |
| 重复Return匹配last_ack/B/G | 原ACK重放 | 不新增/不改T | 不变 | 不影响 | 队列满有限重试 |
| 同H不同正文/D | 拒绝，诊断协议错 | 否 | 否 | 完整保留 | 不自动修成合法后继 |
| 错误generation或G不符 | 拒绝 | 否 | 否 | 不影响 | 新合法事务才可再申请 |
| 错误H/不同H冲突，无匹配当前D | 拒绝，不发伪终结 | 否 | 否 | 不覆盖C/T/U | 非法请求不自动重试 |
| 已退休D/旧Request/过旧Return | 拒绝；仅last_ack匹配Return可无资源重放 | 否 | 不变 | 不影响新D | 禁止重新准入 |
| 错误/重复/迟到ACK | USB按state/B/H拒错 | 否 | 否 | 不清新lease | 正确当前消息仍可消费 |

不同H的非法请求不占终结槽，修正上一版“BUSY/负向完成”的宽泛表述。匹配的已提交D若暂遇内部原始持有，保存原request_pending/H及pending_G，等内部C归还后再复核G再取Frame；不覆盖C，亦不能静默丢弃合法请求使其永久等待。

## 6. G5：非JPEG适配，不降级既有能力

Common/Inc/camera_config.h列Y8、RGB565、YUV422、JPEG；Device/ov5640_device.c分别映射传感器格式。camera_capture.c:1164 JPEG走FIFO，其余走CameraStream_GetFrame:1107；:1184 Release检查IN_USE、in_use_index不是dma_m0/m1，:1020起替换目标排除IN_USE。YUY2是现有YUV422字节布局，不新增像素枚举。

| 格式 | 当前实际能力 | 设计核验适配 |
|---|---|---|
| JPEG/MJPEG | FIFO变长帧；USB/Storage两个owner位 | M_jpeg=session/id，G标签+真实owner快照+实际Release结果 |
| YUY2/YUV422 | CameraStream固定2B/pixel，现有UVC YUY2 | M_stream与IN_USE缓冲、实际DMA目标互斥及UVC源访问终止 |
| RGB565 | CameraStream/传感器固定2B/pixel；当前UVC描述符不支持 | 保留Capture能力，同CameraStream适配，不虚称新UVC格式 |
| Y8 | CameraStream/传感器1B/pixel；当前UVC描述符不支持 | 同上，字节大小及DMA word约束沿原验证 |

usb_uvc_desc.c Validate路径约287行原本仅允许YUV422/JPEG；Storage原本仅JPEG。保留这两个既有功能边界，不将RGB565/Y8新增UVC/录像作为本轮工作，更不将它们从Capture删除。

选择私有类型化资源键M={kind, resource_session, acquisition_id, buffer_index}，16B。JPEG填原loan.session/id，index保留0；CameraStream新增仅身份元数据capture_session、next_acquisition_id。安全StreamStart初始化前递增session，仅原无IN_USE/硬件安全初始化点可换代；每次原GetFrame成功IN_USE提交同锁记录++acquisition_id，原in_use_index即键索引。不用可回绕的next_sequence或地址单独防ABA；编号耗尽拒绝新取得，不回绕，旧持有继续可归还。它不是第二个Loan/引用协议，不改变52B Frame消息或实际缓冲分配。

只读CameraCapture资源适配返回带kind快照：JPEG委托只读FIFO身份查询；CameraStream在原PRIMASK窗口读当前M、buffer state/indices、真实源使用及内部一致性。M来源必须在GetFrame成功后Offer前绑定G/B；Release仍走原StreamReleaseFrame，不按新键偷偷释放任意buffer。Release成功使IN_USE→FREE，记录对应M的完成事实；旧M缺失只说明当前不持有，不能替代历史成功记录。

DCMI继续运行不等于仍访问已IN_USE帧：原所有权不变量保证DMA M0/M1不等于in_use_index；归还前还必须证明UVC或其他消费者异步读已完成/确认Abort。整个Camera停止另外要求真实BSP Stop及Media stopped事实，不能从没有IN_USE推导DMA已停止。Snapshot是独立现有采集模式，不新增消费者帧分发；保留其原模式互斥和停止条件，不把Stream快照用于Snapshot硬件停机证明。

## 7. 设计级证明与边界

A 关联：所有真实取得/Retain均在受控G门后绑定B/M/S，USB当前G取自D并复核Qualifier，SD以同步const G参数传入；pending_G保留等待请求，不挤掉live C；T保留完成归属，原账本是唯一引用真源。DESIGN_PROPOSED；现状SOURCE_NOT_PROVEN。

B 关闭：local E与stopping先关闭需求；合法R使G ACTIVE→RELEASING后所有新取Frame再查ACTIVE，已绑定只排空；只读核验到Qual_Decide同Camera无重入。失败/旧消息不能回ACTIVE。DESIGN_PROPOSED，RUNTIME_NOT_PROVEN。

C USB退出/SD继续：T独立于live C，USB回复队列重试不提前return跳过SD-only；SD标签/owner不受USB撤销影响。D SD退出/USB继续：slot返回只清sd_owned，USB仍持有则块不回收，G_SD关闭不关闭G_USB。E 内部SD-only：C角色INTERNAL_CAMERA绑定触发SD G，取得后先Retain再本地Release；不造USB D/T/ACK，原始侧失败则等待，SD资格核验不能越过未完内部交接。

F JPEG/非JPEG：各M类型由真实源提供，只读查询不清owner、不改ref，实际Release及DMA/CPU源访问终止分开；原格式能力不因查询缺失被移除。G 拒旧：D.H与G、B、M、S各域分别校验，U.retired_H/F不回绕，旧生命周期请求仍受原Replay；不按槽地址重用身份。以上均为DESIGN_PROPOSED、IMPLEMENTATION_REQUIRED和RUNTIME_NOT_PROVEN，不能标运行PASS。

## 8. 闭合判断

G1–G5在统一规范层均有确定选择和失败规则，先前文档冲突由本文件明确替代；不改历史审核记录。是否接受该统一契约仍待人工复审。源码尚无T/D门/epoch标签/非JPEG身份适配，生产两个总体缺口继续SOURCE_NOT_PROVEN，运行继续RUNTIME_NOT_PROVEN。需单独授权才能实施，不能自行进入C2.2C。
