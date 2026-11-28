# H2 DHCP、IP 和 HTTP /health 验收报告

**PASS。** 抓包包含同一轮 DHCP 获取、ARP 请求和应答、HTTP /health 请求及 200 响应。板端 Watch 的 IP 原值 0x2D89A8C0 解码为 192.168.137.45，与 DHCP ACK 相同。此次仅留取启动后板端 IP 原值，网络服务初始状态与首次 MDIO 记录见 H1。

| 检查项 | 结果 |
| --- | --- |
| DHCP DORA | Discover、Offer、Request、ACK 对应帧 24 / 26 / 27 / 29 |
| 事务身份 | 相同 DHCP XID 0xabcd0001，客户端 MAC 02:47:47:00:00:01 |
| 租约 | 192.168.137.1 分配 192.168.137.45，Offer / Request / ACK 一致 |
| ARP | 板端询问 PC，PC 应答，帧 203 / 204 |
| HTTP | PC 192.168.137.1:56118 → 192.168.137.45:80，GET /health 帧 207，HTTP 200 帧 208 |
| 响应体 | 字节 6F 6B 0A，和本机 curl 原始响应相同 |
| 抓包 | 212 帧，原位置与验收归档 SHA-256 一致 |

[PCAPNG](H2_DHCP_HTTP.pcapng) · [H2 结果 JSON](H2_DHCP_HTTP.json) · [DHCP 提取](H2_capture_analysis/dhcp.csv) · [HTTP 提取](H2_capture_analysis/health_tcp.csv) · [ARP 提取](H2_capture_analysis/arp.csv)

## 本轮执行步骤

1. Wireshark 选「以太网 3」开始新一轮抓包。在板端触发网络命令之前启动抓包，才能保留 Discover、Offer、Request 和 ACK 的完整链路。保存为 H2_DHCP_HTTP.pcapng。
2. 验收固件处于 IDLE 时设置 g_n5_cycles=1、g_n5_dwell_ms=600000，最后写 g_n5_command=1。放行 CM7。该固件默认等待验收命令，这一轮保留 10 分钟供 H2 取证。
3. 保留 g_network_status 的 lease_valid、address.state、address.ip.addr、flags、lifecycle、memory_valid、driver.state、driver.first_mdio_fault 和 g_n5_acceptance.phase、first_fault。完整就绪时前三个 flag 位为 1，DHCP 为 BOUND、lease_valid=1，MAC 为 RUNNING，NetworkService 为 ACTIVE。PC 目前为 192.168.137.1，HTTP 请求应发往本轮 DHCP 分给板卡的地址。
4. 在 PowerShell 将 BOARD_IP 换成本轮板卡地址后运行下面一条。保存完整 HTTP 状态和原始响应体。应收到 200 OK，响应体为 ok 加换行。
5. 保存抓包和 HTTP 原始输出。复核同一 DHCP transaction ID 和客户端 MAC 的 DORA（Discover、Offer、Request、ACK），ACK 地址应与 Watch、请求 URL 一致。HTTP GET 和 200 响应应能在抓包中对应。Wireshark 显示过滤器可用 dhcp || arp || tcp.port == 80。
6. 如果阶段异常，保存当前 Watch、PCAPNG、CLI 响应与超时记录，保留首次故障。H2 只做 /health，无需打开 MJPEG 流。

~~~powershell
curl.exe --interface 192.168.137.1 --noproxy "*" --max-time 5 -i "http://BOARD_IP/health"
~~~
