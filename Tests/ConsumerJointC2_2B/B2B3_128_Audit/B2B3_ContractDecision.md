# B2B3 Contract Decision — 待人工决策

基线1124177。本文件仅建议，不构成人工批准，不修改原契约或判定。B2 ID23/31保持CONTRACT_REVIEW_REQUIRED，原BLOCKED/FAIL原样保留。原始来源是Tests/UsbModuleB2/behavior_test.c B2_TestRun中的scenario23/31；Tests/UsbModuleB2/run_tests.py要求baseline/current入口成功且完整trace相等。新协议说明和当前实现不能自行覆盖这项历史验收要求。

## D23：B2/23 STOP START FIFO

原输入：START command10后，依次Push STOP11、START12并推进四轮。旧同步实现可在STOP后重新START；旧Python比较全部诊断/消息/调用轨迹。原C此分支没有逐条ACK/FAULT断言，仅执行并返回0，因此不能从C成功推导旧业务完整语义成立。

当前路径：App_USBHandleCommand STOP登记stopping/stop_command；Task仍推进Cancel→Process→Bridge→Process；App_USBFinishRound等待BridgeIdle后ModeStop，冻结STOP11 ACK。完成USB业务后独立ConsumerEnd提交RELEASE，phase变RELEASE而grant仍有效。START12经App_CameraConsumerBegin受到未清退状态约束，返回BUSY；HandleCommand将结果写ACK及fault。现有原始消息O0/O2见Reconciliation CSV，不能推断每次同一FAULT是否再发送，必须依据fault_reported和实际Task发送轨迹形成完整表。

变化原因：资格事务是异步且尚无C2.2C真实释放闭环，USB本地STOP不等于grant已撤销。跳过ConsumerBegin或清grant允许START会破坏身份和资源边界。BUSY不表示STOP失败，也不表示Camera硬件已停。

建议决策：认可“STOP已完成本地清退、资格释放未完成时，新START返回BUSY并按现有ACK/FAULT规则交付”的有限兼容性差异；保留FIFO取出顺序和原错误。若认可，只新增独立等价契约，旧同步trace失败不改写。若不认可，本轮继续阻塞，另行设计异步排队/等待策略，不能在本证据阶段修改生产代码。建议不把START自动等待、资格重置或伪造RELEASE作为测试修复。

需冻结的独立表：START10资格匹配后业务启动及ACK；STOP11仅在原完成/错误条件发送ACK；START12 ACK BUSY；FAULT是否新发/已报告必须由该请求前后fault/fault_reported证明；所有运输失败重试保持command_id与result，ACK入队不等于对端消费。不得因BUSY允许重复硬件Start、清grant或提前StopCamera。人工决策栏：PENDING。

## D31：B2/31 Fault report reset on START

原输入：START10后，再START11（运行中BUSY），推进；STOP12并推进；再START13并推进。原场景不仅测重启，也测“成功START后故障报告状态复位”。不得把它等同D23或仅核对START13 BUSY。

当前路径：重复START11 BUSY设置fault，原FAULT交付受fault_reported约束；STOP12完成USB本地业务，RELEASE仍在途；START13因ConsumerBegin不能重用资格而BUSY。未成功START时不执行成功分支中的fault清零、fault_reported=false、FPS epoch重建。这是原成功重启目标尚不满足，不是可以删除的目标。

源码依据：CM7/Application/app_usb.c App_USBHandleCommand、App_USBProcess成功分支以及App_USBFinishRound；CM7/Application/app_camera_consumer.c App_CameraConsumerBegin/End；CM7/Application/Tasks/usb_task.c UsbTask_Reply。实际资格与原始消息见33项CSV，不以Debug作为业务权威。

建议拆分验收：本阶段独立验证“资格未释放时START13 BUSY，原fault/已报告状态不因失败START伪复位”；原“真正成功重启后的FAULT复位”保持待验，后续具备合法资格完成条件再测，不能在C2.2B伪造释放通过。若人工同意这一有界拆分，应明确原成功重启子目标仍未关闭，不能整项计PASS。若必须本轮覆盖成功重启，则保持CONTRACT_REVIEW_REQUIRED，不越界C2.2C。

独立表须包含START11 BUSY的ACK和FAULT、STOP12 ACK及ModeStop、START13 BUSY的ACK，以及未重发/重发FAULT的真实原因；不能硬规定每次失败都新发FAULT。负对照应将START13伪改OK、fault清零、fault_reported无依据复位、command_id交换分别判失败。人工决策栏：PENDING。

## 决策签核内容

每项分别记录选择、可接受边界、保留未验目标、审核人/时间和证据SHA。未签核时不得将CONTRACT_CHANGED视为已认可。两项都不要求本轮修改生产；如果业务要求变化需新任务授权。本文件没有修改测试状态、接口、运行时配置或代码。
