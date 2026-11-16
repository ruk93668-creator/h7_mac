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
