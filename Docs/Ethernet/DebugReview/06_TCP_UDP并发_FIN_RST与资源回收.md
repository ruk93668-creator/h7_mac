# TCP/UDP 并发、FIN/RST 与资源回收

## C09 Windows 入站过滤导致板端 SYN 无回应

### 1. 问题背景

板作 TCP Server 5003 与 TCP Client 5004，另跑 UDP 双向。

### 2. 第一次异常

E5 首轮板端 5004 方向重复 SYN，电脑无 SYN-ACK。

### 3. 最初的怀疑

端口未监听、本机策略、地址选择或板端握手状态都可能产生类似现象。

### 4. 排查过程

定向抓 5004，查 Public 防火墙规则，换成已有许可的 Python 3.12 和正确的有线网口。

### 5. 关键突破

相同板端路径换解释器后出现正常握手，报告记录入站规则差异。

### 6. 根因及修复

**CONFIRMED**。已确认该轮入站许可是影响因素；工具收尾随后另修 wait_closed 等待顺序。

### 7. 验证结果

可见 [定向 PCAPNG](../../../output/Ethernet/e5_board/static-5004-20261119-181715.pcapng) 与 [E5_HARDWARE_VALIDATION](../E5_HARDWARE_VALIDATION.md)。静态短测每通道 280/280 Echo。

### 8. 底层原理

TCP Client 看见 SYN 已发并不代表 PC Socket 收到。入站按程序、网口和网络配置筛选。

### 9. 最终经验

两条 TCP 方向分别看握手，避免把 PC 入站失败归到 UDP 或板端任务。

### 10. 面试复盘

E5 板既是 5003 的 Server，又会主动连 PC 的 5004，另有 UDP。我在 5004 定向抓到板端重发 SYN，PC 没有 SYN-ACK。对比本机 Public 配置和程序入站许可后，用已有许可的 Python 3.12 在同一有线口重测，握手成功。这个对照说明 Windows 本地策略确实影响该方向。另一处工具收尾等待是独立问题，先关闭已经接受的会话再等待监听资源自然结束。面试时我会把连接方向、包号和进程许可作为排查线索说出来。


## C10 长测通过而首次关闭失败

### 1. 问题背景

测三通道并发、DHCP 续租，再单独验关闭和池回收。

### 2. 第一次异常

TCP 5003 在活动窗口之后，帧 51887 FIN、51889 ACK，51891 仍带 272 B 数据，51892 RST。

### 3. 最初的怀疑

初看 RST 容易误判活跃流量期间失稳；也可能是 PC 过早关闭或板仍发主动请求。

### 4. 排查过程

对齐抓包与控制窗口，复读 51887–51892；把新增主动请求关掉，保留接收、回显和计时直到排空，再关 Socket。68e1df7 修 PC 工具先关子连接。

### 5. 关键突破

RST 与尾段主动数据交错发生在关闭阶段，活动并发窗口单独结算。

### 6. 根因及修复

**CONFIRMED**。关闭顺序和新增请求竞态得到实板复验。完整生命周期在正式系统仍需另验。

### 7. 验证结果

619.836 s 双向交集，三通道各 3971/3971；独立 30 秒关闭轮各 192/192、双向 FIN/ACK 无 RST；至少运行 125 秒后活动 PCB/SEG/PBUF 回零，DHCP 与监听资源仍在。[E5_HARDWARE_VALIDATION](../E5_HARDWARE_VALIDATION.md) · [长测原始 PCAPNG](../../../output/Ethernet/e5_board/dhcp-long-20261119-190305/capture.pcapng) · [收尾复验](../../../output/Ethernet/e5_board/dhcp-close-20261119-193546/close-validation.json)

### 8. 底层原理

FIN 只关闭一个方向，ACK 说明字节被 TCP 层接受；RST 与 TIME_WAIT 有独立语义。UDP 没有对应握手，必须按有效应用时间窗口核算。

### 9. 最终经验

将持续流量、停止新请求、排空、FIN、计时器回收分别验收。

### 10. 面试复盘

我们跑了 619.836 秒三通道交集长测，各通道 3971 个 Echo 全部核对。第一次收尾却在 FIN 和 ACK 后看到板端继续发 272 字节，下一包 PC 给了 RST。我将根据抓包包号把它划入关闭阶段，并保留那一轮关闭失败。随后先停止两侧新增请求，继续读和回显，把旧数据排空，再关闭 Socket。独立短复验无 RST，运行定时器后活动 PCB、segment 与 PBUF 回零，仍在工作的 DHCP 和监听资源保持在线。这样验收能区分数据稳定性和关停正确性。

源码（`a1fbe31`）`Platform/Ethernet/eth_tcp.c` 的 `EthTcp_Receive()`、`EthTcp_PollConnection()`、`eth_tcp_close()` 分别处理 RX/FIN、重试排空和发起关闭；`Tests/Ethernet/e5_peer.py` 是 PC 对端。
