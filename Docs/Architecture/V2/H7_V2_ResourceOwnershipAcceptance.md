# Application V2 R3 — 并发场景与可测试所有权契约

## 1. 使用方式与状态

对应[Resource Ownership Architecture](H7_V2_ResourceOwnershipArchitecture.md)，输入54718ee。本轮全部用例只是设计，统一**NOT_RUN**；未新增测试代码、判定工具或构建产物。预期拒绝是设计结果，不是已执行EXPECTED_REJECT。历史B1 37/39、Consumer 42/44等记录不因此改变，也不把其他分支旧记录当本轮独立验收。

G=consumer/session/generation/ACQUIRE request；R=独立RELEASE request并关联G；H=交接号；B=Bridge epoch/lease/command；M=后端真实token/Loan；S=Storage slot/generation/session；T=终结收据。每个事件记录递增序号、执行任务、完整身份、前后事实、返回值、Get/Retain/Release/Stop调用次数；没有资源的负向终结也要记录M不存在，不能伪造Loan。

成功标准是契约事件及先后关系，不是旧同步轨迹逐项相等。修改前后的历史证据独立保存；首个违例用first_contract_violation定位。所列故障注入仅未来独立软件夹具使用，不修改生产行为。

## 2. 源码锚点

| 编号 | 实际源码/入口 | 本轮确认事实与未来测试边界 |
|---|---|---|
| S1 | CM7/Media/Src/camera_capture.c：CameraStream_Rotate1051、GetFrame1107、ReleaseFrame1184、StreamReleaseFrame1220 | Raw实际单持有；取得/释放临界区，目标替换成功才READY；未来纯快照尚缺 |
| S2 | CM7/Media/Src/camera_mjpeg_fifo.c：GetFrame325、RetainFrame377、ReleaseFrame405、ReleaseLease424、Mjpeg_BlockComplete491、Mjpeg_Publish784、Mjpeg_ReleaseLoan840 | 原始/SD独立位、真实session/id、最后owner块释放；不含G |
| S3 | CM7/Application/app_uvc_bridge.c：CameraProcess137、UsbProcess268、Bridge_CameraFlush432、Bridge_CameraRelease456、Bridge_UsbReturn484 | Camera实际归还后生成ACK；USB匹配消费；没有Camera可用的完整消费收据 |
| S4 | CM7/Application/app_storage.c：Offer237、ProcessReturns334、Storage_OnStop688、Storage_Release988 | RESERVING与Return状态；Camera真实ReleaseLease后FREE；文件和帧完成分离 |
| S5 | CM7/Application/app_camera_qualification.c：Qual_Decide139、Qual_Submit174、Qual_Execute248（RELEASE约328）、QualificationProcess后续 | 保存grant与独立R、DECIDED/BLOCKED；related不直接比对grant_request，不能据字段名宣称完整关联已验证；released=NULL不应成功；现Frame数据面缺G核验 |
| S6 | CM7/Application/app_camera.c：App_CameraBeginRound、Process、FinishRound365、s_qualification_ops619 | 唯一普通停止推进；released/idle=NULL；保护性事实/真实结果匹配保留 |
| S7 | CM7/Service/usb_uvc_payload.c：BuildPacket107、CompletePacket204、Cancel236、AbortPacket261；usb_uvc_class.c：Process366、UVC_DataIn1063、UVC_ReleaseFrame1105 | 逐包读原Frame，token匹配和Abort结果约束持有；不是一次整帧复制 |
| S8 | CM7/BSP/bsp_dcmi.c：BSP_DCMI_ReplaceTarget803及其CT/NDTR/地址复核；CM7/BSP/bsp_sdmmc.c：PrepareWriteInput849、Sdmmc_Quiescent957、Sdmmc_StopIo1054、Sdmmc_Transfer1083 | 后端硬件安全条件需实测，状态位或回调不能代替整条访问证明 |
| S9 | CM7/Application/app_camera_lifecycle.c：App_LifecycleStage172/Flush184；app_camera_lifecycle_replay.c：TxConfirmConsumed164、RxComplete363、Replay_RxEnter59 | 原协议编号/匹配/背压/重入规则只研究不修改，布局和ReplayRetire约束不变 |

