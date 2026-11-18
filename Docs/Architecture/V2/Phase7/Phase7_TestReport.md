# Phase 7 测试报告

| 套件 | 本轮结果 |
| --- | --- |
| Control Service ARMCC5 O0 | 26/26 |
| Control Service ARMCC5 O2 | 26/26 |
| 负对照 O0/O2 | 各 4/4 由原断言检出 |
| Phase 1/2/3/4 | 250/250 · 112/112 · 158/158 · 154/154 |
| Phase 4 负对照 | 10/10 检出 |
| Phase 5 / 负对照 | 38/38 · 6/6 检出 |
| Phase 6 / 虚拟 Backend / 负对照 | 48/48 · 6/6 · 6/6 检出 |
| Replay / 52B、56B ABI | 648/648 · 2/2 |
| C1 / C1.1 | 419/419 · 24/24 |

ID0–6 覆盖正常事务、多传输绑定、错 session、出口背压、缓存/连接满。ID7–14 覆盖 revision 和再次验证、依赖/补偿权限、队列满、超时、旧结果。ID15–22 覆盖失败及补偿、首错冻结、取消、tick 回绕、未绑定 profile、重入、活动依赖和撤销授权。ID23–25 覆盖复制队列三个方向、执行端接收拒绝，以及身份编号耗尽。真实 Composition、Resolver、MediaGraph、EM_BuildPlan、EM_Load/Process 参与执行。模块 Endpoint 和物理传输用受控夹具替代。真实跨核 IPC 事务和实板驱动为 NOT_RUN。

早期 ID11 同时使用了过短的总时限和队列背压，触发了实际超时。将普通背压 ID11 使用正常时限，仍由 ID12/14 验证在途、未提交超时。首轮 44/46 证据保留。随后队列夹具把空读 NOT_FOUND 视为故障，保存其原始失败后修正夹具的空轮等待判断。最终业务断言和原阶段断言保持。

运行命令：`python Tests/V2Phase7/run_tests.py`、`python Tests/V2Phase7/run_regression.py`、`python Tests/V2Phase7/run_build.py`。原日志和 Map 在 `output/V2Phase7`，Git 证据在 `Tests/V2Phase7/Evidence`。
