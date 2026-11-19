# E5 实板验证

静态 IPv4 三通道短测 **PASS**。先保留在板镜像，打开 `MDK-ARM/h7_mac_ethernet_e5.uvprojx`，人工下载 `output/Ethernet/e5/eth_e5.axf`。Watch 加 `g_e5_result`、`g_e5_status`，确认 heartbeat 增长、CCR.DC=1、memory_valid=1、MAC RUNNING。保留下载校验和固件 SHA。

默认 DHCP Only：PC 与板卡须在同一隔离网且有 DHCP（例如直连由 Windows ICS 提供），在 `main` 入口断点前设置 `g_e5_peer[0..3]` 为 PC 的实际地址。启动后用 Watch 的 current_ip 或租约表找板卡地址。若只测静态地址，WLAN 共享关闭、直连 PC 配 `192.168.50.1/24`，在 main 入口把 `g_e5_mode=0`，板端为 `192.168.50.2/24`；不要把这轮算作 DHCP 并发。TCP PC 监听端口默认 5004，可在 main 停住时同步配置 `g_e5_client_port` 与 PC 参数。设置后去掉断点并持续运行。

先在测试网卡启动 Wireshark，建议显示过滤器 `arp || bootp || udp.port == 5001 || tcp.port == 5003 || tcp.port == 5004`（DHCP 也可用 UDP 67/68 过滤）。确认 PC 防火墙允许该隔离网口接收 TCP 5004、UDP 5002。先跑短会话，PC 开始后默认并行发起 TCP 5003、监听板卡 TCP 5004、绑定 UDP 5002：

```powershell
python -B Tests/Ethernet/e5_peer.py --local-ip 192.168.50.1 --board-ip 192.168.50.2 --seconds 30 --fragment --output output/Ethernet/e5_board/short.json
```

| 验收 | 操作及证据 | 状态 |
| --- | --- | --- |
| 静态 IPv4 三通道短测 | 2026-11-19，PC `192.168.50.1/24` 直连板端 `192.168.50.2`。Ping 3/3，30 秒测试中三通道活动窗口重叠 29 秒。每通道 280/280 个应用 Echo 通过校验，丢失或未确认、重复、乱序、无效帧均为 0。PC 收到并回复板端主动请求 90 个。抓包确认两个方向的 TCP 握手。原始证据位于 `output/Ethernet/e5_board/static-three-channel-20261119-182154` 同名 `.json` 与 `.pcapng`。 | PASS |
| DHCP 与并发 | DHCP Only。PCAPNG 验证 DORA、10 次测量窗口内 T1 单播 Request/ACK，以及两条 TCP 和 UDP 并发。 | PASS |
| UDP 单播／广播、两向数据 | 可选 `--broadcast <当前子网广播地址>`，核对 Echo、主动 TX、不同数据长度和 CRC32。下层最大数据报 1472 字节；测试协议其中 16 字节头、最多 1456 字节应用载荷。 | NOT_RUN |
| 两 TCP 角色、分段／合并 | `--fragment --burst 4` 同时验证两个方向；检查 PC 每个连接的已验证 Echo、板端每角色 acked、拆头和多帧共存。 | NOT_RUN |
| 10 分钟并发流量 | 实际控制窗口 620.005 秒，PCAPNG 六个数据方向交集 619.836 秒。两条 TCP 均使用同一条连接。三通道各 3971/3971 个应用 Echo 均通过。资源归还和关闭单独核对。 | PASS |
| 无读／FIN／RST／重复连接 | 压力轮将板端 `g_e5_send_interval_ms` 设置为 2，PC 用 `--seconds 90 --stall-seconds 30` 暂停 TCP 5003 的 Socket 读取。抓包确认是否触发零窗口、窗口重开及后续 ACK，分别保存板端背压计数和峰值。另跑 `--reconnect-every 15`、`--reconnect-every 15 --rst`。TIME_WAIT 状态和最终回收均须记录。 | NOT_RUN |
| 拔线与恢复 | 保持 CPU 运行，记录 link down 时 ready 和连接数回落；插回等 DHCP/IP 后两条 TCP 重连并继续 UDP。若地址变了，按新 IP 重启 PC 工具，保留两份会话资料。 | NOT_RUN |

每次使用全新输出文件、保存原 PCAP、JSON、Watch 和 Flash Verify。不要把强制压力场景的预期丢弃计入正常基线。等待连接收尾后取池快照；TCP TIME_WAIT 由上游定时清理。若连接丢失时出现未确认应用消息，PC JSON 如实记录，不把 TCP 会话中断当作已送达。E5 未进入 HTTP/MJPEG。

本次首次短测中，PC 默认 Python 解释器受 Public 入站阻止规则影响。定向抓包 `static-5004-20261119-181715.pcapng` 记录板端重复 SYN、PC 无 SYN-ACK。切换到已有入站许可的 Python 3.12 后握手通过。PC 工具先结束已接受连接，再等待监听器完全关闭，修复了 Python 3.12 的收尾等待。防火墙规则保留。