S8的BSP函数是源码追踪锚点，不授权Application直接调用BSP；V2只通过Media/端口交接。新API还未编码时，相关正向验证必须等单独授权实施，不能用桩的资源释放替代真实生产证明。

## 3. 并发/异常矩阵（全部NOT_RUN）

| ID | 场景与可控输入/交错 | 应观察的身份、事实和资源 | 预期契约/负对照 | 源码锚点 |
|---|---|---|---|---|
| R3-01 | USB D发布后FrameRequest入队失败，再重试 | 同G/H/wire ticket；无Camera M；D保留 | 重试同身份，不新增票据；负对照提前清D或Queue失败仍Get必须被抓出 | S3/S9+候选D |
| R3-02 | 确定从未入队后本地取消 | LOCAL_UNCOMMITTED，无Camera接收/Loan | 本地退休、不发Camera ACK，H不回退；若曾入队不得走本分支 | S3+候选D |
| R3-03 | 入队成功后Camera抢先消费，USB尚未登记commit结果 | Camera匹配PUBLISHED D；实际Get可能已发生 | 不误判本地取消，不覆盖M；负对照共享phase双写导致倒退 | S1/S3 |
| R3-04 | Camera接受/绑M后USB超时或STOP | G/H/B/M保留，USB仍可能收到Offer | 不清Loan；实际停止访问后Return；超时不得释放 | S1/S2/S3 |
| R3-05 | L_admit在L_close之前 | 同G有实际M或已预留受理记录 | 既有交接排空，最多一次Get/Retain；不重开G | S2/S5 |
| R3-06 | L_close在Camera消费FrameRequest之前 | 原G为RELEASING，队列有旧请求 | 不Get，负向终结原D；不清队列/不挂新G | S3/S5 |
| R3-07 | 真实M已取得，offer_pending因队列满跨越关闭 | D.G/M/B不变，pending仍可Flush | 只发原Offer/后续Return，不新Get；无M的pending只发原负回复 | S3 |
| R3-08 | Storage RESERVING/Retain间发生本地STOP | G/H/S已固定，Retain是否成功分开记录 | 成功则RETURN_PENDING并真实归还；未Retain方可释放预留 | S2/S4 |
| R3-09 | RELEASE与Storage新Offer排队交错 | Camera owner顺序、旧G和当前S | close后拒绝新Retain，之前已受理保留；负对照只看accepting=false | S4/S5 |
| R3-10 | Return ACK入队失败连续多轮 | Media已释放，T/B固定，ack_pending | 仅重试消息，不重复Release；返回错误仍不可伪装消费 | S3 |
| R3-11 | ACK已入队但USB未消费；Camera想核验退出 | Camera held=false而USB仍RETURN，T有效 | 核验未完成，不能退休T或确认G退出 | S3+消费证明 |
| R3-12 | USB ACK延迟，Camera连续SD-only工作 | USB T独立，内部持有无USB G，Storage M独立 | 不覆盖旧T、不永久阻塞Storage、不生成SD-only伪USB ACK | S2/S3/S4 |
| R3-13 | 重复Return，第一次已实际归还 | 同B/G/H，T原结果 | 只重放原ACK，Release调用次数不增加 | S3 |
| R3-14 | 错误consumer/旧session/generation/错误B Return | 当前G/H/M不变 | 拒绝，不能清另一个owner或调用无参Media Release | S2/S3/S5 |
| R3-15 | 退休旧D后迟到FrameRequest；新D已占槽 | 不复用wire ticket、不同G/H | 无匹配当前D则拒绝，不把旧报文挂新grant；故意复用ticket为负对照 | S3+候选D |
| R3-16 | 槽FREE并复用后旧Return/ACK/环项到达 | 新S.generation/G/H，旧S不同 | 拒绝旧项，不改新M/槽；不凭相同slot index归还 | S4 |
| R3-17 | USB退出而Storage仍ACTIVE并持同Loan | 两G，JPEG原始位与sd_owned、同M不同用途 | 仅清USB权利，SD数据不回收，Camera不停；真实Release失败保留原持有 | S2/S3/S5 |
| R3-18 | Storage退出而USB仍ACTIVE并持同Loan | SD M和USB B独立 | 仅清SD权利，USB可继续；Camera不停，文件错误独立 | S2/S4/S5 |
| R3-19 | 两消费者并行退出，最后一个仍RETURN_PENDING；released/idle分别NULL/BUSY/IO | 各自G/R与M，内部持有、未消费T | 无回调或无证明保持不完成；Process返回SYS_OK不是释放成功；不因业务IDLE停机，最后真实条件满足才唯一停止推进 | S2/S4/S6 |
| R3-20 | AVI写入/Finish/Close/Unmount失败但帧已安全归还 | 文件结果与FREE/真实M分开 | 可按批准条件释放资格，保留FS/清理错误，不计正常完成AVI | S4/S5 |
| R3-21 | Media Release/ReleaseLease失败后重试 | 原M与T状态、引用/owner事实 | 失败保留持有，后续真实成功一次；不先生成成功ACK | S1/S2/S3/S4 |
| R3-22 | 后端Release成功，RxComplete api_busy | 真实M已归还，DECIDED/R原窗口/T固定 | 回调度边界检查重入，保留结果；不重做Release、不重新Init；Flush满则原回复重试 | S5/S9 |
| R3-23 | 同H同正文/同H不同正文/不同H冲突 | 当前D/T不可变 | 同请求复用进度；冲突拒绝，不覆盖当前合法身份/不推进Lifecycle水位 | S3/S9+候选D |
| R3-24 | wire ticket/H/Generation/Request ID耗尽 | 当前有效事务/Loan仍存在 | 拒绝新事务不回绕；原事务仍可完成；没有新增N次Replay配额 | S5/S9+候选D |
| R3-25 | Raw READY→Get与IRQ轮换同时发生 | M0/M1/IN_USE/index/sequence、临界区 | 不借DMA目标、不回收IN_USE；旧H不能释放新Raw持有 | S1/S8 |
| R3-26 | JPEG跨块/双span并发解析和消费者退出 | block state/refs、实际M、两owner | 只解析完成内存；最后owner之外不降Loan块引用；内部原始持有不标USB G | S2 |
| R3-27 | UVC尚有pending packet、取消Abort失败 | 原Frame仍可能被后续读取，token/stream_result | 保留Frame/原STOP IO ACK；后台真实成功才Return，不能改旧ACK为OK | S3/S7 |
| R3-28 | 旧UVC完成token晚到、新packet已开始 | 当前token/偏移/原Frame关联 | 错误token不推进偏移/不释放新Frame，不用SOF替代真实完成 | S7 |
| R3-29 | SD原Frame复制未结束或IDMA Abort失败 | CPU读取进度、AXI buffer所有者、原M | 原Frame仍被读则不得Return；仅AXI未释放时保留I/O错误和AXI占用，不混为原M永久持有证明 | S4/S8 |
| R3-30 | Camera Stop失败/DRAINING/保护性停止交错 | stop身份/attempt事实、Media状态、Lease和Bridge条件 | 本轮普通停止不重复保护性操作；Device_Stop只在Media完全停后；无超时强清 | S1/S6/S8 |
| R3-31 | 未来复制发送与两个零拷贝DMA之一完成 | G/H/M及每异步操作token、不再访问证据 | 复制需全部原始读取结束；零拷贝需全部相关DMA结束，TCP接受不是Return ACK | 无Network实现，契约设计 |
| R3-32 | 同时USB/Storage冷申请、重传与旧QUERY/RELEASE | 两路Tx/Rx/Replay完整身份、starts、资格独立 | 走实际Camera owner串行准入、共享Source一次启动；旧回复不得改新G；原Core批量边界记录保留 | S5/S9 |

