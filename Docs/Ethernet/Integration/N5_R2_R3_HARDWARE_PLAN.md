# N5-R2/R3 实板验收方案

状态 **NOT_RUN**。软件独立审核通过后，等待用户批准，再由操作者使用 V1 板执行。每轮保留固件 SHA、AXF/HEX Verify 结果、PCAPNG、工具 JSON、Watch 快照和异常前后时间。使用独立目录保存每次运行。

| 步骤 | 操作和通过条件 | 原因 |
| --- | --- | --- |
| 1. 固件和内存 | 选择同一候选 SHA 的 N5 Hardware Acceptance AXF/HEX，用下载器 Verify 实际 Flash。Keil 读 CCR 的 DCache 位及 MPU Region 2/3/4/5/6，归档原始值。 | 将抓包、固件版本和 R1 JPEG 布局对应到同一现场。 |
| 2. DHCP 和 Listener | V1 板与 PC 连接测试网络，先抓包再运行。记录 DHCP Discover/Offer/Request/ACK、当前租约、driver.state、lifecycle 和 flags。访问板端 /health 并取得 HTTP 200。 | 区分管理链路、地址和端点是否就绪。 |
| 3. MDIO 与 HTTP 恢复 | 记录 driver.first_mdio_fault、Busy/Timeout/IRQ 计数和 recovering。拔插验证正常 Link Down/Up 和监听重建。遇到实际 MDIO 错误，先保存首次寄存器、时钟及抓包，再比对 AR 的 MB、CR、PA、RDA。暖复位后复读首故障并保存前后值。 | 区分拔线和管理接口失败。实际 MDIO 故障未发生时，其现场保持 NOT_RUN。 |
| 4. 100 帧 CRC | 使用 N5 工具修复分支的 e6_peer.py，以 --stream-only --camera --width 320 --height 240 --frames 100 读取板端并保存新的 JSON。要求 100 帧尺寸和 CRC 全部通过，同时记录帧号及长度。 | 验证 R1 CPU 私有 JPEG 到 PC 的数据完整性。 |
| 5. MJPEG 长稳 | 单流至少 60 分钟，记录帧数、CRC、FPS、PCB/SEG/PBUF、堆峰值、DMA/MAC IRQ 和 MDIO 变化。断开浏览器后观察退休，再重连并确认继续出帧。 | 验证持续传输与收尾期间资源可复用。 |
| 6. USB + SD + Ethernet | 在 IDLE 设置未占用的非零 g_n5_file_number、g_n5_joint_ms=3600000，再写 g_n5_command=2。检查 USB、SD 和 MJPEG 三路实际输出、丢帧及错误。停止后检查 Camera 的跨任务租约与应答、网络和存储资源。 | 验证共有 Camera Frame 的所有权与资源竞争行为。 |
| 7. 100 轮生命周期 | 在 IDLE 设置 g_n5_cycles=100、g_n5_dwell_ms=1000、g_n5_ready_ms=30000，再写 g_n5_command=1。确认 completed=100 且 active、uncertain、first_fault 均为 0。另通过现有控制接口执行 START/STOP/RESTART 并确认监听重建，保存所有终结结果与首故障。 | 区分“命令已入队”和“实际退休完成”，发现跨轮资源残留。 |

发现 MDIO 错误时保存[首故障报告](N5_R2_R3_DIAGNOSTICS_AND_RECOVERY.md)所列字段。PCAP 可验证 TCP、HTTP、DHCP 行为。若首故障仍不足以确认硬件原因，保留 NOT_PROVEN，待审核决定是否补采 RMII 时钟、MDC/MDIO 波形。
