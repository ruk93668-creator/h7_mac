# 联合软件验收状态

本轮 SOFTWARE_BLOCKED，未形成 N1–N3 实现，不引用 R1 的测试 PASS 作为正式功能集成结果。

| 范围 | 本轮状态 |
| --- | --- |
| ApplicationV3 服务和媒体接口源码阅读 | 已执行，证据见 SOURCE_EVIDENCE.json |
| Network 任务与上下文限制 | NOT_RUN |
| START/STOP/RESTART 与事务回执 | NOT_RUN |
| DHCP、TCP 关闭和资源退休联合故障注入 | NOT_RUN |
| Camera 原帧归还与多消费者组合 | NOT_RUN |
| 正式 CM7 JPEG、Raw、CM4 重构建 | NOT_RUN |
| 九个 Ethernet 独立固件 | NOT_RUN |
| 六组 Ethernet 主机回归 | NOT_RUN |
| 相关 ApplicationV3、Media、USB、Storage、OLED、IPC V2 | NOT_RUN |
| 内存、队列和任务增量 | NOT_VERIFIED，尚无集成实现 |
| 旧 IPC Status | 历史 FAIL 保留，日志仍在 Q11_R1_Evidence |

应复用现有 ARMCC/Unicorn 和真实 lwIP 模拟 MAC 框架，以生产 Service 实现和原有队列接口驱动测试，不能直接写服务状态。每个阶段需要正常、故障、超时、重复身份和资源耗尽证据，验证通过后再进入下一阶段。
