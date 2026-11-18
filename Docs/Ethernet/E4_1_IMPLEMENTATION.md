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
| 实板 ARP、Ping、1000 次、拔插恢复 | NOT_RUN | 未烧录、未运行板卡；需执行下列现场步骤。 |

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

## 实板 Ping 取证（待执行）

1. 若还需证明 E3.5 板上镜像身份，**先完成 [E3.5 的 Flash/MPU 读回](E3_5_FINAL_AUDIT.md)，然后才更换固件**。备份 E3.5 固件及证据。人工用独立 E4 工程加载上面的 AXF，启用 Flash Verify 并保存日志、路径及 SHA。调试运行，确保没有断点。
2. 测试 PC 网卡设 `192.168.50.1/24`，网关留空；确认是直连板卡的接口，Wireshark 也选择该接口。接线后记录 `g_e4_result=0`、`g_e4_status.heartbeat` 增长、`memory_valid=1`、CCR.DC=1、`admin_up=1`、`phy.up=1`、协商结果、`driver.state=2` 和 `link_up=1`。仅 PHY Up 不算完成。
3. 在 Windows 管理员终端执行 `arp -d *`；先开始抓包再执行 `ping -n 4 192.168.50.2`。Wireshark 显示过滤器 `arp || (icmp && ip.addr == 192.168.50.2)`，保存 ARP 请求/应答、ICMP Echo Request/Reply、时间戳和原始 PCAP。ARP 的板端对应地址应为 `02:47:47:00:00:01`。
4. 执行 `ping -n 1000 -w 1000 192.168.50.2`，保存发/收/丢包和 RTT。另用 `ping -n 5 -f -l 1472 192.168.50.2` 验证无分片 MTU 1500。记录 `g_e4_status.port`、`driver` 及 `pool_used/pool_peak/pool_errors` 起末值；持续零池错误，静默后池回落，无 DMA/所有权错误才记通过。
5. 拔线时保持 CPU 运行，记录 PHY down、`link_up=0`、`admin_up=1` 和驱动 READY。插回后观察重新协商、驱动 RUNNING、`link_ups` 增加，并再次清 ARP、发 Ping，保存新抓包。若无法恢复，先记录状态和计数，不复位清掉现场。

当前停在 E4.1，等待人工审核及实板 Ping 验收；实验固件未烧录，不进入 E4 后续阶段。
