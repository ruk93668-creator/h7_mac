# Ethernet 正式工程架构

## 层与依赖

| 层 | 正式模块 | 职责 |
| --- | --- | --- |
| Platform | eth_board_stm32h747、eth_phy、eth_mac、eth_dma、eth_memory | 板级时钟及引脚、PHY、MAC、描述符和 DMA 内存 |
| Platform | eth_netif、eth_network、eth_lwip_time、eth_lwip_assert | 网卡适配、静态 IPv4 与 DHCP、lwIP 轮询、时基及致命错误入口 |
| Platform | eth_tcp、eth_udp、eth_transport | Raw API 上的字节收发、连接状态、地址变化与传输编排 |
| Service | CM7/Service/Ethernet/eth_http、eth_mjpeg | HTTP 请求处理、MJPEG 响应和纯 JPEG Source 契约 |
| Application | 正式 CM7 现行业务入口 | 照常启动 Camera、USB、SD 等现有业务。Ethernet 运行接入待审核 |

HTTP 和 MJPEG 依赖 transport 与 JPEG Source。JPEG Source 只交付数据、长度、CRC 和租约。协议层的依赖闭包没有 OV5640、DCMI 或 Camera Capture。诊断帧源、V1 相机 BSP、E6 样片均只由独立测试构建选择。E5 至 E7 复用同一套传输及服务实现。

## 执行上下文和启动门禁

正式链接使用现有 lwIP 2.2.1 Raw API，配置为 NO_SYS=1。正式启动图中没有网络运行所有者。生产入口没有调用 Ethernet 网络初始化、transport 轮询或 HTTP，ETH IRQ 依旧由驱动启动路径使能。链接保留仅验证依赖闭包。

开放运行前，Application V2 需批准唯一的网络执行上下文。该上下文独占 Raw PCB、`eth_network_poll()`、`eth_transport_poll()` 和 `eth_http_poll()`，按上述顺序调用。`eth_network_poll()` 已调用 `sys_check_timeouts()`，因此需要持续驱动这个轮询。定时器、链接和传输事件回调在该上下文内运行。ETH ISR 只记录驱动事件。其他任务通过批准后的应用服务请求机制调用网络能力，具体 ABI 和任务归属待定。

停止边界需覆盖拒绝新请求、取消或排空连接、回收私有 JPEG 租约、使 netif 和 DHCP 退出、停止 MAC/DMA 并确认完成，再释放所有资源。当前网络模块持有单实例 `s_owner`，只有 Link Down 处理，缺少受控的完整销毁和重新初始化契约。正式错误上报需由网络所有者将驱动、网络和协议统计交给上层既有机制，上层事件映射待审批。现有模块的阻塞错误由本轮链接结果以外的运行验收关闭。

## Camera 和缓冲区所有权

- Camera Capture 持有原始 JPEG FIFO。E7 单一消费者短期取得完整帧，校验跨尾范围并复制、计算 CRC，立即尝试归还原帧。归还失败时冻结原租约并停止继续发布。
- E7 的私有 JPEG 由网络侧测试提供器持有。8 字节分段和段间 DSB 已用于实板对照；原相机 FIFO 和 DMA 算法沿用生产实现。
- E5 TCP 发送环拥有独立副本。最终 JPEG 分块成功提交至 TCP 环后，或流终止时，MJPEG 归还其私有租约。TCP 确认负责统计整帧完成，应用缓冲已通过同步复制脱离 DMA。
- 采集停止由现有 Media/BSP 的 StopFacts 和 DMA EN、DCMI CAPTURE/ENABLE 停止条件证明。若停止、启停回收或原帧归还失败，等待故障确认，资源继续由原所有者持有。未来跨任务 Frame Lease、停止交接以及在故障时的恢复责任均需审核。
