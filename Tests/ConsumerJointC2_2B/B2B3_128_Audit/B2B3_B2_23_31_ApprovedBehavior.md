# B2 ID23/31 Approved Behavior Design

人工决策记录：用户本轮明确ID23=A，ID31=B；按此前RestartDecisionPending给出的方案含义落实：ID23接受有界BUSY，ID31拆分当前拒绝与未来合法成功START复位。只批准方向和设计，不授权生产代码，不改旧FAIL/BLOCKED或原断言。

## ID23：有界BUSY专项设计

来源：Tests/UsbModuleB2/behavior_test.c scenario23 START10→STOP11→START12，原Python完整轨迹比较保留。App_USBFinishRound本地STOP后独立ConsumerEnd；RELEASE阶段grant保留时ConsumerBegin拒绝START。

计划输入与步骤：真实Consumer QUERY/ACQUIRE取得grant；通过真实USB/Bridge正常持帧；投递STOP11并延迟Return ACK消费、ModeStop或RELEASE回复；各边界投递START12，记录命令FIFO、ACK/FAULT、grant/phase和完整身份。STOP未完成时命令可尚未被出队，不能要求立即BUSY；允许出队后的BUSY应来自当前真实条件。

期望：STOP ACK对应原真实完成/错误，不能因为START BUSY而改写；START被受理处理而grant未清退时ACK BUSY，FAULT按fault/fault_reported现有规则证明，不能要求每个BUSY都重新发FAULT。RELEASE Stage不等于完成，已入队回复不等于Consumer消费；匹配成功且带释放标志并ConfirmConsumed后才清grant。当前生产released=NULL不能完成此后半段，若仅做核心Ops注入必须标记桩，生产完成分支NOT_RUN/待接口，不伪造grant0。

释放后约束：新START仍须满足Consumer phase等既有门控，包括STOP_IF_IDLE在途；不能只以grant0承诺立即成功。合法新请求使用新generation和更大编号；旧回复/旧请求不改变新尝试。当前拒绝路径不得新增ModeStart，不能停止另一消费者或强制释放帧。

负对照设计：篡改STOP结果/START命令ID或BUSY结果、ACK未消费便End、RELEASE未确认清grant、旧回复匹配新START、遗漏FAULT条件，均需独立判定拒绝。本轮未实现/执行。

## ID31：目标拆分，原成功复位目标保留

来源：scenario31 START10→重复START11→STOP12→START13，原名称Fault report reset on START。

阶段一（当前可设计）：重复START11生成BUSY及既有FAULT交付；STOP12独立完成本地操作；START13在资格仍持有/清退未完时拒绝，失败START不进入成功分支，不清fault或无依据复位fault_reported/FPS。既有已报告FAULT是否重发按源码与原轨迹记录，不机械要求新FAULT。

阶段二（未来合法成功START）：必须先具备真实退出证明、匹配RELEASE确认及后续START允许条件；再通过新generation、合法QUERY/ACQUIRE并使ModeStart成功，验证App_USBProcess成功分支fault=OK、fault_reported=false、FPS起点更新，以及随后新故障能够交付。原成功重启目标仍待执行，不由阶段一BUSY通过替代，不在C2.2B伪造生产退出。

负对照：失败START擅自清fault/复位reported、成功START未复位、新故障永不报告、旧ACK污染新generation，分别建立测试。未来场景若缺真实接口则BLOCKED/NOT_RUN，不记PASS。

## 审批与验收分离

ID23有界BUSY方向已批准；ID31拆分方向已批准。旧严格轨迹历史结果保持；新专项实现/运行及最终验收尚未授权/完成。本设计不批准API实施或C2.2C，不改变Replay边界，不改M4延时。
