# E6 MJPEG 协议

`GET /stream.mjpg` 返回 `multipart/x-mixed-replace; boundary=h7frame`，设置 `Cache-Control: no-cache, no-store`、`Pragma: no-cache` 和 `Connection: close`。整个流无 `Content-Length`，每帧单独声明长度。

```text
--h7frame\r\n
Content-Type: image/jpeg\r\n
Content-Length: <JPEG字节数>\r\n
X-Frame-Sequence: <十进制序号>\r\n
X-Frame-CRC32: <八位十六进制CRC32>\r\n
\r\n
<完整 JPEG，FFD8 开头、FFD9 结尾>\r\n
```

序号从 1 开始，相邻完整帧严格递增。原始 JPEG 数据循环轮换，CRC32 覆盖 JPEG 字节。每轮最多复制提交 1024 B。E5 返回 `ERR_MEM` 时本次不转移所有权，偏移保持，至少 10 ms 后再尝试。只有 `ERR_OK` 才推进偏移。上一帧 trailer 的累计 ACK 到达后才调度下一帧。调度周期错过时只计 `frame_skipped`，保持序号连续。15 秒无发送或 ACK 进展且仍有待发送数据时终止连接。

`frames_submitted` 统计完整帧已提交 E5 环。`frames_completed` 统计 TCP 对端 ACK 已覆盖 trailer。`current_fps_milli` 与 `max_send_latency_ms` 基于 ACK，浏览器解码与呈现需要单独验证。收到 FIN 时停止提交本帧未发的后缀。已经在 E5 队列中的字节按既有规则排空，最后一个非完整 part 随连接结束，后续连接从新帧开始。