每项 future fixture 必须驱动真实owner推进和原消息接口，不直接写granted/资格表/Replay私有状态。可控Device/介质桩只能证明软件契约，不能证明真实DMA终止。R3-31无生产Network时只可设计/静态审核，不能宣称执行通过。

## 4. 可测试不变量与轨迹门禁

1. P-IDENTITY：每实际Get/Retain之前存在同G有效准入，之后有唯一H→M/B/S关联；同一个M不同owner权利分开记录，不以多个描述副本增加引用。
2. P-CLOSE：同owner的L_close之后同G没有新Get/Retain；已准入工作允许完成Return。关闭期间再次START不得复活旧G。
3. P-RETURN：RETURN_PENDING/队列入队/文件Close均不能当作后端释放；Media调用失败保留权利，成功后不得因ACK失败再Release。
4. P-CONSUME：ACK_ENQUEUED严格早于匹配ACK_CONSUMED；Camera仅在匹配消费证明之后退休相应T，不从debug/USB_IDLE裸快照推断。
5. P-RETIRE：不复用ticket，D/T/slot复用拒绝所有旧G/H/B/S；LOCAL_UNCOMMITTED不发Camera终结；COMMITTED不能本地强清。
6. P-DMA：原始数据最后一次CPU/DMA访问完成早于Return；所有并行DMA逐一确认；缓存屏障不是完成事件。
7. P-INDEPENDENCE：一G退出不能清另一G资格/owner；最后退出仍要检查内部持有和硬件，不新增全局STOPPED捷径。
8. P-RESULT：原业务/清理/释放/硬件结果独立，首次IO ACK冻结，后台恢复不覆盖；DECIDED/RxComplete失败不重复资源操作。
9. P-BOUNDS：所有D/T/快照容量固定、耗尽背压；无动态内存、无限Receipt或Replay配额，受保护临界区无文件/USB阻塞操作。

