# 退出安全 R01–R08

这些是独立 Qualification Core Ops 契约桩测试，使用真实 Tx/Rx/Replay。与 joint_test.c 的真实 Camera 适配器测试分别编译，不能解释为真实 Frame Lease、硬件关闭或实板验收。

| ID | 注入及操作 | 验收断言 | O0/O2 |
|---|---|---|---|
| R01 | released=NULL，USB RELEASE | RELEASING、原grant保留、没有终结回复、无stop请求 | PASS/PASS |
| R02 | released=BUSY | Process返回SYS_OK仍不等于释放；Tx无完成回复 | PASS/PASS |
| R03 | released=IO | grant和真实故障证据等待保留，不生成释放完成 | PASS/PASS |
| R04 | 错误Session、错误Generation RELEASE | INVALID_STATE；两路原资格仍ACTIVE；不调用归还证据 | PASS/PASS |
| R05 | 测试Ops确认USB归还，Storage仍ACTIVE | USB可撤销资格；STOP_IF_IDLE终结BUSY；Storage不变；无stop请求 | PASS/PASS |
| R06 | 从未启动、idle=NULL | STOP_IF_IDLE BUSY；不初始化硬件；无HARDWARE_STOPPED | PASS/PASS |
| R07 | 从未启动、idle=BUSY | 与R06相同；没有伪造停止结果 | PASS/PASS |
| R08 | RELEASE等待期间及完成后重复原文 | 等待期间保留grant；测试证据到达后仅撤销一次；缓存原回复重放且不可变 | PASS/PASS |

8个场景、16次ARMCC5执行全部通过。[原始结果](exit_results.json)。测试中没有修改资格表、Replay或grant；Generation通过公开分配接口产生，事务通过ReserveIds/Stage/Flush/AcceptReply/ConfirmConsumed推进。

`drain=SYS_OK`只在R05/R08的独立核心桩中模拟证据，不进入真实Camera适配器；停止回调永远不返回伪造成功结果。真实适配器的released/idle仍为NULL。

首次夹具误用NextGeneration输入输出别名及GetCompleted等待返回码，已按真实接口契约修正；[首次失败记录](exit_fixture_initial_failures.json)保留，不涉及历史断言。
