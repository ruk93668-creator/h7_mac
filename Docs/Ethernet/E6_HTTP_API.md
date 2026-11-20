# E6 HTTP 接口

`eth_http_init(network, frames, frame_count, frame_period_ms)` 在网络初始化后调用，固定 TCP 80，注册 E5 代次回调。帧表和数据应在服务存续期间保持有效。周期允许 50 至 5000 ms，独立固件默认 200 ms。`eth_http_poll()` 由唯一的网络调用者在 network 和 transport 轮询之后调用。`eth_http_get_statistics()` 复制统计。

| 请求 | 响应 |
| --- | --- |
| `GET /` | `200`，内置 HTML、当前 IPv4、已连接状态和 MJPEG `<img>`。favicon 使用 data URI。 |
| `GET /stream.mjpg` | `200`，Multipart MJPEG。 |
| `GET /health` | `200`，正文 `ok` 加换行。 |
| `GET /favicon.ico` | `204`，空正文。 |
| 其他有效路径 | `404`。 |
| 合法但不支持的方法 | `405`，`Allow: GET`。 |
| 非法请求行、路径或字段 | `400`。 |
| 5 秒内头部未到齐 | `408`。 |
| 512 B 仍未读到头结束符 | `431`。 |

支持 HTTP/1.0 和 HTTP/1.1 的 origin-form 路径。允许 ASCII 字母、数字、`/._-`，路径精确匹配。每条 TCP 连接处理第一个请求。额外到达的流水线字节被有界读取并丢弃，连接按 `Connection: close` 关闭。有限响应带精确 `Content-Length`，204 无正文。请求体、TLS、Keep-Alive 和通用代理均超出测试服务器范围。
