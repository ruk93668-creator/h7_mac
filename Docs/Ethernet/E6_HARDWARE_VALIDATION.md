# E6 实板验收

使用独立 Keil 工程 `MDK-ARM/h7_mac_ethernet_e6.uvprojx`，来源 `python -B Tests/Ethernet/build_firmware.py --e6`，输出 `output/Ethernet/e6/eth_e6.axf`。人工下载并记录 Flash Verify、AXF SHA-256 和 MAP。默认 DHCP Only，沿用 E5 的隔离链路短租约服务。静态预检可用 `g_e6_mode=0`，板端 `192.168.50.2/24`，PC `192.168.50.1/24`。在 `main` 读取配置前设值，`g_e6_period_ms=200`。

启动后检查 `g_e6_result=0`、`g_e6_status.driver.state=2`、`transport.ready=1`、`transport.listening=1`、`memory_valid=1` 和 CCR.DC。打开 Chromium 系浏览器访问 `http://192.168.50.2/`（或 DHCP 租约中的板端地址）。页面至少应显示标题、板端地址和位置变化的图形。测试只有一个活动连接，运行 PC 工具前先关闭浏览器页面，播放和压力测试按顺序执行。

在 PC 测试端使用具有隔离网访问权限的 Python。先启动 Wireshark 捕获板端流量，显示过滤器 `tcp.port == 80 || udp.port == 67 || udp.port == 68`。

```powershell
python -B Tests/Ethernet/e6_peer.py --board-ip 192.168.50.2 --output output/Ethernet/e6_board/short.json
python -B Tests/Ethernet/e6_peer.py --board-ip 192.168.50.2 --stream-only --seconds 610 --min-fps 4.8 --output output/Ethernet/e6_board/long.json
python -B Tests/Ethernet/e6_peer.py --board-ip 192.168.50.2 --stream-only --frames 2 --reconnect 50 --output output/Ethernet/e6_board/reconnect.json
python -B Tests/Ethernet/e6_peer.py --board-ip 192.168.50.2 --stream-only --slow-ms 350 --receive-buffer 2048 --output output/Ethernet/e6_board/slow.json
python -B Tests/Ethernet/e6_peer.py --board-ip 192.168.50.2 --stream-only --frames 1 --stop-read-seconds 8 --receive-buffer 2048 --rst --output output/Ethernet/e6_board/rst.json
```

慢读与停止读取至少各观察一次 Watch 的 `http.stream.send_busy`、`frame_skipped`、`send_errors`。另做真实浏览器关闭、重新打开和拔插网线；记录 NETIF/监听恢复、浏览器重连和地址变化。正常关闭时核对 FIN/ACK，RST 后检查重连从首帧开始。记录运行中及关闭至少 125 秒后的池占用和驱动错误。

| 实板项目 | 状态 |
| --- | --- |
| E6 固件 DHCP 与 Flash Verify | NOT_RUN |
| 真实浏览器首页和动态 160×120 画面 | NOT_RUN |
| 5 FPS 目标与连续至少 10 分钟 | NOT_RUN |
| PC 逐帧校验、慢读、停止读取 | NOT_RUN |
| 关闭重开、50 次刷新/连接、FIN/RST | NOT_RUN |
| Link Down/Up 恢复和板端资源回收 | NOT_RUN |

保留 PCAPNG、逐帧 JSON、浏览器截图、Watch、AXF SHA-256、MAP 及池峰值。主机夹具或回环测试不作为实板 PASS。
