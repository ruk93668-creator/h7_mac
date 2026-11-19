# E5 实板验证

当前所有 E5 实板结果为 **NOT_RUN**。主机模拟 MAC、PC 本机回环和编译不能替代实板。不要自动烧录。先保留在板镜像，打开 `MDK-ARM/h7_mac_ethernet_e5.uvprojx`，人工下载 `output/Ethernet/e5/eth_e5.axf`。Watch 加 `g_e5_result`、`g_e5_status`；确认 heartbeat 增长、CCR.DC=1、memory_valid=1、MAC RUNNING。保留下载校验和固件 SHA。

默认 DHCP Only：PC 与板卡须在同一隔离网且有 DHCP（例如直连由 Windows ICS 提供），在 `main` 入口断点前设置 `g_e5_peer[0..3]` 为 PC 的实际地址。启动后用 Watch 的 current_ip 或租约表找板卡地址。若只测静态地址，WLAN 共享关闭、直连 PC 配 `192.168.50.1/24`，在 main 入口把 `g_e5_mode=0`，板端为 `192.168.50.2/24`；不要把这轮算作 DHCP 并发。TCP PC 监听端口默认 5004，可在 main 停住时同步配置 `g_e5_client_port` 与 PC 参数。设置后去掉断点并持续运行。

先在测试网卡启动 Wireshark，建议显示过滤器 `arp || bootp || udp.port == 5001 || tcp.port == 5003 || tcp.port == 5004`（DHCP 也可用 UDP 67/68 过滤）。确认 PC 防火墙允许该隔离网口接收 TCP 5004、UDP 5002。先跑短会话，PC 开始后默认并行发起 TCP 5003、监听板卡 TCP 5004、绑定 UDP 5002：

```powershell
python -B Tests/Ethernet/e5_peer.py --local-ip 192.168.50.1 --board-ip 192.168.50.2 --seconds 30 --fragment --output output/Ethernet/e5_board/short.json
```

| 验收 | 操作及证据 | 状态 |
| --- | --- | --- |
| DHCP 与并发 | 用已获的动态 IP 替换命令两端地址。保存租约状态、双 TCP OPEN、UDP 与 DHCP 的 2 个 PCB，以及 DORA／续租旁路证据。 | NOT_RUN |
| UDP 单播／广播、两向数据 | 可选 `--broadcast <当前子网广播地址>`，核对 Echo、主动 TX、不同数据长度和 CRC32。下层最大数据报 1472 字节；测试协议其中 16 字节头、最多 1456 字节应用载荷。 | NOT_RUN |
| 两 TCP 角色、分段／合并 | `--fragment --burst 4` 同时验证两个方向；检查 PC 每个连接的已验证 Echo、板端每角色 acked、拆头和多帧共存。 | NOT_RUN |
| 10 分钟并发 | `--seconds 660 --interval .05 --burst 4`；按 PCAP 三条通道实际重叠至少 600 秒核对，结合各通道活动最大间隔、CRC、双向数据、RTT 和吞吐。记录起／中／末 heap、pbuf、PCB、segment、timer 的 used/peak/errors 及驱动错误。 | NOT_RUN |
| 无读／FIN／RST／重复连接 | 压力轮将板端 `g_e5_send_interval_ms` 设置为 2，PC 用 `--seconds 90 --stall-seconds 30` 暂停 TCP 5003 的 Socket 读取。抓包确认是否触发零窗口、窗口重开及后续 ACK，分别保存板端背压计数和峰值。另跑 `--reconnect-every 15`、`--reconnect-every 15 --rst`。TIME_WAIT 状态和最终回收均须记录。 | NOT_RUN |
| 拔线与恢复 | 保持 CPU 运行，记录 link down 时 ready 和连接数回落；插回等 DHCP/IP 后两条 TCP 重连并继续 UDP。若地址变了，按新 IP 重启 PC 工具，保留两份会话资料。 | NOT_RUN |

每次使用全新输出文件、保存原 PCAP、JSON、Watch 和 Flash Verify。不要把强制压力场景的预期丢弃计入正常基线。等待连接收尾后取池快照；TCP TIME_WAIT 由上游定时清理。若连接丢失时出现未确认应用消息，PC JSON 如实记录，不把 TCP 会话中断当作已送达。E5 未进入 HTTP/MJPEG。
