# B2B3 Runtime Evidence Design

本设计覆盖B3 21个运行补证目标，逐项输入及判据见CompletionMatrix与CompletionPlan。全部计划未执行。本轮不新增采集代码，不编译测试，不变更原结果。

## 真实源码与采集边界

必须链接CM7/Media/Src/camera_mjpeg_fifo.c的真实CameraMjpegFifo_RetainFrame/ReleaseLease和Mjpeg_ReleaseLoan/ReleaseBlocks，真实app_storage.c及CameraTask归还执行链。现有ConsumerJointC2_2B/joint_test.c:66/68替换Retain/Release；CameraModuleB1/media_test.c也有FIFO桩；都不能单独作为真实ledger证明。可借用调度/运输基础设施，但最终链接manifest、ELF符号地址/源码哈希必须明确真实实现，出现同名桩应构建门禁拒绝。

可控BSP字节流在生产CameraMjpegFifo_Start注册的BspDcmiByteStreamOps回调提供合法JPEG块/帧边界；生产Process/GetFrame得到实际frame，随后真实Retain。不得直接填loans/references或伪造CameraFrame、Media session、资格grant。初步可行性需验证JPEG解析、跨块帧和外部静态地址在软件模拟器能否正确表示；无法合法形成原输入时停止该项，提出具体边界问题，不放宽断言。

文件系统和硬件依赖可控桩，JPEG/AVI、Storage处理及FIFO账本真实执行。已有三项ACK映射充分项仅索引复用，不能重复建模块；资格协议全走原Tx/Rx/Replay。

## 采集数据格式（仅设计，非新协议）

每事件单调seq，case/optimization/firmware-source SHA、模拟调度轮次、函数入口/返回及配对seq、Task/ISR身份、PRIMASK、操作、返回值、消息正文。Camera消费者身份(session_id/request_id/consumer_id/consumer_generation)与帧身份(stream_epoch/lease_id、Media CameraMjpegLease_t.session/id)分别命名，记录来源，禁止相互赋值替代。

帧记录含block mask、真实loan id及usb_owned/sd_owned、每块references/state、Storage slot index/generation/state、文件正在读取状态及待归还数、Return/ACK与资格事务phase/grant。Debug/App_Status是投影，不能充当资源账本权威。记录器只读Unicorn/ELF内存或测试链接观察；不新增生产Observer/回调、不修改ABI。回调入口和返回快照须配对，不能把入口当成功结果。捕获缓冲溢出、符号缺失、身份关联失败均是BLOCKED或采集失败，不得PASS。

## T0–T8 时序及安全判据

T0：生产GetFrame取得USB frame，验证真实loan。T1：App_StorageOfferFrame真实Retain成功，SD和USB所有权分别记录。T2：Storage所有JPEG/CRC/文件读写访问结束（错误路径也记录）。T3：槽位RETURN_PENDING，只表示归还意图。T4：Camera所有者App_StorageProcessReturns将其转RETURNING并保存generation/lease。T5：真实ReleaseLease返回。T6：同generation且RETURNING时成功转FREE，返回失败必须仍RETURN_PENDING；记录ledger变化。T7：Storage下一Process发布缓存，保持原pending_returns可滞后时点。T8：最后消费者真实归还后再核验可回收状态。

ReleaseLease成功的必要证据是正确task/身份、真实函数返回OK及对应SD ownership撤销，而非仅sd_owned桩变量或FREE。USB仍持有时last=false，Mjpeg_ReleaseLoan不应提前ReleaseBlocks；两路最后归还才释放整帧块引用。块仍被后续帧/解析引用时也不能要求所有块references=0。应核对本loan导致的引用变化以及parsed/state约束，不强写一个全局零引用条件。

错误路径：错误owner在Storage层直接返回，不进入真实ReleaseLease；旧session/id返回原错误且当前loan不受影响；重复归还不得第二次扣引用；失败返回不能FREE；generation/state检查不通过不能将已重用槽位错误回收。返回完成后迟到旧凭据与新frame交错要通过真实公开路径生成，不能改内部generation制造“成功证明”。现阶段不允许新START覆盖仍有loan的账本。

## 交错专项与原21项关系

21项各自保存全部T0–T8可达事件，不是用通用压力用例替代。每项安排USB先归还/SD先归还两个合法调度次序。ID34增加Storage错误owner→Camera正确owner；ID36明确实际归还和缓存发布的分离；ID42重复STOP后确认没有第二次Release。

原ID33失败重试、ID35重复Return、ID30/31旧身份作为伴随专项保留，不改其历史断言；未来另建可控scheduler测试在RETURN_PENDING/RETURNING及新frame交接边界注入迟到消息。只能在合法任务让出/消息边界模拟交错，不由两个线程绕过真实单所有者契约同时调用。若需要模拟不可中断临界区内部交错，标记不合法负对照，而非真实生产路径。

对ID25无效JPEG描述，生产Retain可能拒绝frame描述与loan不符，必须先记录可达性。如无法产生合法持有而原业务验证失败，只能分别保留“无效描述拒绝”单元契约与“合法帧归还”集成契约，并请求审核是否等价，不能断言原ID25已完成真实归还。

## 消费者退出、文件及ACK边界

文件Finish/Close/Unmount与帧归还分别记录；Close/Unmount失败不撤销已发生的真实归还。相反，文件关闭成功不能推出帧已归还。CameraProcessReturns仍由CameraTask执行，不让Storage直接调用Media Release。Return ACK依实际USB Bridge实现采集，Storage无相同ACK消息时不凭空新增协议。观察RELEASE意图时不能宣称QUALIFICATION_RELEASED；production released/idle尚未完成时资格安全阻塞是允许且必须记录的事实。

ACK表示原业务操作按契约完成或失败；ACK入队、对端消费、帧归还和Camera硬件停止四者独立。具体USB重启映射需先获ContractDecision批准。RTOS补证依真实Task Run/Create入口，不修改优先级/等待/栈大小。

## 软件与硬件的证明范围

真实源代码在ARMCC5/可控仿真环境执行，可以证明所建模的串行/事件交错、身份、引用和归还判据，无需物理板才能获得这些软件ledger证据。它不能证明真实DMA并发、Cache/FMC仲裁、SD电气故障、硬件FIFO瞬态或全部抢占行为。此类条件保留HARDWARE_REQUIRED/NOT_RUN，另行V1验收，不把历史七项实板记录当作新场景结果。

后续门禁需校验编译源码及符号、完整ID、无原断言删除、原始证据不可改、O0/O2分别执行、10旧负控制与1正控制分别报告；增加真实归还缺失/自洽错误ACK/RTOS错误等负对照，不再把11项自检写作11项负对照。未实际执行均NOT_RUN。本设计不授权编码、测试或进入C2.2C。