负对照至少分别破坏上述一个条件，检查检测器确实FAIL；不能仅删除断言得到“旧轨迹不同”的假通过。未来O0/O2使用同一断言、保存完整ID集合/顺序/退出码/原始事件；历史失败独立保留。

## 5. 实板验收设计（全部NOT_RUN）

| 项目 | 操作及必须观察 | 成功门禁 / 不能用作替代 |
|---|---|---|
| Raw稳定流 | 原160×120 YUY2 10FPS基线；记录固件SHA、帧/秒、DMA地址/状态/栈余量 | 不回收IN_USE、无错误；25目标仅另项实测，不从Graph校验推断 |
| JPEG双路 | USB+Storage，依次单独停一路，注入/观察背压及双span | 另一方数据和所有权保持；真实Release/ACK消费/文件结果分开 |
| SD-only与USB退出 | 延迟USB任务消费ACK，Storage继续；随后恢复USB | T不被覆盖、不持有Frame、不永久阻塞SD；无伪USB内部grant |
| 真正Abort/停止故障 | 在受控硬件条件下记录USB/SD/DCMI真实错误/寄存器和原始访问终止 | 桩PASS不等于硬件PASS；故障未实际触发标NOT_RUN |
| 全停/重启/长期循环 | 记录每轮G/H/B/M/S、Raw/FIFO/队列/收据水位和唯一启动停止次数 | 旧消息不改新会话；资源未归还不得初始化覆盖；票据不回绕 |

每项必须保存板型、固件/源码SHA、构建配置、持续时间、负载、实际帧计数、错误和操作记录。硬件停止成功、Media完全停止、Bridge终结消费、Device完全停止分开填写。当前没有本轮固件、Map或实板结果，不引用用户此前七项通过填入本表。

## 6. R3审核结论与实施前待决项

本报告32项软件/并发设计、9项不变量、5组硬件设计；全部未执行。需人工确认：中立格式类型归Core；D/T精确单写字段及既有非回绕ticket承接/退休边界；USB消费证明的受保护发布方式；Raw/JPEG最小只读查询；完整G/R资源Ops接口及最低容量。API尚未批准，不能自动进入编码。

两项总缺口继续NOT_PROVEN。关闭条件不是“文档齐全”，而是单独授权的实现、适用O0/O2契约及负对照、原有效回归、真实ARMCC5 ABI/Map/栈和板上DMA/停止交错证据共同成立。ADR-01/03/04未获实施批准，不开始Phase1/C2.2C。

## 7. R2-09～13与R3第二轮联合矩阵（输入c52490b，全部NOT_RUN）

原R3-01～32的ID、目标和NOT_RUN不变。以下J01～J08为新增**设计**，不将设计覆盖计入测试执行数，不修改历史测试或断言。

