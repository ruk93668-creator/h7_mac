# N3 — 生产 Camera 消费者

正式 NetworkTask 使用独立上下行 FIFO，复用 Camera 原有的 per-consumer 交接状态机。
原 USB/Storage 的通道值、协议、实际释放行为保持；新通道为 8/9，复用的旧别名 6/7 不变。
Network 不调用 CameraCapture、DCMI 或 OV5640。FRAME_OFFER 有效期间将两段 JPEG 复制到网络私有 SDRAM。
FRAME_RETURN 仅交回凭据；CameraTask 实际释放后发 RETURN_ACK，Network 消费并回送 RETURN_ACK_CONSUMED。
只有真实归还后才发布私有副本，TCP 发送环复制完毕后归还私有租约；TCP ACK 用于整帧确认，不能代替源归还。

私有区 0xD07F0000..0xD07FF000，61440 字节，UNINIT，不由 DMA 使用。
8 字节分段复制及 CRC 提取为公共 Service 实现，独立 E7 同样使用，无复制的生产逻辑。
队满保留原事务，超时发送取消申请但不伪造释放，旧 Epoch/lease 不能退休当前租约。

Preflight 的生产 Service O0/O2 覆盖正常归还、上行队满、取消、停止竞态、旧 Epoch、
实际归还失败重试、非所有者拒绝、四服务组合、真实 DORA/T1 与浏览器 RST，及 100 轮软件启停。
这些是生产代码配合模拟 MMIO/网络对端的测试，不是硬件 PASS。
最终冻结源码的全构建/主机矩阵仍待执行，最终结论以 N4/FINAL_ACCEPTANCE 为准。
