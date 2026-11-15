# C2.2B-ID17 最终软件验收

基线：`8a83591b4baa0a850ccdfb269b68d752af5bee81`。工作分支：`codex/stage-C2-2B-B2B6-v1-compat`。日期：2026-11-15。

## 最终判断

1. **ID17不是当前CameraTask串行准入下已复现的生产路径缺陷。** Core同轮批量QUERY仍返回BUSY，作为EXPECTED_REJECT边界保留原FAIL。
2. **生产串行准入O0/O2均通过。** 两条QUERY在第一次处理前同时排队，每轮至多接收一条；两路复用Session 1并最终GRANTED/ACTIVE，Camera启动一次，自唤醒提交4次对应4条请求。
3. **本任务的软件复验、边界分类、构建和资源审计已执行完成；C2.2B全量兼容性验收仍为BLOCKED。** 历史128项完整轨迹/谓词证据缺口未升级为PASS；不能用专项通过代替全面验收。
4. **C2.2C尚未完成。** 真实released/idle仍NULL，资格撤销、STOP_IF_IDLE/Shutdown/Restart真实关闭闭环不在本轮实现；实板全部NOT_RUN。

本轮生产代码修改为0，不改Qualification/Consumer/Replay、CameraTask、USB/Storage业务或底层。新增文件全部位于Tests/ConsumerJointC2_2B/ID17。

## 1. 根因及边界证据

详见[Report.md](Report.md)。Core ID17在同轮先登记双QUERY，首次Session分配受`Qual_OtherWork()`的其他未完成/未可靠回复事务门控，两路均固定BUSY。生产Task已有单条准入及每条自唤醒提交，未将两条冷QUERY同时登记进Core，第一条建立Session后第二条复用。保持该Task边界，不修改Core实现。

| 场景 | O0原始结果 | O2原始结果 | 独立验收分类 |
|---|---|---|---|
| Core ID17批量QUERY | FAIL，return/failure=114 | FAIL，return/failure=114 | EXPECTED_REJECT |
| 既有CameraTask ID5（只读探针复用） | PASS，return=0 | PASS，return=0 | PASS |

Core最终：session=0、starts=0、Consumer phase=[6,6]/result=[BUSY,BUSY]/grant=[0,0]，Qualification=[INACTIVE,INACTIVE]，final_reply_enqueued=[1,1]。Task最终：session=1、starts=1，Consumer phase=[GRANTED,GRANTED]/result=[OK,OK]/grant=[1,1]，Qualification=[ACTIVE,ACTIVE]，final_reply_enqueued=[1,1]。

[results.json](results.json)保存四次独立原始返回、状态及逐轮数据；[ProbeEvidence](ProbeEvidence/task_O0.json.gz)保存完整消息身份/前后状态。Core原consumer_test.c及所有历史断言零修改；只读probe没有将原返回114改成0，EXPECTED_REJECT和原始FAIL是分开的字段。

ID5现有fixture初始化资格并设置其Task ready前置，随后执行真实CameraTask_Process/ReceiveLifecycle、Qualification及受控Tx/Rx/Replay。调度、运输和物理设备/Media仍为桩；自唤醒验证是Post提交计数，不是实板RTOS实际调度计数。无Frame Lease真实释放证明。

## 2. 完整执行与优化等级

请求的13条命令全部在8a83591独立快照中实际执行，[execution.json](execution.json)记录命令、环境、时长、退出码和原始日志。直接Consumer命令保持退出1，其余请求命令退出0；嵌套历史脚本内的FAIL及退出1逐项保存在history/runs.json。没有覆盖工作分支里的旧结果。

