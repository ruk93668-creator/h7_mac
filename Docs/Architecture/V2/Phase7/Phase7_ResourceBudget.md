# Phase 7 资源预算

| ARMCC5 类型 | B |
| --- | ---: |
| CSService（含单 Composition Scratch、8 会话及 4 记录） | 2068 |
| CSExecutionEndpoint | 560 |
| CSExecClient / CSExecServer | 68 / 548 |
| CSAdapter，每逻辑通道 | 108 |
| CSRequest / CSReply | 40 / 48 |
| CSIngress / CSEgress | 68 / 80 |
| CSExecution / CSExecResult | 492 / 28 |
| CSService.scratch 的偏移 | 1096 |
| IpcShared | 320 |

固定核心实例（不含 Adapter）3244 B。五个逻辑入口同时装配时，对象合计 3784 B。若每方向各配一条消息，五通道入口/出口与三个执行槽的 payload 另占 1288 B，总和 5072 B，未计 RTOS 队列元数据、Ops、目录、现有 EMContext 或实体传输栈。当前无活动生产队列。

ARMCC5 对象节 O0 RO 5680 B，O2 RO 4548 B，四个单元模块级 RW/ZI 均为 0。动态内存和 Media 池增加为 0。指令级模拟采样的最低 SP 差值为 O0 1264 B、O2 1248 B，包含测试链上的 Composition、Execution Planner 和间接回调。真实任务栈高水位和 WCET 未验证，不能据此调整生产任务配置。

| 原 Keil Target | Code | RO-data | RW | ZI |
| --- | ---: | ---: | ---: | ---: |
| CM7 | 137690 | 3114 | 1340 | 100932 |
| CM4 | 26350 | 1438 | 936 | 3576 |

在保留用户工程配置的隔离副本完整重编，两核各 0 Error、0 Warning。新 V2 通过独立 ARMCC5 编译/链接，未登记原 Target。原工程 Map/AXF/生成镜像 SHA 记录在构建证据中。