| ID | 输入与场景 | 阶段/预期判定 | 负对照与记录要求 | 状态 |
|---|---|---|---|---|
| J01 | 纯图夹具两同格式Raw sink，Pad显式fan-out且格式Ops合法；后端仍原单IN_USE | Graph可FORMAT_VALIDATED；中立ConsumerUseSet在Resource静态能力预检拒绝多零拷贝持有；不Activate/不Get | 若只看fan-out便放行第二Get必须FAIL；夹具图不是注册生产Network/新消费者 | NOT_RUN |
| J02 | 当前产品Raw→USB+Storage | Storage仅JPEG，在格式阶段拒绝，Resource不执行Get/Retain | 不用此用例冒充J01后端所有权拒绝证据；保留首个GraphIssue/原错误 | NOT_RUN |
| J03 | JPEG P2+P3，同媒体、USB目标10、Storage目标25 | Graph媒体合并且Sink策略独立；资源预检只接受现两类owner；实际G/M及速率需另一阶段证明 | 不从配置25或两条Link推导双路实际FPS/自动Retain；保存三个速率域证据 | NOT_RUN |
| J04 | JPEG Ethernet+Storage未来模板/第三零拷贝需求 | 当前Graph首先UNIMPLEMENTED；独立Resource纯能力夹具也不得借USB/SD位模拟第三owner | 不开启allow_future或注册Network来让测试通过；区分两阶段拒绝 | NOT_RUN |
| J05 | USB T未消费→已消费未观察退休；SD-only复用原始借用记录/旧M槽 | T不持Frame、身份不被覆盖，Storage继续；只有匹配Camera退休cookie后D复用 | 故意用相同Media index代替session/id应FAIL；旧ACK不得清新D | NOT_RUN |
| J06 | Raw/JPEG snapshot在IRQ/归还边界采样，旧G/R稍后请求核验 | 只读查询一致、out失败不变；快照仅当前owner验证动作有效，旧R拒绝 | 校验查询前后owner/refcount/DMA无变化；瞬时无Loan不得当永久退出证据 | NOT_RUN |
| J07 | Storage纯GetCaps/Validate，规范上限、错误格式/长度/span配置；实际坏SOI/指针另送运行端口 | Core接口无Camera私有头；同一规范能力源；内容/指针/slot错误分层检查 | Validate产生Mount/Open/消息/Retain任一调用即FAIL；不能把内容没检查当纯格式测试失败 | NOT_RUN |
| J08 | 上限Catalog/8请求/5Sink策略，Ops否决、错误ID/alias/环、原G已关闭 | 图循环符合第1.21节预算，首次错误out不变；即使图合法，Resource L_close后仍拒旧G新持有 | 故意自动修正尺寸、清T/Replay、旧G重开或重复普通Stop均FAIL；无动态内存/栈扩容 | NOT_RUN |

### 7.1 R3第二轮原场景强化点

- R3-01/02/03：publication完整发布、每次重试先置UNKNOWN、Camera抢先消费、COMMITTED不可回退、明确从未入队取消分开；原非回绕wire ticket消耗不回退。
- R3-10/11/12/15/16：ACK生成、入队、消费发布、Camera核验、retired cookie、D复用逐事件记录；T历史M可已被后端复用，但不能再次Release。
- R3-17/18/19：逐G释放真实owner，slot扩展只G/H不复制引用数；最后一个消费者退出也不能省略内部原始持有/硬件安全。
- R3-22/23/24：冻结业务结果与RxComplete重入错误保持，Flush背压重试原身份；禁止新Replay配额、Reset或清水位。
- R3-25/26/27/28/29：新Raw实例epoch、Raw序号回绕、实际JPEG session/id、逐包原始读取与多DMA完成分别证明；快照不是访问终止凭据。

以上均为DESIGN_PROPOSED/IMPLEMENTATION_REQUIRED；RUNTIME_NOT_PROVEN。不能因为矩阵可写出就宣布任何32项执行PASS。

## 8. R2/R3最终人工复核记录模板

