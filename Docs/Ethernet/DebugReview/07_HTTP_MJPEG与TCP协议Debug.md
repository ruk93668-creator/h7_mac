# HTTP/MJPEG 与 TCP 协议 Debug

HTTP 请求可能跨 TCP 段，必须解析到完整标题结束符后才路由，缺行或头过大按对应错误响应。每个 multipart part 的边界、长度、CRC 和序号要一致。TCP ring 可返回 ERR_MEM，流状态保留偏移再重试；零窗口时等待，连接代次变化时丢弃旧句柄、清理当前租约。Host 用慢读、零窗口、FIN/RST、掉线与地址变更覆盖这些路径。


## C11 DHCP 恢复后 Listener 与 TIME_WAIT

### 1. 问题背景

E6 使用样片验证 HTTP/MJPEG 和链路恢复。

### 2. 第一次异常

Host 回归场景中重新获租后可服务的 IP 已恢复，旧 TCP tuple 仍处于 TIME_WAIT，监听重新绑定需处理。

### 3. 最初的怀疑

首先区分地址没有恢复、旧 listener 未移除与旧 TCP 状态影响重新绑定。

### 4. 排查过程

查历史 7f321f6 中 eth_tcp_server_start 的 SO_REUSE/SOF_REUSEADDR 设置，并比较 test_e6_network 的 relink 断言。

### 5. 关键突破

测试中同时观察新 listener 成功与旧 TIME_WAIT 存在，能证明重绑与旧连接回收可以并行。

### 6. 根因及修复

**CONFIRMED**。已在 Host 确认重新绑定机制。独立 E6 实板同场景 NOT_RUN。

### 7. 验证结果

真实 lwIP 加模拟 MAC/时钟，605 秒虚拟连续测试 3025 张确认帧及 5 次 DHCP 续租；旧句柄和资源回归通过。[E6_IMPLEMENTATION](../E6_IMPLEMENTATION.md) · [E6_HARDWARE_VALIDATION](../E6_HARDWARE_VALIDATION.md)

### 8. 底层原理

TIME_WAIT 能屏蔽旧连接迟到的数据；允许 listener 复用本地绑定，仍须由 TCP 及代次隔离旧连接。

### 9. 最终经验

对每种通过结果标注实板、真实协议软件或模拟硬件。

### 10. 面试复盘

HTTP/MJPEG 建在 TCP 字节流上，我们先用 E6 Host 把请求拆段、Multipart、零窗口、超时和重连跑通。一次需要重点验证的是重新拿到 IP 时旧连接仍留在 TIME_WAIT。通过检查早期代码里的 SO_REUSEADDR 和测试断言，我确认新 listener 成功绑定的同时旧连接仍在等待清理，旧代次句柄也无法操作新连接。605 秒长测是在 Host 的虚拟时钟内完成，不能用来宣称 E6 已烧录实板。这个题可以同时讲协议状态、可靠测试和结果适用范围。

源码（`a1fbe31`）`Platform/Ethernet/eth_http.c` 的 `EthHttp_Parse()`、`EthHttp_Request()` 与 `Platform/Ethernet/eth_mjpeg.c` 的 `eth_mjpeg_poll()` 第 119 行、`eth_mjpeg_ack()` 第 47 行。未提交的合并准备把两组 Service 文件迁到 `CM7/Service/Ethernet/`，本章行号仍对应历史版本。
