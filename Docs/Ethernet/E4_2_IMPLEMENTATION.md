# STM32H747 Ethernet E4.2 实施与验证

2026-11-19。开工前实查本地与远端 `codex/ethernet-bringup` 均为 `3a8add11e608ae59cd20d5ad789d8a7f501836f8`；工作区差异仅 Keil 生成文件 `MDK-ARM/RTE/_h7_mac_CM7/RTE_Components.h`。该文件构建前后 SHA256 一致：`fdf6fa5523eff808cd572fd711f7cd2bfb2ad1c1a6f1b0b182330ddbbebb5413`，未提交该修改。

E4.2 实现静态、DHCP Only 和显式 Static Fallback。使用同一 lwIP 2.2.1 发布归档，仅增加字节一致的 UDP、DHCP 与 ACD 文件，保留许可证。见 [来源](../../ThirdParty/lwip/UPSTREAM.md) 和 [DHCP 生命周期](E4_2_DHCP.md)。生产 CM7、MAC/DMA、MPU 配置、FreeRTOS、USB、Storage、Camera、CM4 OLED 与 Application V2 未改。

## 软件验证

在 x64 Visual Studio Developer Command Prompt 运行 `python -B Tests/Ethernet/run_e4_host.py --dhcp`，并用相同脚本不带参数复跑 E4.1。真实 lwIP 和适配层只替换 MAC 接口与毫秒时钟；服务器回复由测试构造为完整 Ethernet/IP/UDP 帧，客户端输出从 MAC 提交口解析。每种模式独立进程。两组命令均成功退出。

| 检查 | 结果及证据 |
| --- | --- |
| 静态兼容、链路/启动门禁 | PASS；静态模式 ARP/ICMP、MTU、原 E4.1 错误路径通过。DHCP 版静态模式折返后仍 0 次 DHCP Start/Discover、无 PCB、无 DHCP context。无链路不启动。 |
| Discover、Offer、Request、ACK、NAK | PASS；观测实际输出 Discover 和带请求地址的 Request；注入 NAK 后立即重新发现；ACK 和 ACD 后才有效，IP/Mask/Gateway/Server 与 360/120/240 秒租约正确。 |
| T1、T2、到期 | PASS；虚拟时钟驱动真实周期定时器，验证 T1 单播和 ACK、T2 广播和 ACK、之后停止应答让租约到期；旧地址消失并重新 Discover。粗定时粒度沿用上游 60 秒。 |
| 无服务器、等待超时、Fallback | PASS；DHCP Only 超时一次后继续发送 Discover、IP 仍为 0；Fallback 拒绝空配置，超时只启用指定静态地址、释放 DHCP 资源，迟到 ACK 无效；再次 Link Up 后重新获取租约。 |
| 掉线与重获 | PASS；保留 admin up、关闭 link 、清除旧地址并回收 PCB/heap；无掉线 Release 发到 MAC。重连只发起一次适配层 Start，再实际交换报文取租约。 |
| UDP PCB、PBUF、超时池 | PASS；强制 PCB 不足得到 ERR_MEM，限速重试并恢复；超时池耗尽可计数，释放后真实定时器回调正常；PBUF_POOL 回到 0、原静态池耗尽和失败释放回归通过。 |
| 实板 DHCP、Ping、拔插后重新获取 | PASS（2026-11-19）；Windows ICS 提供租约，抓到完整 DORA，新地址 Ping 4/4。 |
| 实板 Static Fallback | PASS（2026-11-19）；无 DHCP 回复，状态进入 FALLBACK，备用静态地址 ARP 和 Ping 通过。 |
| 实板 T1/T2、Lease Expiry | NOT_RUN；当前 ICS 租约七天，未用于短租约验收。 |

## 实板 DHCP 和重连结果（2026-11-19）

用户手动烧录；PC 连接手机热点，Windows Internet 连接共享给直连板卡的以太网 3，PC 端为 `192.168.137.1/24`。初始 Watch 显示 BOUND、lease_valid=1、starts=1，地址为 `192.168.137.254`；初次 Ping 4/4，MAC 对应 `02:47:47:00:00:01`。