| 对象 | 设计文件定位 | 本轮结论及下一门禁 |
|---|---|---|
| R2-09 Storage依赖 | ModuleContract1.18 / DependencyRules8 | DESIGN_PROPOSED：接口中立化，代码实现待单独授权 |
| R2-10 速率 | ModuleContract1.19 | DESIGN_PROPOSED：三域所有者/测量明确；实测NOT_RUN |
| R2-11 Graph API | ModuleContract1.20 | DESIGN_PROPOSED：ID/类型/合并/纯Ops清单完整；具体API待批准 |
| R2-12 算法 | ModuleContract1.21 | DESIGN_PROPOSED：循环/边探测预算明确；实际实现/WCET NOT_RUN |
| R2-13 资源预算 | ResourceBudget9 / ResourceArchitecture13.6 | DESIGN_PROPOSED：候选布局/估算，不是Map/栈实测 |
| R3-01～05精确契约 | ResourceArchitecture13.1～13.5 | DESIGN_PROPOSED：能力/身份/退休/关闭/快照逐项定义；运行RUNTIME_NOT_PROVEN |
| Grant→Frame Lease | ResourceArchitecture5、13 | NOT_PROVEN保持；需实际G/H/M绑定、负对照和原回归/硬件证据 |
| Stable Admission Closure | ResourceArchitecture6、13.4 | NOT_PROVEN保持；需所有入口真实闸门、未消费终结和消费/退休证明 |

原B2 ID23=A、ID31=B及AC-01～06不变；这里不批准重启或强退资源行为。对旧C1/C2.1.1格式、Replay的语义/容量/编号规则没有新定义。所有ADR仍UNRESOLVED。本轮只文档检查，软件、ARMCC5布局/Map、任务栈/DWT、硬件均NOT_RUN；等待人工最终审核。

## 9. 统一Composition/Control新增32项联合场景（C01～C32）

本表是新增架构验收**设计**，与原R3-01～32和J01～08分别编号。均DESIGN_PROPOSED、NOT_RUN；无生产/测试实现、无判定升级。逻辑传输夹具仅能证明Manager/Resolver语义，不能替代IPC/USB/BLE/网络实际协议或真实DMA。

