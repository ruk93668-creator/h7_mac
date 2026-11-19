# STM32H747 Ethernet E4.1 实施与验证

2026-11-18。开始前实查本地与远端 `codex/ethernet-bringup` 均为 `31339f683d2fad74a61a0d1c85fd828937bdd4be`。按用户授权开始 E4.1，未将 [E3.5 遗留审核](E3_5_FINAL_AUDIT.md) 标为关闭。保留 Keil 用户改动 `MDK-ARM/RTE/_h7_mac_CM7/RTE_Components.h`，构建前后 SHA256 同为 `abdebd6072716dcf8aaf93f331bf00d54567188cb80f7bd54ac5e803427ac1b1`。

采用 [上游稳定版 2.2.1](../../ThirdParty/lwip/UPSTREAM.md)，新增独立 netif、NO_SYS 单循环网络管理、独立 Keil 目标和主机测试。板端 IP 为 `192.168.50.2/24`，网关 `0.0.0.0`，MAC 为 `02:47:47:00:00:01`。不改变生产 MAC/DMA、生产入口、FreeRTOS、USB、Storage、CM4、Camera 或 Application V2。没有 DHCP、TCP/UDP 业务、HTTP 或 MJPEG。

| 门禁 | 结果 | 证据 |
| --- | --- | --- |
| lwIP/netif、ARP、IPv4 ICMP、软件定时器 | 软件 PASS | 真 lwIP＋模拟 MAC：核对 ARP 回复字段、奇数长度和 1514 字节 ICMP 回复的地址、校验和、载荷；NO_SYS 毫秒定时回调。 |
| pbuf 链、边界、异常和所有权 | 软件 PASS | 1514 字节链 TX、1515 拒绝、三槽满/BUSY、完成错误与再提交、驱动提交错误、RX 拒绝输入释放、1522 RX 被消费后丢弃、RX 每轮预算、PBUF_POOL 耗尽后恢复并回到 used=0。 |
| Link 状态与恢复 | 软件 PASS | PHY down、协商 BUSY、Start 失败均不开放 link；成功后收发，掉线时 admin 仍 up 而发送受拒，重连后重测 ARP/ICMP。 |
| 原 Keil CM7 工程全量重编 | 编译 PASS | 0 Error、0 Warning；Code 137998、RO 3114、RW 1396、ZI 113484，生产源码和链接配置未增加 lwIP。 |
| 独立 Keil CM7 E4.1 | 编译 PASS | 0 Error、0 Warning；Code 26798、RO 1110、RW 156、ZI 38388 字节，源码构建期间稳定，链接门禁通过。 |
| 实板 ARP、Ping、MTU 1500、1000 次 | PASS（2026-11-19） | 原始 PCAP 完整重放，板端长测计数已对账。 |
| 实板拔插后通信 | PASS（2026-11-19） | 用户报告插回后恢复；PC 收到 4/4 个回应。拔线及协商过程未取得 Watch 快照。 |

独立固件 SHA256：`23df0d4edb8eb05b80889dc9571e6bba8e1faa941fa9332e1ee95fd8b9997c94`。路径 `output/Ethernet/e4/eth_e4.axf`。证据：同目录 `eth_e4.map`，以及 `output/Ethernet/e4_result.json`、`e4_build.log`、`e4_production_build.log`。这些是本机文件，AXF 哈希不能证明板上已烧录该固件。生成的工程和二进制不提交。

| 链接对象 | 起址 | 大小/说明 |
| --- | ---: | --- |
| E4 RO 执行区 | `0x08000000` | `0x6D04` 字节 |
| 普通 RW/ZI（含栈等） | `0x20000000` | `0x1518` 字节 |
| 缓存测试执行区 | `0x24010000` | `0x5078` 字节，Region 5 |
| 含两个对齐临时帧的 `s_network` | `0x24010040` | 3232 字节 |
| lwIP `ram_heap` | `0x24010D8C` | 8211 字节，含上游对齐与内存管理开销 |
| `memp_memory_PBUF_POOL_base` | `0x24012F6A` | 8451 字节，原始 backing array；lwIP 内部对齐后才分配节点 |
| 原 Ethernet DMA 池 | `0x24078000` | 12544 字节，Region 4，原描述符与 buffer 布局不变 |

