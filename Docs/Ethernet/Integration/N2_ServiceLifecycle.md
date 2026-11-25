# N2 — Network Service 生命周期

本阶段实现真实 Mailbox START/STOP/RESTART/QUERY，操作只在 NetworkTask 执行。
START 完成表示软件服务已建立，不表示 PHY、DHCP 租约、TCP listener 已就绪。

STOP 关闭新发送，HTTP 归还私有副本，TCP 发送环按连接截止时间排空或真实 abort。
调用 tcp_close 后继续运行 sys_check_timeouts，直到 active/TIME_WAIT/bound/listen PCB 全部退休。
之后才清理 DHCP、ARP、netif，并执行 MAC stop。MAC stop 失败保留实例，返回 FAULT，允许再次 STOP。
lwip_init 全局只执行一次；服务停止不伪造 lwIP 全局卸载。重新启动重新登记 netif，不重复创建任务或邮箱。

MAC 初次初始化失败时保留所有者，显式 recover 先停止 DMA，再重新识别 PHY；不通过清零软件状态替代硬件停止。
该故障恢复接口属于授权范围内的必要 Platform 修改，须纳入最终独立驱动回归。

验证：Evidence/N2/verified-build.json 三正式目标 0 Error/0 Warning。
lifecycle-results.json 包含真实生产 Service/Mailbox/lwIP 的静态、DHCP、HTTP、停止失败重试及显式 RESTART，O0/O2 通过。
硬件 MMIO 被测试夹具替代；这些结果不是实板验证。最终联合包仍需补齐网络负载及 Frame Lease 场景。
历史 draft/prior 构建含枚举初始化警告，保留原结果，不视作 PASS。
