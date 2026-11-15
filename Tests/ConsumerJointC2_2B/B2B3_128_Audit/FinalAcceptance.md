# C2.2B B2/B3 历史门禁审计与验收报告

## 结论

153 项全部具有逐项源码映射和本轮 O0/O2 软件证据。新等价契约 306/306 PASS（7400 条检查），但最终历史门禁是 **34 PASS、119 CONTRACT_CHANGED**，不能声称 153/153 PASS。原 128 BLOCKED：**10 PASS、118 CONTRACT_CHANGED、0 仍缺软件证据的 BLOCKED**。118 项等价契约已验证，契约变更仍需人工审核。

唯一原 FAIL B2/10 的缺陷根因已关闭，修复来自 8a83591；本轮用旧代码负对照重现并验证当前修复。没有新增生产代码修改、没有发现新的生产缺陷。本轮单元范围没有只能由硬件执行的原 128 项断言；产品级硬件风险仍有 9 项 NOT_RUN/HARDWARE_REQUIRED，不能据此声称实板通过。

C2.2B 本轮软件证据闭环完成，**不作无条件完整验收通过结论**：119 项契约映射待审核，历史严格门禁仍非全通过。C2.2C 真实退出尚未实现，不能宣称完整独立关闭；可提交后续设计审查，未经审核不进入 C2.2C 编码。

## 基线、范围及冻结

分支 codex/stage-C2-2B-B2B6-v1-compat；源码基线 177c0e1b3ed3e1d8ed5ba23cec67a85965daf789。开始时本地与远端一致。Freeze.json 保存冻结 SHA 和原报告哈希。RawResults/Frozen 保存原153、128清单、唯一FAIL、原缺陷和报告，历史 output 未清理。

本次仅新增 B2B3_128_Audit 目录，生产 CM7/CM4/Common/Board/MDK-ARM 与基线无差异，原测试文件及历史断言未修改。新增测试源码仅在唯一 output 快照编译，不登记正式 Keil 工程。

## 逐项交付与核验方法

Inventory.csv 与 Records/ 为153项独立记录，包含原ID/名称/断言、源码及测试入口、前置条件、缺证及首个阻塞、软件/实板范围、结论和措施。SourceMapping.md 列出原断言表达式及行号；运行时捕获的每个表达式与原源码核对，原分支/求值条件保留。EvidenceMatrix.md、contracts.json、RawResults/Current 提供全部事件和消息。612 次执行覆盖153×原/当前×O0/O2；当前原 C 入口306次通过，原基线 B3 48/49 的4次故意负对照失败按原 Python 契约保留。

新判定验证授权先于 ModeStart/Mount、完整身份、ACK/FAULT 独立重试、FIFO、Cancel/Process/Bridge/Process 顺序、所有权任务、Debug 投影和状态发布时间、JPEG/CRC/文件字节及哈希、Finish/Close/Unmount、失败后安全保留。不是仅看 C return=0。11 个门禁正负控制全部通过：篡改 ACK/Generation/文件/任务/Debug/FIFO、删除断言/消息/发布记录和持帧停止都会失败，防止判定器假阳性。

旧 Python 完整轨迹相等要求保留，新协议的合法异步事件不再被伪装成旧同步事件。分类与新旧语义细节见 BlockerClassification.md；唯一FAIL根因见 UniqueFail.md。分类完成不等于验收通过。

## 本轮真实回归

| 测试 | 实际结果 |
|---|---|
| C1 | 419/419 PASS |
| C1.1 新增 | 24/24 PASS，原 C1 419 保留 |
| C2.1.1 | 648/648 PASS，40960 压力事务 |
| C2.2A / R2 | 162/162、74/74 PASS |
| 原 B1 / Media | 37/39、6/6；ID9/10 原 FAIL 保留 |
| 原 B1 Integration | 0/11 原 FAIL 保留；未改历史夹具 |
| 原 B2 完整 Python / B3 | 16/102、14/51 原结果保留 |
| 新逐项 B2/B3 因果契约 | O0/O2 共306/306 PASS，7400检查 |
| Consumer / Business | 42/44、62/62；Core ID17 原失败不抹除 |
| 新联合 / 因果检查 | 11/11、11/11（原运行器 O0） |
| 退出安全 | 8项、O0/O2共16执行 PASS |
| ID7 / ID7联合 | 22/22、22/22 PASS |
| B2 C兼容入口 | O0、O2 各102/102；不等同完整旧 Python PASS |
| ID17生产串行入口 | O0/O2 PASS，同 Session、双授权、启动一次、自唤醒4次 |
| ID17 Core 批量冷QUERY | O0/O2 原FAIL保留，独立分类 EXPECTED_REJECT |
| Regression Gate / Power / I2C | 29/29、14/14、24/24 PASS |

