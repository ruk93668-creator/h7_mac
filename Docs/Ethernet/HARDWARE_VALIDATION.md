# E2/E3 实板测试准备报告

状态：**未下载、未完成 PHY Link 实测、未完成 MAC/DMA 实板收发、没有性能数据。** 软件模拟和编译通过均不替代硬件验收。

需准备指定 STM32H747 实板、可恢复原固件的下载器、隔离以太网对端和可测 50 MHz 的仪器。记录实物 PCB/BOM 版本和实际安装的 RJ45 方案。仅使用本工作目录生成的固件，不操作 h7_mac 或 h7_mac_sd 工程。

## 构建与测试入口

1. 在工程根目录运行 `python Tests/Ethernet/build_firmware.py --smoke`。
2. 成功产物为 `output/Ethernet/smoke/eth_smoke.axf`，仅供测试；脚本不刷写。
3. 该固件替换 CM7 main，不运行 Application、Framework 初始化或 RTOS scheduler；沿用本工程的 SystemClock_Config 和 HAL TIM6 时基。它不唤醒 CM4 的 HSEM 启动握手，不修改 CM4 固件。测试前确认 CM4 已按原启动方式进入等待，避免旧固件仍活动。
4. 固件开启 ICache 和 DCache，MPU region 4 先于 cache 配置。测试 MAC 固定为 02:47:47:00:00:01；仅在隔离测试网络使用。
5. 用调试器观察 `g_eth_test_result`、`g_eth_test_statistics`、`g_eth_test_rx_count`、`g_eth_test_tx_done`、`g_eth_test_last_rx` 和 `g_eth_test_last_rx_length`。无需串口；若额外采集串口，需先确认现有引脚和波特率，本次未增加 UART 配置。

调试命令：写 `g_eth_test_command=1` 初始化；2 启动；3 停止；4 发一帧。主循环取走后自动清零。写 `g_eth_test_continuous=1` 后每毫秒尝试发一帧，BUSY 不算成功；写 0 停止连续提交。调试期间保持 HAL tick 运行，不在持有 DMA buffer 时修改其内存。

## 独立验收门禁

| 步骤 | 动作和必须保留的证据 |
| --- | --- |
| 1 PHY ID | 命令 1；result=0；记录 phy_id，应匹配 LAN8742A OUI/model，revision 可变。先示波确认 PG9 reset 和 PA1 的 50 MHz，不能只以 MDIO 可读代替 RMII 时钟测量 |
| 2 PHY Link | 接对端；在 `eth_mac_start` 内 `eth_mac_get_link` 返回后、MAC/DMA 启动前停住，记录 local link 的 up、autoneg_complete、speed_mbps、full_duplex；或由同一任务单独调用公开 get_link 接口采集。up=0 或协商未完成不得放行后续步骤 |
| 3 MAC 初始化 | 检查 SYSCFG RMII、GPIO AF11、MACA0 地址；核对 MACCR speed/duplex 与步骤 2 一致，不用固定 100/full 代替协商结果 |
| 4 DMA 启动 | 命令 2 返回 0、state=RUNNING；核对 descriptor base/ring length/DSL/RBSZ、MAC TE/RE、DMA ST/SR、MPU region 4；确认无 FBE/CDE |
| 5 TX | 命令 4；对端抓取 EtherType 0x88B5，检查目的广播、源 MAC、60 字节无 FCS 内容和序号；本机 ticket/reclaim 成功还不能单独证明线上已收到 |
| 6 RX | 对端向测试 MAC 发 0x88B5；检查 rx_count、长度和完整字节内容；对端可能插入 padding，驱动不向调用者提供 FCS |
| 7 连续双向 | 每方向先 1000 帧，再持续 10 分钟；记录双方计数、序号、数据校验、各错误计数及丢包。测试 60/512/1514/1522 字节边界；raw_peer.py 当前生成 60 字节测试帧，更大帧用对端抓包工具构造 |
| 8 错误恢复 | 停止 CPU 消费使 RX 耗尽，恢复 poll 后确认继续；连续提交 4 帧应有 BUSY；拔线 service 应停止并返回 LINK_DOWN，插回后显式 start；重复 stop/start 100 次；故障时记录 last_dma_status/last_mac_status，按 stop/start 恢复。禁止为造 FBE 而向生产内存写入坏 DMA 地址 |

在 RUNNING 时主循环每 10 ms 调用 service 检查链路。故障需要任务层 service/stop，不能把 ISR 已屏蔽中断误认为 DMA 已停止。单步调试会造成接收耗尽，必须在报告区分调试暂停和自然丢包。

## 对端工具

Linux 独立网卡、具备 raw socket 权限时，可显式运行：

```sh
sudo python3 Tests/Ethernet/raw_peer.py --interface enp3s0 --count 1000 --seconds 10
```

工具只收发实验 EtherType 0x88B5，不配置 IP、不发送 TCP/UDP。接口名需改为实际网卡。PCAP 过滤 `eth.type == 0x88b5`，保留 PCAP、工具 JSON、固件 SHA、PHY ID、协商模式、错误统计、测试时长及缓存状态。工具仅完成语法和帮助入口验证，尚未在 Linux 网卡执行。

本阶段吞吐或时延不得报告为实测值；独立 DMA 模型的 3000 帧通过不是网络性能结果。测试固件和 raw peer 均留在 Tests，不能作为生产 Application 交付。
