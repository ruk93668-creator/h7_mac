# P8.2 测试报告

实现 182dbb0ba4c06048e312077cbe57174e6e29baf4。新专项命令 python Tests/V2Phase8_2/run_tests.py。与原工程共享真实 ARMCC5 Cortex-M7 FreeRTOS queue.c、tasks.c、port.c、TCB、通知及 App_Events。Unicorn 选定当前 Task，未仿真硬件抢占或实际 SVC/PendSV 排程。物理端口隔离，IPC Task 的 Service 边界使用可控计数；独立 IPC Runtime 回归使用真实原协议源码。

| 项 | O0 | O2 | 判据 |
| --- | --- | --- | --- |
| P806 | PASS | PASS | 实际 BOOT 发布和通知消费，非法任务/ISR/PRIMASK/BASEPRI 不能进入 |
| P807 | PASS | PASS | 九队列全满；非阻塞拒绝后原身份重试；真实通知合并为一个位，FIFO 无丢项 |
| P808 | 2/2 | 2/2 | 三路真实 Endpoint，7 个匹配完成；错身份回复/ACK 和重复 Camera 请求不污染当前事务；ACK 由模块出队 |
| P809 | PASS | PASS | Storage 补偿失败 emulation，首错 -7、恢复错误 -9，EM_BLOCKED；正常补偿单独完成 |
| P810 | PASS | PASS | 原 IpcTask 正常启动并到达真实 100 tick 内核等待入口，无 Control/Execution 工作产生 |

O0/O2 合计 12/12。接入的五个原任务/创建文件另独立编译，10/10。原回归注意区分原始结果和比较门禁。

- Phase 1～7 依次为 250/250、112/112、158/158、154/154、38/38、48/48、52/52。相关负对照为 Phase 4 的 10/10、Phase 5 和 6 各 6/6、Phase 7 的 8/8。Camera 旧请求保护后已复跑 Phase 4 正负回归。
- C1 419/419、C1.1 24/24、C2.1.1 648/648、C2.2A 162/162、R2 74/74、BASEPRI 26/26、C2 ABI 2/2。旧 Camera 业务夹具用隔离的空运输钩子保持原断言，真实 P8.2 运输由新专项独立验证。
- 双核 IPC Runtime 原套件 212/260，48 条原始 FAIL 保留。对 P8.1 的 ZIP 源码快照重新执行的 260 项完整身份、结果和失败行逐项相等，**新增回归 0**。原失败编号为 17、18、19、21、26、32、33、34、38、45、46、48，各四种核/优化组合。不将该原套件计为 260/260。P810 通过的是新任务专项和相对基线无退化的软件门禁。

原始证据在 output/P8_2/test_20261119_134055_526026/results.json、task_optimization/results.json、regression/index.json、ipc_comparison.json、ipc_baseline_complete.log 及各回归日志。沿用原断言；自动测试无法提供实板调度、物理资源或 DMA 证据。
