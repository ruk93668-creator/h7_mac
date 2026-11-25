# 正式硬件验收准备状态

NOT_RUN。尚无 N1–N4 通过的软件及新正式固件，本文件仅记录待形成验收包的项目。

软件通过后应提供 JPEG、Raw、CM4 的 AXF/HEX/MAP 和 SHA、对应源码 SHA，以及配置说明。用户手动下载并使用 Verify 或 Flash Readback 核对板上身份。读取 PHY ID、Link 速率与双工、DMA 实际地址、MAC 状态、MPU Region 4 和 CCR.DC。保留完整数值和时间。

配置 PC 与固件的相同静态网段，先 ARP/Ping 1000 次，再验证 TCP Server、Client、UDP、HTTP。DHCP 测试使用真实 DHCP Server。JPEG 模式检查完整 JPEG 数据和 CRC，Raw 模式仅测试可用网络功能。执行拔插线、短租约续租、刷新和重连并保存 PCAPNG。

依据生产生命周期请求入口连续执行至少 100 轮 START/STOP/RESTART。每轮读取原事务结果、DMA 停止、Camera 租约、PCB/pbuf/heap、队列与栈水位。旧资源未退休时停止继续重启并保存诊断。

按格式合法性测试 Camera+Network、USB+Network、Storage+Network 及三消费者组合，CM4 OLED/IPC 全程运行。建议联合长稳至少 60 分钟，记录各路帧率、CRC、流量、续租、内存峰值、任务栈、CPU 和两类 DMA 错误。历史 RX 零计数、RBU、JPEG 损坏、DCMI FIFO 和 20 FPS 未达标继续单独观察，不能凭模型结果关闭。

所有上述实板项目为 NOT_RUN。本轮没有烧录或改动板上数据。
