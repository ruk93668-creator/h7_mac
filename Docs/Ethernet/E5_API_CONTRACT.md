# E5 公共接口

所有函数、回调、初始化和轮询都由调用 `eth_network_poll()` 的同一执行上下文使用。先初始化网络，再初始化 `eth_transport`；不会启动线程。配置被复制，但回调的 `arg` 由调用者长期持有。每轮按 network → transport → 测试或应用顺序执行。禁止 ISR 访问这些 API 或从回调递归轮询。

| API | 所有权与完成条件 |
| --- | --- |
| `eth_transport_init/poll/ready/get_statistics` | 单实例。ready 表示 admin、链路及 IPv4 均有效；具体监听和连接状态另查。网络失效或 IPv4 改变时移除应用 UDP、关闭 listener、终止托管连接；恢复后重新绑定。不会复用前一连接的排队数据。 |
| `eth_udp_start/stop/send` | 单应用 PCB；启停由 transport 管理。发送只接受 1–1472 字节；复制后调用 lwIP、释放临时 pbuf，返回后原缓冲可复用。ERR_MEM 表示本次失败，调用者决定是否有限重试。ERR_OK 是协议栈受理，可能等待 ARP，不是对端确认。 |
| UDP 接收回调 | 线性数据和源地址仅在回调期间借用；之后由适配层归还 pbuf。可同步回复；禁止从回调销毁接收服务。回调 ERR_MEM 记为资源丢弃，其他失败记为拒绝。超长或无效源丢弃。 |
| `eth_tcp_server_start/client_connect` | listener 最多一个被接受的活动连接；多余连接 RST。连接开始成功只表示 SYN 流程已启动，收到 OPEN 事件或读到 OPEN 才可发送。10 秒连接截止。PC 客户端目标、重试延迟可配置；重连延迟 0 表示同一 link epoch 不自动重试。 |
| `eth_tcp_handle/get_state/get_statistics` | 句柄含角色与代次，无 PCB。新连接不接受旧句柄。状态为 IDLE、CONNECTING、OPEN、DRAINING；lwIP 仍负责 SYN_RCVD、FIN_WAIT、CLOSE_WAIT、LAST_ACK、TIME_WAIT。统计按角色累计，pending/unacked 随连接清零。 |
| `eth_tcp_send` | 1–2920 B，整次全收或全拒；ERR_OK 表示复制入应用环，调用者可复用数据。ERR_MEM 不接受任何字节；等待 ACK 或后续轮询再提交。lwIP COPY 入队计 queued_lwip，DMA 成功分别由 netif/驱动计，TCP 对端确认由 tcp_sent 计 acked；三个计数不可互代。 |
| `eth_tcp_read` | 读取任意数量的流字节，返回实际数量；0 意味本轮无字节或无效连接，FIN 通过事件区分。读出后才调用 tcp_recved，恢复对端窗口；应用不持有 pbuf。 |
| `eth_tcp_close/abort` | Close 先禁止新提交，等待当前环、ACK 与 RX 排空；应用应继续读。5 秒内未排空则 abort。正常 FIN 后交还 PCB 给 lwIP；不能观察 CLOSE 事件就认定 FIN 已确认。Abort 丢弃未确认内容，不重发应用请求。 |
| TCP 事件回调 | 在 transport 轮询发出 OPEN/READ/ACK/FIN/CLOSED，允许通过有代次句柄读写或关闭，不交出 PCB。位标志可能合并，数量从统计或累计 ACK 获得。 |

Raw recv 只有完成整条 pbuf 链复制后才释放它；空间不足返回 ERR_MEM，交给 lwIP 重新交付。发送每角色每轮最多尝试一次短搬运，20 ms 重试间隔，检查 sndbuf 和 sndqueuelen，同时受 lwIP ACK/tcp_poll 提示。2.2.1 正常关闭的 FIN 内存失败可能设置 TF_CLOSEPEND 并返回 ERR_OK，此时 adapter 立即丢弃 PCB 引用，由 lwIP 定时器重试；真正返回 ERR_MEM 时才保留该 PCB 并在截止前再试。tcp_err 仅清理本地上下文，不访问已释放 PCB；本地 abort 先摘回调。角色状态不承诺 TCP 应用消息边界；E5 测试协议在上层处理长度和 CRC。