## DHCP 与三通道联合长测流程

隔离直连使用 `以太网 3`，PC 地址 `192.168.50.1/24`，板端租约 `192.168.50.2/24`。关闭 ICS，确认没有其他 DHCP Server、默认路由或网桥。Keil 停在 E5 `main` 读取配置之前，设置 `g_e5_mode=1`、`g_e5_reconnect_ms=0`、`g_e5_send_interval_ms=1000`。关闭自动重连使测试连接结束后可检查活动 PCB 归还。

先开始抓包，捕获 DHCP 67/68、TCP 5003/5004 和 UDP 5001。用本机已允许接收测试流量的解释器启动以下两个进程，证据目录必须新建：

```powershell
python -B Tests/Ethernet/e5_lease_peer.py --direct-board-only --seconds 975 --output <证据目录>/lease.jsonl
python -B Tests/Ethernet/e5_peer.py --local-ip 192.168.50.1 --board-ip 192.168.50.2 --wait-concurrent 150 --seconds 620 --drain-seconds 12 --interval .15 --burst 1 --fragment --seed 5601 --output <证据目录>/peer.json
```

看到 `LEASE_READY` 和 `E5_PEER_READY` 后让板卡运行。PC 在两个 TCP 角色接通后才提交三通道请求，单独记录准备阶段的连接失败。租约程序只应答预设板卡 MAC，循环发送短租约续租 ACK。对照 PCAPNG 的 DHCP 选项 51、58、59 核实实际租期、T1 和 T2。以抓包数据帧双向活动窗口的交集验证 600 秒，另外检查消息计数和两条 TCP 的连接状态。

PC 停止新增请求后保留读、回显和接收路径。进入排空阶段时将板端 `g_e5_send_interval_ms` 置零，停止新主动请求。确认两端均停止主动发送后关闭 TCP Socket。查两条连接的 FIN、对端 ACK 和异常 RST。CPU 持续运行至少 125 秒，再记录 `g_e5_status` 中的板端 PCB、segment、pbuf、heap、listener、UDP PCB 和 timer。板端仍在 DHCP 网络上时，监听 PCB、DHCP 与应用 UDP PCB、周期定时器和 DHCP 结构属于持续服务资源。保存快照后在证据目录创建 `lease.stop`，让短租约工具正常写出 JSON 并关闭。

### 第一轮记录（2026-11-19）

**FAIL（人工复位中断）**。`output/Ethernet/e5_board/dhcp-long-20261119-184823` 保存第一轮完整 PCAPNG、PC JSON、DHCP 事件与复核摘要。抓包中的六个数据方向重叠 605.568 秒，已看到完整 DORA 和 10 次 T1 ACK。操作者确认在计划排空前复位。PC 统计三通道未确认请求分别为 122、122、187，没有完整 FIN/ACK 和运行后资源快照。保留在中断前观测到的连续流量作为事实，整轮不计入最终验收通过。

### 第二轮联合长测（2026-11-19）

源码基线 `68e1df73dd61b6d876546b3220596f03972f5ae4`。板卡沿用已烧录的 E5，未重新烧录。实际本地 AXF SHA-256 为 `97f3e13214b57f34811166752a665f91d00adccb01ece88bfc7a4b895b4d083e`，板端 Flash Verify **NOT_RUN**。原始证据在 `output/Ethernet/e5_board/dhcp-long-20261119-190305`，包括 `capture.pcapng`、`peer.json`、`lease.jsonl` 和 `validation.json`。

| 通道 | PC 发出／有效 Echo／未确认 | PC 收到／回复板端主动请求 | CRC 无效／重复／乱序 | RTT 平均／最大 ms | 测量窗口 TCP 重传帧 |
| --- | ---: | ---: | ---: | ---: | ---: |
| PC → 板端 TCP 5003 | 3971 / 3971 / 0 | 633 / 633 | 0 / 0 / 0 | 22.197 / 359 | 6，含 2 次快速重传 |
| 板端 → PC TCP 5004 | 3971 / 3971 / 0 | 626 / 626 | 0 / 0 / 0 | 113.346 / 313 | 1 |
| UDP 5001/5002 | 3971 / 3971 / 0 | 636 / 636 | 0 / 0 / 0 | 0.247 / 16 | 不适用 |

带 CRC 的 PC 请求合计 11,913 个全部收到有效 Echo。PC 在两条 TCP 建立前记录 15 次连接尝试失败。进入活跃测量后两个通道各保持一条连接，没有重连、连接失败或提前 FIN/RST。上述重传是 Wireshark 对实际数据流的判定，应用 Echo 无丢失。按 PC 工具整个会话计时，两方向应用载荷各约 0.081 Mbit/s。峰值吞吐 **NOT_RUN**。

