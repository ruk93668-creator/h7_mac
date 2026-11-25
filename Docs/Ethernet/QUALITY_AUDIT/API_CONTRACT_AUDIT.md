# Q5 接口设计

结论 **NEEDS_FIX**。逐声明索引见 source-index.md。以下按使用边界解释返回值、调用者和完成条件。

| 接口组 | 调用者与所有权 | 异步、错误与取消 |
| --- | --- | --- |
| PHY / Board / Memory | MAC 持有 PHY/MDIO 配置；启动负责 MPU；调用者不借用寄存器/DMA 内存 | PHY reset begin/poll 有 BUSY/超时；MAC 初始化检查内存；板级 Prepare 无错误返回 |
| Ring / MAC | Netif 或 raw bench；TX submit 复制进入内部缓冲并返回 ticket；RX Poll 拷贝到调用者缓冲 | TX 完成靠 reclaim，对端是否收到由上层确认；Stop/状态/统计用于失败恢复，串行调用，IRQ 仅锁存事件 |
| Netif / Network | 同一执行者持有长生命周期 netif；RX pbuf 成功交给 lwIP，拒绝时释放；TX pbuf 链复制进 staging | Poll 有 RX budget 并驱动定时器，DHCP 分离启动与有效租约；状态读和 stop 跨任务契约未形成 |
| UDP | Transport/start 绑定端口与回调；Send 自建 pbuf、提交后释放；回调数据仅在回调内使用 | ERR_MEM/路由/参数反馈，stop 移除 PCB。借用期需在头文件明确 |
| TCP | Transport/HTTP 按角色和 generation 访问；Send 成功复制进应用队列，随后 tcp_write(COPY) 转交；Read 释放窗口 | ERR_MEM 可重试；ACK 统计表示远端累计确认；Close 排空、Abort 丢弃；过期 handle 拒绝 |
| Transport / HTTP | 接收已初始化且常驻的 Network；HTTP 负责请求超时及连接事件；静态帧数组需常驻，动态源上下文须有效 | Link 或地址变化触发 PCB 停止；生命周期只适合单实例单次初始化，缺正式服务停止与重启契约 |
| MJPEG / E7 Provider | E7 借用可能跨 FIFO 尾部的 Camera Frame，复制并校验后归还原帧。私有 JPEG 由 provider 按 lease 管理，成功提交最后一段后可归还 | TCP ACK 决定整帧完成统计和下帧节奏。断链、FIN、超时经 HTTP 清理；Stop 失败保留借用/隔离事实，未经证明不得让 DMA 复用。 |

重点问题 ETH-Q01、Q03、Q04、Q05。已审独立 E7 的 8 字节分段读取路径，实测边界见原 E7 证据。

## 逐接口结论

源文件行号见 [声明索引](source-index.md)。除了向量入口，以下状态接口依赖相应模块的串行拥有者。统计输出参数不产生借用。

- **Board/PHY**。EthBoard_Prepare 由 MAC 同步配置 RMII 和外设时钟，void 无失败反馈。eth_phy_probe 只读板级地址并核对 ID。eth_phy_reset_begin 发出软件复位，eth_phy_reset_poll 报告 WAIT 或完成，驱动调用者拥有时间预算。eth_phy_read_link 双读状态，只有成功时使用输出。MDIO 回调参数须跟随 PHY 对象常驻。
- **Memory/Ring**。eth_memory_configure 在启动期设置非缓存窗口，eth_memory_valid 读回地址与属性。eth_ring_reset 要求硬件已停且 DMA 内存对齐、可达，复制 ops 但借用其上下文。eth_ring_tx 同步复制并返回 ticket。eth_ring_reclaim 仅按序回收，EMPTY 无完成。eth_ring_rx 将内容交给调用者缓冲，空间不足保留帧，错误帧消费后归还槽位。
- **MAC**。eth_mac_init 复制地址并初始化硬件状态。eth_mac_start 完成协商检查后启动 DMA，eth_mac_stop 等待并核验硬件可停止，失败需保留故障。eth_mac_get_link、eth_mac_get_address、eth_mac_get_statistics 使用调用者输出。eth_mac_tx_submit、eth_mac_tx_reclaim、eth_mac_rx_poll 分别用于复制提交、按 ticket 回收和有界复制接收。eth_mac_service 推进状态与恢复。ETH_IRQHandler 只供向量表使用，不能代替 Service。
- **Netif/Network**。eth_netif_init 由 netif_add 调用并设置接口方法，eth_netif_poll 在预算范围内处理 RX 再回收 TX。eth_network_init 为静态地址便捷入口，eth_network_init_address 校验模式、超时和 fallback，拒绝再次占用唯一接口。eth_network_poll 只推进当前所有者，调用者须持续运行它来驱动超时。
- **UDP**。eth_udp_start 绑定 netif/端口/回调，参数无效、PCB 不足和 bind 失败各有返回值。EthUdpReceive_t 收到的地址和数据只在回调期间有效，返回错误仅统计，本包仍释放。eth_udp_send 同步复制，ERR_OK 表示本地接纳。eth_udp_stop 移除 PCB，eth_udp_get_statistics 要求非空且没有显式保护，见 ETH-Q11。
- **TCP**。eth_tcp_configure 注册事件回调和 idle 时限。eth_tcp_server_start 完成 listen 绑定。eth_tcp_client_connect 成功只表示发起连接。eth_tcp_stop 停止两个 role 和 listener，eth_tcp_poll 推进队列、超时及 EthTcpEvent_t 通知。eth_tcp_handle 返回 role/generation，eth_tcp_get_state 检查句柄有效性。eth_tcp_enable_fast_send 是逐连接实验调节。eth_tcp_send 复制写入队列，eth_tcp_read 复制取出并推进窗口。eth_tcp_close 发起排空，eth_tcp_abort 立即终止，关闭事件作为应用层完成信号。eth_tcp_get_statistics 读角色快照，eth_tcp_listening 读监听状态。
- **Transport**。eth_transport_init 配置单例和回调，借用已初始化的 Network。eth_transport_poll 随 Link/IP 启停端点。eth_transport_ready 表示存在可用链路和地址，监听以及连接是否成功应读 eth_transport_get_statistics。
- **HTTP**。eth_http_init 借用静态 frame 数组，eth_http_init_source 复制回调表但长期借用 context，两者均验证配置。eth_http_poll 驱动请求、超时、响应及关闭状态。eth_http_set_stream_fast_send 影响后续 stream。eth_http_get_statistics 返回统计快照。正式显式 Stop 契约缺失。
- **MJPEG**。eth_mjpeg_start 初始化一轮响应并保存 ACK 基线，其 stream 必须先零初始化或处于已知状态。eth_mjpeg_ack 推进累计确认及完成统计。eth_mjpeg_poll 按偏移分段提交，在 ERR_MEM 时重试。eth_mjpeg_set_source 借用回调表和上下文。eth_mjpeg_release_frame 只归还当前持有租约，不表示 ACK 已完成。
- **lwIP 适配**。eth_lwip_assert 是断言报告通道，正式构建交给 Error_Handler。sys_now 是 lwIP 读取 HAL 毫秒 tick 的回调，独立主机测试由受控时钟替代。