生产增量为 0。独立固件与 E3.5 的测试组和功能不同，不将两份总大小之差当作 lwIP 独立开销。拆分复制和执行上下文见 [移植说明](LWIP_PORTING.md)。

## 实板结果（2026-11-19）

PC 测试网口为以太网 3，`192.168.50.1/24`，对端 `192.168.50.2` 解析为 `02:47:47:00:00:01`。启动时用户确认 E4 返回 0、驱动 RUNNING、netif link up。没有独立的 Flash Verify/MPU 寄存器日志；本地 AXF 哈希或运行状态不等同于板上镜像的独立读回。

- 初次 Ping 为 4/4、丢包 0%。MTU 短测为 5/5；重放确认 1 组 ARP、5 组 IPv4 总长 1500 字节的 ICMP，DF 置位，MF 和偏移为 0。
- 长测 `ping -n 1000 -w 1000 192.168.50.2` 发送并收到 1000 帧，丢包 0%，Windows RTT 最大 1 ms。重放匹配 1000 组请求和应答，无重复或载荷差异；另有 35 组 ARP，Npcap 无丢弃。板端 TX 提交与完成均从 12 增至 1047，增量 1035 与 ICMP 加 ARP 回复对齐；RX 交付从 65 增至 1100。起末 pool_used=0、pool_peak=3、pool_errors=0；端口 TX/RX 错误、拒绝、BUSY、无 pbuf 均为 0。CCR=0x70200（DC=1）、memory_valid=1。长测结束截图未展开驱动环及 IRQ，不对这些字段宣称零增量。
- 用户报告重新插线后恢复正常。随后 PC Ping 4/4，丢包 0%，每次 RTT 小于 1 ms；重放确认 1 组 ARP 及 4 组 ICMP 的完整匹配。此结果证明插回后可通信；未保存拔线时 READY、重新协商时刻及 link_ups/link_downs 的实板快照。

原始 PCAP 留在本机测试输出目录；[PCAP 回放摘要和 SHA256](Evidence/E4_1_20261119/pcap_replay.json)、起/中/末 Watch 截图及 Ping 日志已归档。

## 实板 Ping 复验步骤

1. 若还需证明 E3.5 板上镜像身份，**先完成 [E3.5 的 Flash/MPU 读回](E3_5_FINAL_AUDIT.md)，然后才更换固件**。备份 E3.5 固件及证据。人工用独立 E4 工程加载上面的 AXF，启用 Flash Verify 并保存日志、路径及 SHA。调试运行，确保没有断点。
2. 测试 PC 网卡设 `192.168.50.1/24`，网关留空；确认是直连板卡的接口，Wireshark 也选择该接口。接线后记录 `g_e4_result=0`、`g_e4_status.heartbeat` 增长、`memory_valid=1`、CCR.DC=1、`admin_up=1`、`phy.up=1`、协商结果、`driver.state=2` 和 `link_up=1`。仅 PHY Up 不算完成。
3. 在 Windows 管理员终端执行 `arp -d *`；先开始抓包再执行 `ping -n 4 192.168.50.2`。Wireshark 显示过滤器 `arp || (icmp && ip.addr == 192.168.50.2)`，保存 ARP 请求/应答、ICMP Echo Request/Reply、时间戳和原始 PCAP。ARP 的板端对应地址应为 `02:47:47:00:00:01`。
4. 执行 `ping -n 1000 -w 1000 192.168.50.2`，保存发/收/丢包和 RTT。另用 `ping -n 5 -f -l 1472 192.168.50.2` 验证无分片 MTU 1500。记录 `g_e4_status.port`、`driver` 及 `pool_used/pool_peak/pool_errors` 起末值；持续零池错误，静默后池回落，无 DMA/所有权错误才记通过。
5. 拔线时保持 CPU 运行，记录 PHY down、`link_up=0`、`admin_up=1` 和驱动 READY。插回后观察重新协商、驱动 RUNNING、`link_ups` 增加，并再次清 ARP、发 Ping，保存新抓包。若无法恢复，先记录状态和计数，不复位清掉现场。

当前停在 E4.1，等待人工审核；不进入 E4 后续阶段，也不将 E3.5 的遗留审计标为关闭。
