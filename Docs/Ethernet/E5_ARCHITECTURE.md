# E5 TCP/UDP 架构

基线：`125e30587119fb23ac8d0043f39c8273d48c8abd`（本地和远端实查相同）。E5 独立编译使用 lwIP 2.2.1 NO_SYS：同一循环运行 `eth_network_poll()`、`eth_transport_poll()` 和测试应用，ISR 不调用 lwIP。IP 未就绪、掉线或地址切换时停止应用传输；链路和有效 IPv4 恢复后重新绑定。静态地址和 DHCP 继续由既有网络层拥有。

初始预算：MSS 1460、收发窗口各 2920；每角色各一份 2920 B RX/TX 软件环，最多两个活动角色。6 个 TCP PCB（含关闭/TIME_WAIT余量）、1 个 listener、24 个 segment、2 个 UDP PCB（DHCP+应用）、24×512 RX pbuf、12 KiB heap。Region 5 从 E4.2 的 20,900 B 增加预计不足 30 KiB，保留至少 8 KiB，最终按 MAP 校验。Region 4 非缓存窗口、DMA 副本和驱动不改。

TCP 流与应用协议分离。Server 接受一个连接，Client 可配置目标和重试；每个角色独立环、ACK 计数、活动/截止时间。收到 pbuf 时全部复制或向 lwIP 返回 ERR_MEM 由其保留；应用读取后才归还接收窗口。发送环、lwIP COPY 队列、DMA 完成和对端 ACK 各自计数。链路断开撤销已有应用连接，不跨连接重放字节。正常关闭共用 lwIP FIN/TIME_WAIT 机制。

UDP 由一个绑定到指定 netif 的 PCB 收发；DHCP PCB 不复用。测试工程在传输层之上实现序号、长度、CRC32 的流式 Echo 和主动消息，PC 端使用标准 Socket；固件不编译 Socket 或 Netconn。
