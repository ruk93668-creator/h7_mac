# E6 链接内存审计

ARMCC 地图的 Region 5，执行基址 `0x24010000`，容量 65,536 B，实际使用 **46,516 B**，剩余 **19,020 B**。验收基线 E5 使用 50,776 B，因此 E6 减少 4,260 B，达到至少保留 8,192 B 的要求。E6 使用 HTTP/MJPEG 上下文替换 E5 压测应用缓冲，复用原 TCP 环和池。

| 对象 | 链接使用 |
| --- | ---: |
| HTTP `s_http`（请求 512 B、响应 1024 B、MJPEG 状态、统计、配置和对齐） | 1,900 B RAM |
| HTTP + MJPEG 对象 Code、RO 合计 | 3,923 B ROM |
| 八张 JPEG 有效载荷 | 48,814 B Flash |
| JPEG 目标文件含描述符及链接对齐 | 48,912 B Flash |
| 固件 Code / RO / RW / ZI | 69,886 / 51,434 / 332 / 64,344 B |
| 固件 ROM（Code + RO + RW） | 121,652 B |
| 固件静态 RAM（RW + ZI） | 64,676 B |

八个 `e6_jpeg_*` 符号和夹具表由 Keil 放入 Flash。构建脚本核对每个 JPEG 的地址和数量。Region 4 内 `g_eth_dma_memory` 仍在 `0x24078000`、大小 12,544 B。Region 5 MPU 属性与 E5 相同，初始化顺序保留 DCache 启用前的 MPU 配置。相比已审核 E5 固件，ROM 增量 52,460 B，静态 RAM 减少 4,260 B。HTTP 响应构造另用 512 B 临时栈页，独立 CM7 栈预留 4096 B，目标栈峰值待实板测量。
