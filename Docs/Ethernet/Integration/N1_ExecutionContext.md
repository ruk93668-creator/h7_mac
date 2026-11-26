# N1 — 唯一网络执行上下文

审查日期：2026-11-26（Asia/Shanghai）。源码冻结：`78580073610ff4cfe06a4b004b1b0d75bffef499`；main：`851c1115fe097f7fd2e0bdcaa5f7b3f8c03feb71`。

**PASS（软件）**。真实NetworkTask已接入ApplicationV3 Registry、Record、Mailbox、Events和BOOT屏障。
没有新增第二套Framework、全局网络Mutex或动态任务。

| 项目 | 实际配置 |
|---|---|
| 任务身份 | NETWORK=5，原有0–4不变 |
| 任务资源 | 优先级2，2048字/8192字节静态栈，8字节对齐 |
| 调度 | 10ms轮询，邮箱/队列事件唤醒；RX沿用有界预算 |
| 邮箱 | 4条值类型命令，成功入队不表示完成 |
| 固定池 | 12消息槽、4 Record信号槽、共16 QueueSlot |
| 其他任务 | Camera/USB/Storage/Control/IPC优先级和栈未改 |

NetworkTask一次绑定eth_execution。正式配置使用ETH_NETWORK_SERVICE，入口拒绝其他任务、ISR以及不可调度上下文。
LWIP_ASSERT_CORE_LOCKED也检查同一拥有者，直接越权进入Raw API按原Error_Handler策略处理。
独立验证保留原单循环配置入口，不能用于绕过正式绑定。

RX、TX reclaim、Link、DHCP、TCP/UDP、HTTP/MJPEG和sys_check_timeouts均由NetworkTask执行。
STOP后仍维护全局cyclic timer，不重新启动netif/MAC。ETH ISR只确认/锁存硬件事件，不运行lwIP；
当前由10ms轮询兜底，不在未核准的ETH IRQ优先级调用内核。

其他任务通过原Service Request/Events交互，不借出PCB/Pbuf/Descriptor。
回调在同一任务同步执行，不递归poll。QUERY走原值类型终结；g_network_status为只读调试镜像。
跨字段一致快照应在暂停CPU后读取，业务方不能直接读取lwIP内部对象。
最终六组Ethernet及170/170生产场景通过；原生队满和未完成请求均按原BUSY语义验证。
