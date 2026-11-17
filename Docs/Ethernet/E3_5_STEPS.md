# E3.5 实板执行步骤

Keil 请打开独立入口 `MDK-ARM/h7_mac_ethernet_e35.uvprojx`。生成命令为 `python Tests/Ethernet/build_firmware.py --e35 --prepare-only`（只生成配置，不编译、不下载）。不要继续使用已打开的旧 smoke 工程：Keil 可能保留先前的输出目录。该入口固定输出 `output/Ethernet/e35/eth_e35.axf`，不在 smoke 目录复制同名固件。

本轮固件与旧 `test_main.c` 命令不同。只下载 `output/Ethernet/e35/eth_e35.axf`；不要下载旧 smoke 文件。测试工程由 `python Tests/Ethernet/build_firmware.py --e35` 生成，入口 `e35_main.c`，不启动 Application、FreeRTOS 或 CM4。先保存实际下载文件 SHA256，与 E3_5_VALIDATION.md 核对。下面 PowerShell 命令均在 `D:\32\STM32H7\h7_mac_wk` 执行。

## 连接、启动和记录

1. 网线连接板卡与以太网 3，PC MAC 必须为 `EC-1A-C3-3E-DB-B2`。其他网卡不是本工具的测试对端。VLAN 测试先检查网卡驱动是否剥离标签；PCAP 必须可见 8100 / VID 7。没有标签的抓包不能用于判定板卡 VLAN 接收失败。
2. 下载后运行 CPU，禁用收发路径断点；Watch 开启运行时刷新。写 `g_e35_command=1`，等待命令自动清零，记录 `g_e35_result=0`、`g_e35_statistics.phy_id`。
3. 写命令 `2`，记录 result=0、state=2、`g_e35_link` 的 up、autoneg_complete、speed_mbps、full_duplex。填写真实值，不能只写“符合预期”。
4. 记录 `g_e35_metrics.ccr`（DC 位 16 必须为 1）、memory_valid=1、caller_tx_address、caller_rx_address、mac_config、vlan_config。两个 caller 地址应在 `0x24010000..0x2401FFFF`；DMA 池仍为 `0x24078000`。记录两次 heartbeat / poll_calls / cache_pressure_passes，确认 CPU 在运行。
5. 每个用例开始与结束均保存 `g_e35_statistics`、`g_e35_metrics`、`g_e35_link` 的完整数值或截图。IRQ 是事件数，可能合并，不能要求 irq_rx 与帧数完全相等。计数差值按 uint32 回绕计算。

命令只在前一命令清零、result 已更新后发送。100 次重启运行期间不要发送别的命令。配置仅在 active=0 修改；命令 7 锁存 session/length/vlan。每个独立测试使用新的 session，避免旧包混入。

| 命令 | 操作 |
| --- | --- |
| 1 / 2 / 3 | 初始化 / Start / Stop |
| 4 | 本会话发送一帧 |
| 7 | 开始新会话并清本会话计数（要求 RUNNING、active=0） |
| 8 | 结束采样、停止连续 TX；不停止 MAC |
| 9 / 10 | 暂停 / 恢复 RX Poll；CPU、服务、IRQ 持续运行 |
| 11 | active=0 时执行 100 次 Stop/Start，失败即停 |
| 12 | 连续提交四帧、中途不 reclaim，记录四个返回值 |

`g_e35_result` 仅记录命令结果。运行错误查看 metrics.first_error / errors / service_result 与驱动错误计数。Link Down 返回正值 3，单独记录，不算负错误。命令 8 后 valid_rx / TX 计数冻结，但心跳、raw_rx、IRQ 与统计仍可能变化。

## 帧长测试（先 60，再 512、1514、VLAN）

每项先命令 8、3、2（确认无待回收 TX），记录驱动初值；设置：

```ini
g_e35_session = 351
g_e35_length = 60
g_e35_vlan = 0
g_e35_poll_enabled = 1
g_e35_interval_ms = 10
g_e35_tx_duration_ms = 15000
g_e35_command = 7
```

