# 128 项 BLOCKED 的审计分类

基线 `177c0e1b3ed3e1d8ed5ba23cec67a85965daf789`。原始门禁保存在 RawResults/Frozen/153_gate.json，未覆盖。

| 范围 | 原始状态 | 本轮最终状态 | 数量 |
|---|---|---|---:|
| 原有通过 | PASS | PASS | 24 |
| 原有阻塞 | BLOCKED | PASS | 10 |
| 原有阻塞 | BLOCKED | CONTRACT_CHANGED | 118 |
| 唯一失败 b2/10 | FAIL | CONTRACT_CHANGED；缺陷根因已关闭 | 1 |

本轮没有把等价契约通过写成旧严格轨迹门禁通过。全部 153 项的 O0/O2 新契约执行为 306/306 PASS、7400 条检查；原 128 项中的 118 项仍需人工确认契约映射，不是无条件验收通过。软件证据仍缺失的 BLOCKED 为 0；当前发现并需修复的新生产缺陷为 0。

## 分类规则与证据

每项记录见 Inventory.csv 和 Records/；原始断言及生产源码行号、运行入口、实际首个差异见 SourceMapping.md；逐次检查见 contracts.json，原始事件和完整消息见 RawResults/Current/。

- A 基础设施：旧前置条件使用同步 START；新夹具通过真实 Consumer、Tx/Rx/Replay、Qualification 完成授权。UsbTask 夹具原先把首次 START ACK 延迟到下个故障场景，现将取得授权后的 FinishRound/Reply 纳入同一逻辑启动轮次。仅改测试工具，未改历史文件和断言。
- B 缺证：新增只读运行时采样、原始 C 断言执行记录、52B/56B 消息正文、Debug/状态发布时点、文件哈希、归还和调用身份记录。缓存发布直接观察私有权威快照写入，不从 Debug 推导业务事实。
- C 生产缺陷：唯一历史 b2/10 对应已由 8a83591 修复的 ID7。使用 3cf288f 的 app_usb.c 重现 O0/O2 语义失败，并与当前代码对照；本轮没有新增生产修复。
- D 架构契约变化：旧 Python 要求完整同步轨迹逐事件相等；新协议增加 QUERY/ACQUIRE/GRANTED 和异步清退，不能机械相等。119 项保留 CONTRACT_CHANGED，并验证业务事件因果关系、原始业务断言及安全条件，等待人工审核。
- E 实板：本 128 项声明的软件单元目标均可用真实业务模块及可控硬件边界复验，因此其中没有必须靠硬件才能执行的单元断言；产品物理时序与真实故障验证另见 HardwareAcceptance.md，全部本轮 NOT_RUN，不能用单元结果替代。
- F 不适用：本轮没有以“不适用”关闭任何一项。

A/B 为证据补齐过程分类，可以与 D 同时存在；它们不是互斥的最终状态，不能相加成 128。最终计数以 Inventory.csv/summary.json 为准。

## 恢复的 10 项

B2 96–101 按原 Python 的当前入口条件检验；B3 47–50 分别核验私有权威 getter、FPS/文件编号诊断篡改负对照及 ready 私有权威。B3 48/49 原基线必须失败、候选必须通过，这是原门禁的负对照要求，不是掩盖失败。没有降低断言。

## 不能忽略的语义变化

USB START 失败后仍须 ModeStop 后释放资格；后台恢复不能改写原 ACK IO。资格 RELEASE 尚无生产真实证明时 grant 保留。STOP 后新 START 可以因清退未完成被 BUSY 拒绝。Storage START/STOP/SNAPSHOT/RECOVER 的 FIFO 不变，但异步授权中 STOP 可取消尚未开始的录像，不能要求旧同步 Mount 轨迹。状态发布按真实 Process 次数，而不是测试虚构的单轮计数。所有情况均有独立事件检查。