| ID | 场景 | 应核验的契约/首个违例 | 状态 |
|---|---|---|---|
| C01 | USB单独启用 | 统一Desired→依赖/Graph/Resource→Plan；一G正常授权，不能从控制ACCEPTED直接发帧 | NOT_RUN |
| C02 | Storage单独启用 | 只选所需Camera/Storage依赖，JPEG与SD-only内部原始用途正确，不假造USB G | NOT_RUN |
| C03 | USB+Storage | 同源媒体合并，两G与两owner真实独立；Sink速率独立，Camera不重复启动 | NOT_RUN |
| C04 | USB+Ethernet | 当前未来网络能力缺失/第三owner不支持必须拒绝；未来授权后由描述扩展，不改Supervisor业务分支 | NOT_RUN |
| C05 | USB+Wi-Fi | 当前无实现返回NOT_IMPLEMENTED；未来控制/媒体接口分离，不能借USB owner | NOT_RUN |
| C06 | USB+Storage+Network | Graph能表达也不能越过JPEG第三owner限制，明确Resource拒绝，不伪Retain | NOT_RUN |
| C07 | Raw多消费者 | 同格式纯图合法与原后端单IN_USE拒绝分开，禁止普通refcount补共享 | NOT_RUN |
| C08 | JPEG多消费者 | 原始/SD实际M区分；最后owner回收块，独立退出不影响另一方 | NOT_RUN |
| C09 | 不存在模块 | Registry UNKNOWN_MODULE，无模块Start/Get/文件操作 | NOT_RUN |
| C10 | Required缺失 | 返回缺失cap/requirement/版本/首错，原Applied与有效视图不变 | NOT_RUN |
| C11 | Optional缺失 | 按明确Optional规则记录缺失；不默默丢用户明确请求的Required功能 | NOT_RUN |
| C12 | Provider冲突 | 显式pin/rank优先；同级歧义拒绝，不能按链接顺序碰运气 | NOT_RUN |
| C13 | 依赖循环 | 显式工作stack/Kahn有界检测环，Resolver不调用模块初始化掩盖环 | NOT_RUN |
| C14 | Graph合法Resource拒绝 | 只得到FORMAT_VALIDATED，不Activate/Get/Retain；旧运行配置保留 | NOT_RUN |
| C15 | Resource支持Graph拒绝 | 格式/protocol/Sink冲突首错拒绝，不因资源可用绕过类型 | NOT_RUN |
| C16 | 格式不兼容 | Core/OV/Capture/UVC/Storage纯否决保留；无静默转换/降规格 | NOT_RUN |
| C17 | Source/Sink FPS不同 | 五速率域分别策略/测量；10基线与MJPEG25目标分级，不将目标写实测 | NOT_RUN |
| C18 | M4配置请求 | 逻辑Gateway夹具可验证普通客户端权限；当前IPC控制codec未实现，端到端不得标PASS | NOT_RUN |
| C19 | USB控制请求 | Control Adapter统一入口；当前独立USB Control缺口不得以UVC帧请求替代 | NOT_RUN |
| C20 | BLE/Wi-Fi同时写 | M7单活动写、base_revision仲裁，另一方BUSY或STALE；无Last Writer Wins | NOT_RUN |
| C21 | 旧Configuration Revision | 拒绝STALE_REVISION，返回当前revision，不触及硬件/资源 | NOT_RUN |
| C22 | 重复Transaction | 相同身份/全文返回原进度/结果，不同正文冲突；退休旧请求RESULT_EXPIRED不重执行 | NOT_RUN |
| C23 | 执行中客户端断线 | 已接受Tx身份和Frame权利保留，按明确策略继续/取消，断线不是Release | NOT_RUN |
| C24 | 执行中再次修改 | BUSY，不隐式覆盖；安全取消完成后新revision才能新写 | NOT_RUN |
| C25 | USB Control与UVC并行 | 目标停UVC应保留控制基础依赖；当前Mode_Stop停Device缺口保持，不能用假Composite桩冒充硬件 | NOT_RUN |
| C26 | Wi-Fi控制/视频并行 | 逻辑角色/队列预算与资源分离，未来链路容量单独证明；当前无网络NOT_RUN | NOT_RUN |
| C27 | M7配置失败回滚 | 只补偿实际成功步骤；文件/Frame/卷错误分别记录，硬件恢复须真实应用成功，不能只复制旧config | NOT_RUN |
| C28 | Camera Deactivate与Drain | 先关受影响入口、Return/ACK消费、G核验；未归还不能Cleanup/覆盖缓冲 | NOT_RUN |
| C29 | 关闭与迟到请求 | L_close后旧G新Get/Retain为0，之前真实M继续排空，旧通知不得开门 | NOT_RUN |
| C30 | ACK消费与D/T退休 | 入队/消费发布/Camera退休cookie/D复用逐事件匹配；T不持帧、不覆盖SD-only证据 | NOT_RUN |
| C31 | Future Network退出 | 假设将来真持M必须逐DMA完成/Return；Control连接/TCP关闭不等于退出，现无实现 | NOT_RUN |
| C32 | M4 IPC中断 | 状态age/心跳故障不清M7 grant/D/T；配置权威仍M7，恢复握手与控制协议缺口单列 | NOT_RUN |

记录公共Control Client/Transport/ConfigTx/Revision和实际G/R/H/B/M/S/T的因果关联，不把它们数值相等作为通过条件。Resolver/Graph/Resource纯检查负对照要求硬件/Frame操作次数0；执行用例必须走Module owner公开接口，不直接写私有granted/phase。当前没实现的模块/transport保持NOT_IMPLEMENTED或NOT_RUN设计边界，不能为了执行C04/C18等注册生产Network或偷改IPC ABI。

原32项R3、8项J和32项C共72项设计条目，实际执行0项；数目仅覆盖清单，不是PASS计数。两个NOT_PROVEN保持。未来执行前需先授权API/ABI/生产或测试实现，分别记录软件/桩/实板及O0/O2原始证据，不改历史断言。

## 10. 冻结架构一致性门禁

所有72项仍是NOT_RUN设计。新增静态检查义务：五核心职责/目录/Phase0～10未改；Control Service包含IPC及其他传输，不以M4为唯一总控；Composition候选计算/Registry绑定不记录为切换完成；Execution仅执行计划；Application Modules与Media Resource的业务/物理持有权威不被Manager合并。提前定义Phase7/8接口不等于对应阶段已执行。