确认 active=1、result=0、next_sequence=1 后，在 PC 执行：

```powershell
python Tests/Ethernet/e35_peer.py --session 351 --length 60 --count 1000 --interval-ms 10 --delay-seconds 10 --capture-seconds 30 --output output/Ethernet/E3_5/board_60_run1
```

看到 `CAPTURE_READY` 后，保持 CPU 运行，写 `g_e35_tx_enabled=1`。PC 发送前预留了 10 秒操作时间。等待 PC 完成、板 TX 自动停止并且 tx_completed=tx_accepted；再命令 8，保存最终值。输出目录必须是新目录，禁止覆盖旧证据。

依次更换 session=352/353/354，长度=512/1514/1518；VLAN 项 `g_e35_vlan=1`、PC 加 `--vlan`。1518 为不含 FCS 长度，对应用户确认的线上 1522 字节。普通帧前三项不加 `--vlan`。工具额外允许不含 FCS 的 1522，仅供后续负向边界调查，不是本轮标准 VLAN 验收项。

通过要求：板 valid_rx=1000、next_sequence=1001，corrupt/gaps/late_or_duplicate/errors=0；PC sent=1000，complete=true、corrupt/duplicates/reordered/internal_missing=0，board_unique=板 tx_accepted=板 tx_completed>0。驱动 rx_errors/tx_errors/ownership_errors/irq_dma_error/irq_mac_error 无增量，PC npcap_stats.dropped=0。两方向末尾丢失必须通过这些总数相等排除，不能只看内部序号缺口为零。后台无关帧允许增加 ignored/raw_rx。VLAN 的 mac_config 中 JE/S2KP/GPSLCE 应为 0，vlan_config 标签剥离字段为 0，MACPFR.VTFE=0；MAC 负责补 FCS，软件不附加 FCS。

## TX Ring 满与 BUSY 恢复

先命令 8、3、2，开始新会话 session=355、length=60、vlan=0、命令 7。PC 只捕获：

```powershell
python Tests/Ethernet/e35_peer.py --session 355 --count 0 --capture-seconds 30 --output output/Ethernet/E3_5/board_tx_full_run1
```

CAPTURE_READY 后命令 12。记录 `g_e35_burst[0..3]`，应为 `0,0,0,2`。即使硬件已完成，尚未软件 reclaim 的槽仍占用，故第 4 次应 BUSY。等 tx_completed=3 后命令 4，应 result=0、accepted=4、completed 最终=4、tx_busy=1，PC 唯一完整收到序号 1..4。保存 tx_full 前后增量。若不是该序列，先保存证据，不改 OWN 或 Tail 来迎合测试。

## RX 耗尽、恢复 Poll

先命令 8、3、2，session=356、length=60、vlan=0，命令 7 后 9。确认 poll_calls 停止增加而 heartbeat 持续增加，记录 RX OWN mask、irq_rbu 和 rx_delivered 初值。PC 发 1000 帧：

```powershell
python Tests/Ethernet/e35_peer.py --session 356 --count 1000 --interval-ms 1 --capture-seconds 10 --output output/Ethernet/E3_5/board_rx_exhaust_run1
```

CPU 全程运行，不用断点模拟暂停。结束后保存 RX OWN mask、irq_rbu、DMA 状态和错误计数。预期三个已发布描述符都耗用、mask=0，RBU 有增量；MTL FIFO 可能暂存一些帧，恢复后的交付数量不固定，不要求只收到三个。此阶段丢包属于有意资源耗尽，记录实际数量，不把它套用零丢包规则。

命令 10 恢复 Poll，等缓冲帧排空、raw_rx 稳定。命令 8 后换 session=357、命令 7，按普通 60 字节步骤再发 1000 帧。新会话必须 1000/1000、无错误，验证无需重启 MAC 就能恢复。分别保存“耗尽前/耗尽后/恢复后”三份完整快照和两份 PCAP。

## 100 次 Stop/Start

