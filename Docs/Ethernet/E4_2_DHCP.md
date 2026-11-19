# E4.2 DHCP 客户端设计

E4.2 沿用 [lwIP 2.2.1 固定源码](../../ThirdParty/lwip/UPSTREAM.md)。`--e42` 启用 UDP、DHCP 和上游默认的 DHCP 地址冲突检测（ACD），保留 `NO_SYS=1`、IPv4 和定时器。UDP 只服务 DHCP，无 UDP/TCP 应用、DNS、IGMP、IPv6、Socket 或 Netconn。原 `--e4` 仍只编译静态 IPv4 目标。

`eth_network_init()` 保持旧静态调用；启用 DHCP 的编译变体可调用 `eth_network_init_address()`。

| 模式 | 起始地址 | 等待超时 | 再次 Link Up |
| --- | --- | --- | --- |
| STATIC | 显式静态配置 | 不运行 DHCP | 保持静态配置 |
| DHCP | 0.0.0.0 | 每次获取周期仅记一次超时，继续上游发现 | 无旧地址，重新发现 |
| DHCP_FALLBACK | 0.0.0.0 | 尚无有效租约时停止 DHCP，释放 PCB/上下文，应用指定静态地址 | 清掉 fallback 后重新尝试 DHCP |

等待时间从 MAC 成功启动、发布 netif link up 开始计时；ACK 之后还须完成 ACD，才算拥有租约。租约由有效变为无效时新计获取窗口。测试入口默认 DHCP Only、30 秒；Fallback 地址和掩码必须显式填写，没有通用局域网默认值。运行中不热切换策略；现场静态地址应由操作者挑选无冲突地址。

**实际调用顺序。** netif 先 admin up。单循环检查 PHY 协商和 MAC/DMA 状态，Start 成功后发布 link up，再调用 `dhcp_start()`。只有 `dhcp_supplied_address()` 且 netif link up 才标记租约有效。上游 DHCP 细/粗定时器、ACD 和 ARP 均由 `sys_check_timeouts()` 驱动。协议栈负责 DORA、NAK、T1、T2、到期及重发，不等待网络 I/O。启动失败只每秒重试一次，上游已经运行时上层不再调用 Start；`starts` 统计适配层调用，不包含 lwIP 到期后的内部重新调用。 `start_result` 保留实际最近一次上层调用返回值。

**掉线。** `eth_mac_service()` 按既有契约停机，适配层先 `netif_set_link_down()`，然后 `dhcp_release_and_stop()` 和 `dhcp_cleanup()`，清除地址及 ARP 表。2.2.1 的 `dhcp_stop()` 也是 Release-and-stop 的包装；当持有租约时上游可能在内部构造 Release，但 linkoutput 已关闭，不提交任何 Release 给 MAC。没有直接改 DHCP 内部状态，也不在掉线时调用 PCB 底层释放接口。上游 `netif_set_link_up()` 自带 `dhcp_network_changed_link_up()` 钩子；本策略已清理旧客户端，钩子没有旧租约可恢复，不额外调用，随后重新 Start。Fallback 只在无有效租约时接管，切换时没有需要 Release 的租约。

**状态。** `DISABLED`、`WAIT_LINK`、`STARTING`、`FALLBACK`、`ERROR` 为适配层生命周期状态；上游 INIT→STARTING，SELECTING/BACKING_OFF→SELECTING，REQUESTING/CHECKING/REBOOTING→REQUESTING，BOUND/RENEWING/REBINDING 逐项映射。保留原始 `lwip_state`；读状态和租约秒数不会写入 lwIP 内部。RENEWING/REBINDING 在租约失效前仍可使用地址。统计字段中的 IP 按 lwIP 网络字节序保存（Keil 按 32 位整数显示时注意字节序）。

**资源。** 只留 1 个 UDP PCB，供 DHCP 的单个共享客户端 PCB。ARP、DHCP fine/coarse 和 ACD 占四个周期定时器，沿用两个空余应用定时槽（共六个）。heap 仍为 8 KiB，DHCP 控制块由上游动态分配并在停用后清理；发送报文由 `PBUF_RAM` 分配，DHCP message 初始容量为 308 字节（240 字节 BOOTP 固定头和 cookie + 68 字节 options；另有 pbuf 元数据和协议 headroom）。RX 继续使用 16×512 字节 PBUF_POOL；没有放大池或修改 DMA。系统定时器的耗尽测试直接探测 memp 返回 NULL，并释放自己的槽，未在满池状态下调用上游会断言的 `sys_timeout()`。实际 ARMCC 内存布局见实施报告。