| 项目 | O0/O2执行或项目数 | 实际结果 | 退出码/分类 |
|---|---|---|---|
| Consumer直接专项 | 22 / 22 | 42 PASS、2原ID17 FAIL | 1；原suite仍FAIL |
| Business直接专项 | 31 / 31 | 62/62 PASS，含Task ID5两次 | 0 / PASS |
| 原ConsumerJoint run_tests.py | 11 / 0 | 11/11 PASS | 0 / PASS |
| 原check_contracts.py | 11 / 0个因果检查 | 11/11 PASS | 0 / PASS |
| R01–R08 | 8 / 8 | 16/16 PASS（8独立场景） | 0 / PASS |
| ID7 run_id7.py | 11 / 11 | 22/22 PASS | 0 / PASS |
| run_id7_joint.py | 11 / 11 | 22/22 PASS | 0 / PASS |
| B2 C入口 | 102 / 102 | 204/204 PASS | 两命令均0 / PASS |
| 新ID17只读probe | Core+Task各1 / 各1 | 2个原FAIL符合负向边界、2个正向PASS | probe运行器0 |

原check_contracts.py只分析其原run_tests.py的O0事件；没有将O2 C运行冒充该Python因果脚本的O2运行。O2独立因果脚本标记NOT_RUN，O2对应11项C合同检查通过由run_id7_joint.py原始结果单列。所有执行次数与项目口径见[acceptance_results.json](acceptance_results.json)。

Business在直接命令和历史回归中各执行一次，加上只读probe，Task ID5总共执行6次（O0/O2各3次），均PASS。Consumer也执行两次原suite，再加只读probe，Core ID17总共6次原FAIL均保持同样边界；绝不写成Consumer 44/44。

## 3. ID7、B2 ID10及退出保护

ID7专项22/22，原联合22/22；`check_id7.py`复验旧3cf负对照记录未变、候选因果顺序、请求正文唯一及无虚假释放标志。B2 ID10在O0/O2均C入口返回0，Consumer.phase=RELEASE(4)、grant=1；不把这当作资格已经释放。原始3cf负对照仍保留为FAIL证据，未复写。

R01–R08独立Core Ops桩16次通过；真实Camera适配器没有新增released/idle证据。所有本轮完整事件在Evidence/joint_events、id7_events、id7_joint_events以及b2_traces中；原始8a目录中的证据哈希保持不变。

## 4. 历史回归与ID完整性

项目数按原运行器口径，基线/候选对照项不冒充一次物理硬件执行。完整日志、原始结果和子进程退出码见[Evidence/history/runs.json](Evidence/history/runs.json)。

| Suite | 本轮结果 | 原suite状态/退出 |
|---|---|---|
| C1 | 419/419 | PASS / 0（由C1.1重新调用） |
| C1.1新增 | 24/24 | PASS / 0 |
| C2.1.1 | 648/648，含40960次压力事务 | PASS / 0 |
| C2.2A | 162/162 | PASS / 0 |
| R2 | 74/74 | PASS / 0 |
| B1 | 37/39，ID9/10 FAIL保留 | FAIL / 1 |
| B1 Media | 6/6 | PASS / 0 |
| 旧CameraModuleB1 Integration | 0/11保留 | FAIL / 1 |
| 原B2完整Python门禁 | 16/102保留 | FAIL / 1 |
| 原B3完整Python门禁 | 14/51保留 | FAIL / 1 |
| Business | 62/62 | PASS / 0 |
| Consumer | 42/44，ID17 O0/O2保留 | FAIL / 1 |
| Regression Gate单元检查 | 29/29 | PASS / 0 |
| V1 Power / I2C软件专项 | 14/14；24/24 | PASS / 0 |

[regression_id_comparison.json](regression_id_comparison.json)对全部原C用例ID键、执行顺序、缺失、新增、重复及PASS/FAIL变化进行比较：全部ID检查PASS，无变化。这里的ID检查PASS不意味着有历史FAIL的suite通过。原153项完整门禁报告未修改，128项BLOCKED不自动提升为PASS，B1停止时序差异也没有通过恢复重复STOP来消除。

## 5. 正式构建、ABI、Map、RAM、Stack

原Keil工程、ARMCC5、CM7/CM4完整Rebuild，均退出0、0 Error、0 Warning。源版本8a83591，固件内容与ID7已验证候选一致。日志：[CM7](Evidence/build/h7_mac_CM7.log)、[CM4](Evidence/build/h7_mac_CM4.log)。[artifacts.json](artifacts.json)记录每个实际AXF/MAP/HTM/HEX文件绝对路径、大小和SHA256；文件保存在本轮唯一output快照中，不把构建二进制加入Git。CM7原Target不生成HEX，标记NOT_RUN，未开启新选项；CM4四种产物均保存。

