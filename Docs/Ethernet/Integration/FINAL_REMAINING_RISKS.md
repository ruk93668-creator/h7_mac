# 未关闭的软件阻塞

| ID | 等级 | 证据 | 影响与所需工作 |
| --- | --- | --- | --- |
| NET-01 | P1 | app_services.c 服务清单、runtime_event.h 身份集合 | 正式 Network 执行者和所有者门禁尚未实现，不能启动 NO_SYS Raw API |
| NET-02 | P1 | queue_pool.c:7–13 固定配额 | 现有池按六个公共通道、三个服务邮箱和三个 Record 信号配额设计，新增网络前需完整静态资源预算 |
| NET-03 | P1 | eth_network.c:17–18、eth_transport.c:11–31 | 单例没有完整生命周期解除入口，不能证明 Stop 后再次 Start 合法 |
| NET-04 | P1 | eth_http.c 单例、eth_tcp_stop | HTTP/MJPEG 清理与 PCB/TIME_WAIT 全部退休没有正式停止终结 |
| NET-05 | P1 | camera_service.c:103–109、311–323 | Ethernet 帧消息路由与实际归还闭环尚未接入，E7 Provider 不能直接作为正式消费者 |
| NET-06 | P1 | 尚无本轮集成实现 | 联合故障注入、多消费者、内存、栈和队列完整验收未执行 |
| NET-07 | P1 | MPU/CCR 实际读回缺失 | 实板 Cache、DMA 停止与多外设共存仍待验 |
| PERF-01 | P2 | 历史 E7 | 20 FPS 未达标，保留原性能目标和证据 |
| HW-01 | P1 | 历史 E7 | DCMI DMA FIFO Error 根因未关闭，保留 8 字节复制策略与错误检测 |

NET-01–06 是需要继续实施的工程工作，不将它们冒充外部权限或环境阻塞。没有证据表明必须重新设计 ApplicationV3，现有 Runtime 和 Media 协议具备复用基础。