用户按步骤拔线再插回，恢复截图显示 BOUND、lease_valid=1、starts=2、timeouts=0、fallbacks=0。整盘回放确认同一事务（`abcd0003`）的 DISCOVER、OFFER、REQUEST、ACK；ACK 发放 `192.168.137.165/24`，网关及服务器为 `192.168.137.1`，租期 604800 秒；Watch 显示 T1 302400 秒、T2 453600 秒。新地址 Ping 4/4、丢包 0%、往返小于 1 ms，对应板卡 MAC。未取得拔线中间状态截图；此结果验证了重连实际重新取得租约和通信，不能代替掉线瞬间的地址/资源快照或实板续租。

[完整回放摘要](Evidence/E4_2_20261119/dhcp_relink_replay.json)、Watch 截图和 Ping 日志已归档。原始 PCAP 保留在本机 `output/Ethernet/E4_2/dhcp_relink_20261119_161058/capture.pcap`，199 条记录，Npcap 报告丢弃 0，SHA256 与回放一致。

## 实板静态 Fallback（2026-11-19）

Windows 关闭 WLAN 连接共享，直连的以太网 3 配置为 `192.168.50.1/24`。首次排查时实际为 DHCP Only（用户确认未设置 mode=2），未计作 Fallback 验收。正式复测在 main 入口设置 mode=2、timeout=30000 ms、fallback `192.168.50.2/24`、网关 0，随后运行。板端截图确认 FALLBACK、lease_valid=0、lwip_state=OFF、starts=1、timeouts=1、fallbacks=1，当前 IP 为 `192.168.50.2`。截图没有 UDP PCB 池字段，不把此次观察当成实板池归还读数。

原始抓包中同一事务的 4 次 Discover 均无回复，间隔约 0、4、12、28 秒；随后板端以备用地址回应 ARP 和 4 次 ICMP。PC 明确绑定 `192.168.50.1` 从直连网口 Ping，4/4、0% 丢包、往返小于 1 ms。Npcap 报告丢弃 0。[回放摘要](Evidence/E4_2_20261119/dhcp_fallback_replay.json)已记录 SHA256；原始 PCAP 留在 `output/Ethernet/E4_2/dhcp_fallback_mode2_20261119_163138/capture.pcap`。Fallback 状态下再拔插触发重试已在主机测试通过，本轮未在板上单独执行。

## 编译和内存

重新全量编译原 `MDK-ARM/h7_mac.uvprojx` 的 `h7_mac_CM7` 及独立 E4.2 目标，均为 **0 Error / 0 Warning**。本地日志为 `output/Ethernet/e42_production_build.log`、`e42_build.log`，链接和哈希记录为 `output/Ethernet/e42_result.json`。初次 ARMCC 链接通过但有两处枚举初始化警告；改成显式枚举后最终全量构建无警告。

| 目标（字节） | Code | RO | RW | ZI |
| --- | ---: | ---: | ---: | ---: |
| 原 CM7 | 137998 | 3114 | 1396 | 113484 |
| E4.1 已归档基线 | 26798 | 1110 | 156 | 38388 |
| E4.2 | 38022 | 1242 | 212 | 38720 |
| E4.2 相对 E4.1 | +11224 | +132 | +56 | +332 |

| ARMCC 链接对象 | 地址 | 占用 |
| --- | --- | ---: |
| Flash RO | 0x08000000 | 0x9960 |
| 普通 DTCM RW/ZI | 0x20000000 | 0x1570 |
| Region 5 缓存执行区 | 0x24010000 | 20,900，剩余 **44,636** 字节 |
| 网络上下文 | 0x24010060 | 3,328 |
| lwIP heap backing array | 0x24010E58 | 8,211；配置 heap 8,192 |
| UDP PCB backing array | 0x24012EEA | 35；1 个 PCB，含对齐余量 |
| SYS_TIMEOUT backing array | 0x24012F18 | 99；6 个槽，含对齐余量 |
| PBUF_POOL backing array | 0x24013094 | 8,451；16 节点，数据区各 512 |
| Region 4 非缓存 DMA 池 | 0x24078000 | 12,544，不变 |

`fromelf` 确认 ARMCC 的 `dhcp_start()` 以 `0x48`（72 字节）请求 DHCP context，不含 heap 分配器开销或报文 pbuf。Region 5 地址为原缓存窗口，Raw API 的 DHCP/ARP/ICMP 数据仍经 CPU 缓冲复制；DMA 使用 Region 4 专用池。DCache 保持启用，独立固件不改变两区属性。本表是链接与静态资源证据；实际峰值需实板读取。

