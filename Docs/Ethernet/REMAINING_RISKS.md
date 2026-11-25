# 合并前遗留风险

| 问题 | 状态 | 验收条件 |
| --- | --- | --- |
| 正式 FreeRTOS 中的 lwIP 单所有者、任务请求、回调及错误映射 | BLOCKED | Application V2 审批执行上下文和服务请求契约后验证 |
| Stop、Abort、二次启动及超时后的资源回收 | BLOCKED | 明确 netif、DHCP、TCP、Camera Lease、DMA 的撤销与归属，覆盖故障和重复启停 |
| 正式 Camera 与 Network 跨任务租约和私有缓冲 | BLOCKED | 批准正式消费者、租约与生产内存方案，验证所有权和停机完成条件 |
| E7 DMA FIFO Error 根因 | BLOCKED | 8 字节分段读取已有实板短测，仍需根因证据和持续压力测试 |
| QVGA 20 fps、多帧 ACK 流水线 | FAIL / NOT_RUN | 当前 9.33 fps 数据为短测，多帧流水线待设计与验收 |
| 正式固件中的 Camera、USB、SD 并行实板回归，CCR、MPU 和 IRQ 读回 | NOT_RUN | 正式固件烧录后按既有功能用例和读回步骤验收 |
| RTP/JPEG、RTSP | NOT_RUN | Roadmap，等独立授权和架构审核 |

合并候选当前仅具备编译链接层面的正式接入。网络启用、跨任务协作和资源生命周期仍依赖上述门禁。
