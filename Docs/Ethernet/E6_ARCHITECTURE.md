# E6 HTTP/MJPEG 架构

板级初始化复用 E5 独立固件的 CM7 时钟、MPU 和 DCache 配置。调试入口 `e6_main.c` 以 DHCP Only 启动，也可在初始化前选择静态 IPv4。以 `NO_SYS=1` 和 lwIP 2.2.1 Raw API 运行。

```text
CM7 单循环
  eth_network_poll     PHY / MAC / netif / DHCP / lwIP 定时器
  eth_transport_poll   有效地址门控 / HTTP listener / TCP 回收和事件
  eth_http_poll        按代次管理一个会话 / 512 B 请求头 / 响应
    eth_mjpeg_poll     分帧 / 每轮最多提交 1024 B / ACK 门控
```

HTTP 通过 E5 的 `eth_tcp_*` API 读写及关闭连接。lwIP PCB、发送环、pbuf 和 DMA 描述符由现有层管理。TCP 80 监听只允许一个活动连接，首页关闭后浏览器建立视频连接。首页内联处理最多三次初次图片加载失败，处理浏览器取图时上一连接仍在排空的情况。网络栈禁止在 ISR 中调用。应用 UDP 和主动 TCP Client 均关闭，DHCP 仍可持有自己的 UDP PCB。

代次与状态绑定：OPEN 从空请求开始，FIN 后停止新的 MJPEG 提交并由 E5 排空，RST/CLOSED 清除会话。代次不一致的通知被过滤。TCP 传输层在失去链路、IP 清空或有效 IP 改变时移除连接和监听器。网络栈重新可用后，E5 重新打开监听器，新播放从序号 1 开始。

E6 使用 8 张 Flash 中的原创 JPEG。固件没有摄像头采集或编码路径。