编译器实测：[resources.json](resources.json)。Consumer=232B、Tx=144B、Rx=332B、Qualification=824B、LifecycleMessage=56B、AppMessage=52B、CameraFrame=24B。28项大小/偏移均符合现有ABI；例如Tx.pending=28、Rx.replay=40/Rx.reply_pending=160/Rx.candidate=276、Qualification.rx=144/session=808、Lifecycle.identity=20/result=44、AppMessage.command=4/stream_epoch=8/lease=12/result=16/frame=28。ARMCC5的AppMessage enum字节布局没有被当成全uint32消息解码。

| Map项目 | ID7前已验证基线 | 8a83591 / 本轮 | 本轮增量 |
|---|---|---|---|
| CM7 Code | 147714B | 147746B / 147746B | 0B（+32B属于既有ID7） |
| CM7 RW / ZI | 1376 / 101536B | 同左 / 同左 | 0 / 0B |
| CM4 Code | 26302B | 同左 / 同左 | 0B |
| CM4 RW / ZI | 936 / 3576B | 同左 / 同左 | 0 / 0B |
| AppUsbContext | 332B | 332B / 332B | 0B |
| CameraTask / UsbTask / StorageTask栈 | 6144 / 8192 / 8192B | 同左 | 0B |
| 三任务链接器已知深度 | 496 / 540 / 1416B | 同左 | 0B |
| Qualification已知深度 | 408B | 同左 | 0B |

Consumer对象和所有列出的静态栈地址/大小与8a Map一致，项目未登记新增测试文件。间接回调完整最坏栈及硬件高水位仍NOT_RUN/未证明，不能把链接器已知深度当作实板余量。

AXF调试数据随更深的快照路径增大，CM7 Debug列+59636B、CM4 +11128B；这不是运行时RAM增长。新资源审计最初误把Debug列纳入全等断言，初始审计日志保留在Evidence/audit_initial_debug_column_failure.log；现分别核对运行时五列、静态对象、调用深度，并验证两核可装载节SHA256与8a完全相同。没有修改历史测试断言来消除该新审计工具的错误比较。

## 6. 剩余问题、实板与后续

- 本轮ID17生产软件边界已明确；其他调用者若将多条冷QUERY同轮批量送Core，不能套用串行通过结论。
- 历史128项完整谓词/轨迹证据仍BLOCKED；完整C2.2B兼容性门禁尚未签收。
- C2.2C的真实分消费者归还证明、成功资格撤销、硬件停止与重启闭环未实现；没有伪造完成标志。
- 本轮无新硬件运行；此前V1通过记录不作为新固件/新故障场景的独立证明。

[HardwareAcceptance.md](HardwareAcceptance.md)给出10项实板步骤、预期、固件SHA与记录栏：USB单路、Storage单路、双路同时冷启动、双授权、只启动一次、分别停止一路、双业务停止、Abort失败后台恢复及通知/队列。全部NOT_RUN；未来按实际烧录镜像和操作顺序记录，不能通过强制清资格完成测试。

## 7. 复现、Git及交付

在仓库根目录依次执行：

```powershell
python Tests/ConsumerJointC2_2B/ID17/run_acceptance.py
python Tests/ConsumerJointC2_2B/ID17/trace_id17.py
python Tests/ConsumerJointC2_2B/ID17/audit_results.py
```

第一个入口在新快照执行用户要求的13条命令，Consumer原始退出1是预期保留；第二个入口在原Core ID17和Task ID5上增加只读观测；第三个核验完整ID、原FAIL、二进制/资源/ABI及范围。执行日志保留每个实际退出码。

本轮只提交新增ID17测试/观测工具和报告到当前独立分支，保留8a83591全部历史。不改main、不合并PR、不清理原output。最终本地提交SHA、远端SHA及main保持情况在交付消息核对；[manifest.json](manifest.json)固定源码基线和旧记录哈希。