## 实板准备（不自动烧录）

1. 如需证明板上旧镜像身份，先保留并完成相关读回。构建 `python Tests/Ethernet/build_firmware.py --e42`，打开独立工程 `MDK-ARM/h7_mac_ethernet_e42.uvprojx`，人工加载 `output/Ethernet/e42/eth_e42.axf`。本轮 AXF SHA256：`05f113fdf8d513e2057474626cf89cc7923dd8a099a55ed13928e2cdf9370a9d`。本地 AXF 哈希不能当成板上校验日志。
2. 默认模式为 `g_e42_mode=1`（DHCP Only）。切换模式时在进入 `main`、C 运行时已初始化全局变量但尚未执行网络初始化时暂停，设置 `0`（旧静态 `192.168.50.2/24`）或 `2`（Fallback）。模式 2 需同时写 `g_e42_fallback_ip[0..3]`、`g_e42_fallback_mask[0..3]`、`g_e42_fallback_gateway[0..3]`；仅在隔离直连网络可示例选 `192.168.50.2/24`、网关 0，PC 选 `192.168.50.1/24`。等待时间 `g_e42_timeout_ms` 默认 30000。设置后移除断点，保持 CPU 运行。
3. **DORA：**将板和 PC 放入有 DHCP Server 的测试网。直连且仅配置 PC 静态 IP 不会自动提供 DHCP。Wireshark 过滤 `udp.port == 67 || udp.port == 68`，必要时在 DHCP Server 网口/交换机镜像端口抓取单播。保存 DORA、ACD 和 `g_e4_status.address` 的真实 IP、Mask、Gateway、Server、租约值；Link Up 不等于 BOUND。
4. **连通性及掉线：**PC Ping 实际分配 IP，保存 ARP/ICMP。拔线截图记录 admin up、link down、lease_valid=0、IP 清零、UDP used 回落；插线后不复位，记录新 Discover、租约和再次 Ping。检查 link_up/down 次数、端口与 MAC/DMA 错误、UDP/定时器/heap/PBUF 的 used/peak/errors；同时保留 CCR.DC 和 memory_valid。保存实际截图和原始 PCAP。
5. **Fallback：**接无 DHCP 的隔离网络，记录超时前地址为零、DISCOVER 和超时后 FALLBACK；核对明确配置的静态地址、有效租约位为 0、PCB 已归还，并从对端 Ping。再次拔插应重新尝试 DHCP。
6. **续租/重绑/到期：**测试服务器可配置 360 秒租约、120 秒 T1、240 秒 T2。用可控服务器回复或不回复续租，分别验证 T1 单播、T2 广播重绑和持续不回复时到期清 IP 后重新 Discover。保存服务器和板端证据，按上游 60 秒粗定时精度判读。

### 直连 PC 的短租约专用工具

没有可配置路由器时，保持板卡直接连测试网口 `以太网 3`，PC 端只配置 `192.168.50.1/24`，关闭 Windows ICS，不接交换机或桥接网络。板卡复位后在 `main` 暂停并确认 `g_e42_mode=1`，等工具输出 `LEASE_CAPTURE_READY` 再取消断点运行。工具只监听已固定的 PC 测试 MAC，只处理该板卡的 MAC；发现另一 DHCP 服务器的响应即停止。先运行离线检查：

```powershell
python -B -m unittest discover -s Tests/Ethernet -p test_e42_lease_peer.py
python -B Tests/Ethernet/e42_lease_peer.py --direct-board-only --output output/Ethernet/E4_2/lease_board_run1
```

此方案使用 `192.168.50.2/24`、租期 180 秒、T1 60 秒、T2 120 秒。首次绑定后，工具回复一次 T1 单播，然后不回复下一次续租直到 T2，回复一次 T2 广播请求；接着不再回复续租、重绑及到期后的 Discover。每个阶段观察 `g_e4_status.address`，保存首次 BOUND、续租/重绑期间及到期后无 IP 的证据。首次 ACK 后还有 ACD，实际计时按板端状态与抓包对齐，不要求从 PC 点击按钮起整秒发生。工具保存原始 PCAP 和阶段记录，不能以工具自己的状态代替板端租约清零证明。通常约 6 分钟，期间保持连接、CPU 运行；无需下载新固件。

当前停在 E4.2，等待人工审核与实板 DHCP 验收；未进入 E5。
