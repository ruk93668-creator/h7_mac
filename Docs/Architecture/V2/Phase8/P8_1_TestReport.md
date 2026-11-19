# P8.1 测试报告

专项命令 python Tests/V2Phase8_1/run_tests.py，源码 bdd999b1e8e25e08988a208197ef692b9ae56ff7。ARMCC5 编译链接，Unicorn Cortex-M7 执行。原 V2 和底层纯格式验证器参与链接，未绑定的真实硬件和 Task 入口设为失败陷阱。

| 测试 | 结果 |
| --- | --- |
| V1 四个 Profile、发现模块、缺依赖、重复 ID、坏拓扑、Raw/JPEG 互斥、独立速率、过高拒绝、Raw 进 Storage、JPEG 超限、未知 Profile、Applied、ABI | O0 17/17、O2 17/17 |
| 52B AppMessage / 56B AppLifecycleMessage | 通过 |
| Phase 1 / 2 / 3 / 4 | 250/250、112/112、158/158、154/154，Phase 4 负对照 10/10 |
| Phase 5 / 6 / 7 | 38/38、48/48、52/52，负对照 6/6、6/6、8/8 |
| C1 / C1.1 / C2.1.1 | 419/419、24/24、648/648，C2 ABI 2/2 |

Phase 6 另有 Storage backend 6/6。老阶段回归沿用原断言和编译范围，输出定向到独立目录。output/P8_1/regression/index.json 的八个运行器均退出 0，Phase 7 为 output/P8_1/joint/results.json。本轮早期夹具链接失败和 Profile 未填显式 sink policy 的失败原样保留，最终四个 Profile 均通过原专项断言。

| 实板项目 | V1 对照 / P8.1 |
| --- | --- |
| 断电冷启动 | NOT_RUN / NOT_RUN |
| PA15/U10 1.5 V | NOT_RUN / NOT_RUN |
| I2C1 真实事务与 OV5640 ID 0x5640 | NOT_RUN / NOT_RUN |
| Legacy USB UVC 枚举及实际出图 | NOT_RUN / NOT_RUN |

V1 软件电源 14/14、I2C1 24/24。真实 Grant→Lease 和稳定准入关闭继续 NOT_PROVEN。