抓包显示 1 次 Discover、1 次 Offer、1 次初始 Request、1 次初始 ACK。活跃窗口内有 10 次 T1 单播 Request/ACK，无 T2 重绑、NAK 或地址变化。实际 ACK 选项 51/58/59 分别为 180/60/120 秒，板端 Watch 也显示 `BOUND`、`lease_valid=1`、180/60/120 秒。数据窗口中六个方向的最大报文间隔为 0.420 秒（取最大后四舍五入），采集端丢包 0。PCAPNG SHA-256 为 `e4a3aef0b56c545c72c0d541a726c2790cd51e8e8794a5c7e7ac3168736fe515`。

第二轮 TCP 5004 的双向 FIN 均被确认。TCP 5003 的整轮关闭结果为 **FAIL**。PC 发出 FIN（帧 51887）并收到 ACK 后，板端一条 272 B 的主动发送与 PC 关闭交错（帧 51891），随后 PC 回 RST（帧 51892）。这发生在 620 秒活跃测量窗口之后。保留该失败，复验有序关闭时先令板端停止新增主动请求，仍保持两角色接收、回显与 TCP 定时器运行。

板端 `CCR=0x00070200`、`memory_valid=1`。以下池峰值来自板端 Watch。取数时已超过 TCP 关闭后 125 秒，短租约工具此后到时退出，板端从 BOUND 进入 REBINDING 并下线应用传输。关闭后按同一租约仍在线的资源基线另行核对。

| 池 | 峰值 | 后续快照占用 | 分配错误 |
| --- | ---: | ---: | ---: |
| PBUF_POOL | 3 | 0 | 0 |
| TCP PCB | 2 | 0 | 0 |
| TCP segment | 5 | 0 | 0 |
| MEM heap | 7536 B | 80 B | 0 |
| TCP listen PCB | 1 | 0 | 0 |
| UDP PCB | 2 | 1 | 0 |
| SYS_TIMEOUT | 5 | 4 | 0 |

MAC/DMA 异常 IRQ、环 TX/RX 错误、TX 中止及所有权错误均为 0。TX 提交与完成均为 24,030。环 TX 满累计 1087，RX Buffer Unavailable IRQ 1635，TX Buffer Unavailable IRQ 21703。这些事件次数不能推导帧丢失数。上述快照、原图和转录值 `board-watch.json` 与 PCAPNG 保存在同一证据目录。

板端三通道各收 3971 个请求、发 3971 个 Echo，CRC 无效、重复、乱序和序号缺口均为 0。板端累计主动请求／收到 Echo 分别为 TCP 5003 的 634/633、TCP 5004 的 626/626、UDP 的 1070/636。TCP 5003 最后的未确认请求发生于 FIN/RST 冲突。UDP 累计计数包含 PC 关闭监听后的板端持续发送，按 `udp-window.json` 与 PCAPNG 拆分后，620 秒窗口内板端主动 UDP 请求 621/621 得到有效 Echo，PC 主动 UDP 请求 3971/3971 得到有效 Echo。

### 关闭复验的首次启动（2026-11-19）

`output/Ethernet/e5_board/dhcp-close-20261119-192934` 已保存。PC 启动了短租约和两条 TCP 的准备阶段，150 秒没有收到板端 DHCP Discover，测试未进入流量阶段，Ping 无应答。操作者随后确认 CPU 仍停在断点。该次关闭和资源复验 **NOT_RUN**，保留作为启动诊断证据。

### 有序关闭复验（2026-11-19）

独立短轮 `output/Ethernet/e5_board/dhcp-close-20261119-193546`。禁用板端新增主动请求，沿用同一 DHCP、TCP 和 UDP 配置。PC 连续发送 30 秒后停发和排空 8 秒。三条通道各 192/192 个有效 Echo，未确认、校验错误、重复、乱序均为 0。TCP 5003 的两组 FIN/ACK 为 2329→2331、2334→2335，TCP 5004 为 2328→2330、2332→2333，抓包无 RST。有序关闭复验 **PASS**。`capture.pcapng` SHA-256 为 `b4947cd9ad05c8752cc795d702ea4a4a6e733b63a36e79d5242d631b63606759`，采集端丢包 0。同目录 `peer.json` 和 `close-validation.json` 保存原始统计和序号复核。

最后一个 FIN/ACK 后保持板端运行至少 125 秒。Watch 读回 `ready=1`、`listening=1`、`connections=0`、`link_lost=0`。PBUF_POOL、TCP PCB、TCP segment 当前占用均为 0，内存池分配错误均为 0。剩余 1 个 listener、2 个 UDP PCB、4 个周期 timer、80 B heap，与 DHCP 和监听服务仍在线相符。PCAPNG 也记录到关连接后的 T1 续租。资源归还 **PASS**。原始 Watch 和 `board-watch.json` 保存在短轮证据目录。

**本次 E5 实板验收 PASS。** DHCP 续租与至少 600 秒三通道并发由第二轮联合长测覆盖。有序关闭和定时器后的资源归还由单独复验覆盖。中断和异常证据均独立保留。E5 PC 工具的并发等待、排空与连续续租回归共 5 项 PASS，原 E4 短租约回归 2 项 PASS。板端 Flash Verify、MPU 寄存器逐区读回、峰值吞吐及表中额外压力项目维持 **NOT_RUN**。
