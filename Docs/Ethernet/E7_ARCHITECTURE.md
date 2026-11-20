# E7 真实 JPEG 网页视频架构

独立 CM7 固件沿用 NO_SYS 单循环：CameraCapture 处理 → Ethernet/lwIP 轮询 → E5 连接及重绑处理 → HTTP/MJPEG 轮询。OV5640 输出 160×120 JPEG，DCMI/DMA1 按已有代码写 SDRAM FIFO。取到 READY 时复制到 Network 私有缓冲，建立逐帧长度、CRC32 和传输序号，再分块送入 E5。HTTP 首页、健康检查和浏览器访问地址与 E6 相同。

E6 静态帧表入口保留。动态源 acquire 分别返回取得、暂无、无效。网络租约 ID 单调增长，跨相机重启也继续计数，耗尽后拒绝新租约。E5 连接代次隔离旧会话。无帧时只等待下一周期。连接失败、RST、FIN 和网络下线释放帧；帧的最终 JPEG 分块复制提交后也释放，TCP ACK 仍只负责标识完整 multipart part 的确认。

Camera 采集 FPS、私有复制 FPS、HTTP 提交 FPS 和 TCP 确认 FPS 分开统计。200 ms 是 HTTP 调度目标。CameraCapture_Process 即使没有浏览器或 TCP 队列满也由主循环持续调用。