RegressionComparison.json 比较14套历史原始 ID 集合、重复、顺序及结果：无缺失、无新增、无重复、顺序一致、结果无变化。原始 FAIL 从未删除或改判成原始 PASS。执行退出码见 ExecutionVerdict.json；预期失败进程的 exit=1 明确保留。

首次 ID17 预检因先前回归写入快照结果而拒绝执行，不计作测试通过；原日志保留，最初复制的旧结果明确禁用。随后在独立干净177快照重跑，exit=0；唯一可信本轮记录 RawResults/ID17Clean/results.json。运行器已把 ID17 放在结果写入之前。夹具、清理结果字段和发布轮次等调试过程的失败/警告记录均保存，不隐藏。

## ARMCC5 正式构建与资源

使用原 Keil Target 的独立源码快照，CM7/CM4 完整 Rebuild 均0 Error、0 Warning；未覆盖用户设置或原 output。完整日志 Evidence/build.log 及 Evidence/build/h7_mac_CM*.log，真实 AXF/MAP/HTM/HEX 路径和 SHA256 见 Evidence/build_build.json、Resources.json。CM7 原 Target 不产 HEX，未强开。二进制留在 output，不提交构建产物。

本轮 ARMCC5 重测28个结构大小/偏移全部等于基线，见 Layout.json/RawResults/layout_commands.json。AppMessage=52B、LifecycleMessage=56B、Consumer=232B、Tx=144B、Rx=332B、Qualification=824B、CameraFrame=24B。CameraFrame_t 本基线实际为24B，不能沿用历史误称52B（52B是 AppMessage）。

| 项目 | CM7 | CM4 |
|---|---:|---:|
| RW | 1376 | 936 |
| ZI | 101536 | 3576 |
| 对177的RW/ZI增量 | 0/0 | 0/0 |

CM7 Camera/USB/Storage 静态栈6144/8192/8192字节；链接器已知调用深度496/540/1416字节，Qualification已知408字节，均未变。静态分析含间接调用未能穷尽，不将已知深度说成栈安全上界；真实运行水位尚需实板。ABI、任务优先级、队列、等待周期未改。生产RAM增量0。

两核 ELF 可加载段哈希均与177基线一致（Resources.json）；AXF全文件因调试路径不同而不等同，Map运行列相等，调试字节CM7+6472、CM4+1220，不计作运行RAM增长。

## 未解决风险与 C2.2C 前置条件

119 项契约变化需人工签核；否则不能将历史严格门禁宣称通过。新测试使用真实应用/协议/资格模块，但 UVC/硬件/文件系统/部分交接边界为可控桩，物理DMA并发、SD掉卡、真实Abort/STOP故障及栈水位需要 HardwareAcceptance.md 实板补证。既有用户七项V1通过保留其历史身份，本轮无新增硬件执行。

生产 released/idle 仍未提供完整真实证明；RELEASING/grant 保留和无伪造完成属于安全保护，不是成功退出。后续需独立核验每消费者真实 Lease/Return/ACK，完成 RELEASE、STOP_IF_IDLE/Shutdown，保持无超时强制回收及原Replay边界。本轮没有实现这些功能，没有修改 main 或合并 PR。

## 复现与审查

先 run_regression.py（独立快照，ID17优先），再 run_probe.py、evaluate.py、test_gate.py、report_inventory.py、final_audit.py、audit_layout.py。旧ID7负对照使用 run_probe.py --old-id7。final_audit.py 要求 HEAD 正是冻结177，提交后的复现请在177的独立快照放入本目录运行，不能重置当前工作区。全部原始命令、退出码和输出在 execution.json、Evidence/、RawResults/ 和 probe_results.json。

完整Diff通过本轮提交查看；最终提交SHA及远端一致性在交付消息报告，不把含自身SHA的报告循环改写。
