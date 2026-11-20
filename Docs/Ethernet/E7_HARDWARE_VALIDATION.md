# E7 实板验收

**V1 主控板**请使用 [E7 V1 独立工程和固件](E7_V1_ADAPTATION.md)；以下原始构建命令对应 V0。两版使用相同的 Watch 变量。

所有实板项目当前 **NOT_RUN**。执行 `python -B Tests/Ethernet/build_firmware.py --e7`，打开 `MDK-ARM/h7_mac_ethernet_e7.uvprojx` 并人工下载 `output/Ethernet/e7/eth_e7.axf`。保存 Flash Verify、AXF、MAP 和 PCAPNG。默认 DHCP Only。静态预检时在 main 读取配置前设 `g_e7_mode=0`，PC 用 192.168.50.1/24，板端用 192.168.50.2/24。

1. 确认 `g_e7_sdram_result=0`、`g_e7_sensor_id=0x5640`、`g_e7_camera_init=0`、`g_e7_result=0`、`g_e7_camera.running=1`。确认地址和 `g_e7_transport.listening=1`，DCache 保持开启。复核 Region 2、4、5 硬件读回。
2. 浏览器打开板端地址，移动镜头前的物体并保存两张能辨认真实物体变化的截图。连续运行至少 600 秒，保存采集、复制、提交、TCP ACK 四组 FPS、丢帧及所有驱动统计。
3. 关闭浏览器后逐帧验证。保留 DHCP/TCP 80 抓包、JSON、帧尺寸和错误计数。

```powershell
python -B Tests/Ethernet/e6_peer.py --camera --board-ip 192.168.50.2 --seconds 610 --output output/Ethernet/e7_board/stream.json
python -B Tests/Ethernet/e6_peer.py --camera --board-ip 192.168.50.2 --stream-only --slow-ms 350 --receive-buffer 2048 --output output/Ethernet/e7_board/slow.json
python -B Tests/Ethernet/e6_peer.py --camera --board-ip 192.168.50.2 --stream-only --frames 1 --stop-read-seconds 8 --receive-buffer 2048 --rst --output output/Ethernet/e7_board/reset.json
```

4. 慢读时观察 `g_e7_http.stream.send_busy`、`frame_skipped`。同时确认 Camera 完成数仍增加、`release_errors=0`。浏览器正常关闭和 RST 后检查私有租约释放、新播放从序号 1 开始。
5. 拔插网线，分别核对物理链路、IPv4 有效及 listener 恢复，重开网页。更换 DHCP 地址时按新 IP 验证。
6. 写 `g_e7_command=1` 请求 Stop，确认 `g_e7_command_result=0`、StopFacts 完成且成功，`g_e7_dma_cr.EN=0`、`g_e7_dcmi_cr.CAPTURE/ENABLE=0`。待私有租约释放后用命令 2 重启，检查旧租约无效。硬件停机或原帧归还失败时保留 Watch 和寄存器，无成功证明则停止该轮复验。

目前 DCMI/DMA Watch 提供实时寄存器快照与 Camera 层合并错误计数。分别统计硬件中断错误需要现有 BSP 的专用计数接口，本轮保持其实现边界。