命令 8，确认 TX 已回收、链路稳定，命令 11。等待 `g_e35_restarts=100`；逐项保存 `g_e35_restart_stop[100]`、`g_e35_restart_start[100]`，200 个返回值必须全 0，最后 state=2。99 表示该步尚未执行，不能当 PASS。之后新 session=358，复做 60 字节双向 1000 帧，确认 ring 重建后真正能收发。记录中止帧数、错误计数增量。

## Link Down / Up

命令 8，停止对端发包。记录运行快照，拔线，保持 CPU 运行并等待刷新，记录 link.up=0、service_result=3、state=1；插线后记录真实 up=1/协商速率/双工。驱动要求显式命令 2 恢复，不自动 Start。确认 result=0、state=2，session=359 重做 60 字节双向测试，错误数无异常增量。

## 至少 10 分钟双向与缓存一致性

前面各项通过后再运行。命令 8、3、2，session=360、length=512、vlan=0、poll_enabled=1、interval_ms=10、tx_duration_ms=620000，命令 7。PC：

```powershell
python Tests/Ethernet/e35_peer.py --session 360 --length 512 --count 62000 --interval-ms 10 --delay-seconds 10 --capture-seconds 660 --output output/Ethernet/E3_5/board_duplex_10min_run1
```

CAPTURE_READY 后 5 秒内置 tx_enabled=1。约 11 分钟，脚本发送未完成时会自动延长捕获，避免旧测试 35 秒窗口截断问题；等待脚本退出，确认板 tx_enabled=0、tx_elapsed_ms>=620000、completed=accepted 后命令 8。每分钟保存一次心跳、计数、链路、错误、DC 位、memory_valid、cache_pressure_passes；不要停 CPU。

PC sent=62000、板 valid_rx=62000，其他通过要求同普通帧。用 PCAP 时间戳分别计算 PC→板与板→PC 的第一/最后有效测试帧时间；两方向收发重叠区间必须 >=600 秒。仅固件运行 620 秒不足以证明双向重叠；如果主机调度、延迟设置或手工操作使重叠不足，记录 NOT_RUN/重测，不截取旧抓包补齐。

记录每方向帧率和 `有效帧字节数*8/实际秒数`；该约 100 fps 测试是稳定性负载，不是最大吞吐测试。CPU 使用缓存调用者缓冲区且提交后立即污染原 TX 数据，DMA 使用非缓存副本；两方向全长比较必须持续为零损坏。32 KiB 缓存压力持续写读且 errors=0，ccr.DC=1，memory_valid=1。不能用“DCache 已开启”单独判定一致性通过。

## RX 0/1000 调查和证据保留

历史 case 保持 OPEN。若再次零接收，先保留现场，禁止先重启清零。保存 session/config/active、start_tick/end_tick、heartbeat 两次采样、poll_calls 两次采样、raw_rx、valid_rx、ignored、corrupt、next_sequence、驱动完整统计、OWN mask、DMACSR、PCAP、PC JSON、实际 AXF SHA。

- active=0 能证明本次采样关闭，不能单凭旧截图认定历史原因。
- heartbeat/tick 不增加且调试器显示 Halt，可证明本次 CPU 停止；运行时 Watch 未刷新需恢复刷新后复核。
- raw_rx/irq_rx 有变化、valid_rx=0：检查协议会话、标签和采样窗口，不能直接判断 DMA 坏。
- PCAP 有出站帧只证明 PC 捕获/提交，不证明线缆或板端接收；主机 VLAN 卸载也须排查。
- active=1、CPU/Poll 持续、Link正常，PCAP对应会话完整，但 RX OWN/irq/rx_delivered 不前进：记录真实接收异常候选，保留寄存器和缓存配置，调查根因后再决定修复。

原始 PCAP、JSON、截图、日志保存在每个新用例目录，不提交 AXF/构建产物；报告记录路径、SHA256、实际数字。传回板端实值后才更新 PASS/FAIL，整个 E3.5 未关闭前不进入 E4。
