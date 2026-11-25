# 自有 Ethernet 源码审计索引

| 文件 | 行数 | 首行职责注释 | 项目内 include | 首个引用位置 |
|---|---:|---|---|---|
| CM7/Service/Ethernet/eth_http.c | 354 | 未置于文件顶部 | eth_http.h | 由工程、lwIP 或独立入口引用 |
| CM7/Service/Ethernet/eth_http.h | 58 | 未置于文件顶部 | eth_mjpeg.h, eth_transport.h | CM7/Service/Ethernet/eth_http.c:1 |
| CM7/Service/Ethernet/eth_mjpeg.c | 203 | 未置于文件顶部 | eth_mjpeg.h | 由工程、lwIP 或独立入口引用 |
| CM7/Service/Ethernet/eth_mjpeg.h | 112 | 未置于文件顶部 | eth_tcp.h | CM7/Service/Ethernet/eth_http.h:4 |
| Platform/Ethernet/arch/bpstruct.h | 2 | 有 |  | 由工程、lwIP 或独立入口引用 |
| Platform/Ethernet/arch/cc.h | 15 | 有 |  | 由工程、lwIP 或独立入口引用 |
| Platform/Ethernet/arch/epstruct.h | 1 | 未置于文件顶部 |  | 由工程、lwIP 或独立入口引用 |
| Platform/Ethernet/eth_board.h | 9 | 有 |  | Platform/Ethernet/eth_board_stm32h747.c:2 |
| Platform/Ethernet/eth_board_stm32h747.c | 55 | 有 | eth_board.h | 由工程、lwIP 或独立入口引用 |
| Platform/Ethernet/eth_dma.c | 246 | 有 | eth_dma.h | 由工程、lwIP 或独立入口引用 |
| Platform/Ethernet/eth_dma.h | 111 | 有 | eth_types.h | Platform/Ethernet/eth_dma.c:2 |
| Platform/Ethernet/eth_lwip_assert.c | 11 | 未置于文件顶部 | arch/cc.h | 由工程、lwIP 或独立入口引用 |
| Platform/Ethernet/eth_lwip_time.c | 8 | 未置于文件顶部 |  | 由工程、lwIP 或独立入口引用 |
| Platform/Ethernet/eth_mac.c | 579 | 有 | eth_mac.h, eth_phy.h, eth_board.h, eth_memory.h | 由工程、lwIP 或独立入口引用 |
| Platform/Ethernet/eth_mac.h | 99 | 有 | eth_types.h | Platform/Ethernet/eth_mac.c:2 |
| Platform/Ethernet/eth_memory.c | 80 | 有 | eth_memory.h | 由工程、lwIP 或独立入口引用 |
| Platform/Ethernet/eth_memory.h | 14 | 有 | eth_dma.h | Platform/Ethernet/eth_mac.c:5 |
| Platform/Ethernet/eth_netif.c | 120 | 有 | eth_netif.h | 由工程、lwIP 或独立入口引用 |
| Platform/Ethernet/eth_netif.h | 43 | 有 | eth_mac.h | Platform/Ethernet/eth_netif.c:2 |
| Platform/Ethernet/eth_network.c | 283 | 有 | eth_network.h | 由工程、lwIP 或独立入口引用 |
| Platform/Ethernet/eth_network.h | 88 | 有 | eth_netif.h | Platform/Ethernet/eth_network.c:2 |
| Platform/Ethernet/eth_phy.c | 76 | 有 | eth_phy.h | 由工程、lwIP 或独立入口引用 |
| Platform/Ethernet/eth_phy.h | 55 | 有 |  | Platform/Ethernet/eth_mac.c:3 |
| Platform/Ethernet/eth_tcp.c | 491 | 有 | eth_tcp.h | 由工程、lwIP 或独立入口引用 |
| Platform/Ethernet/eth_tcp.h | 112 | 未置于文件顶部 |  | CM7/Service/Ethernet/eth_mjpeg.h:4 |
| Platform/Ethernet/eth_transport.c | 110 | 未置于文件顶部 | eth_transport.h | 由工程、lwIP 或独立入口引用 |
| Platform/Ethernet/eth_transport.h | 33 | 未置于文件顶部 | eth_network.h, eth_tcp.h, eth_udp.h | CM7/Service/Ethernet/eth_http.h:5 |
| Platform/Ethernet/eth_types.h | 48 | 有 |  | Platform/Ethernet/eth_dma.h:5 |
| Platform/Ethernet/eth_udp.c | 122 | 未置于文件顶部 | eth_udp.h | 由工程、lwIP 或独立入口引用 |
| Platform/Ethernet/eth_udp.h | 23 | 未置于文件顶部 |  | Platform/Ethernet/eth_transport.h:6 |
| Platform/Ethernet/lwipopts.h | 68 | 有 |  | 由工程、lwIP 或独立入口引用 |

## 公开声明及文件行号

- `CM7/Service/Ethernet/eth_http.h:35` — `eth_http_init`
- `CM7/Service/Ethernet/eth_http.h:43` — `eth_http_poll`
- `CM7/Service/Ethernet/eth_http.h:46` — `eth_http_init_source`
- `CM7/Service/Ethernet/eth_http.h:53` — `eth_http_set_stream_fast_send`
- `CM7/Service/Ethernet/eth_http.h:56` — `eth_http_get_statistics`
- `CM7/Service/Ethernet/eth_mjpeg.h:91` — `eth_mjpeg_start`
- `CM7/Service/Ethernet/eth_mjpeg.h:101` — `eth_mjpeg_ack`
- `CM7/Service/Ethernet/eth_mjpeg.h:104` — `eth_mjpeg_poll`
- `CM7/Service/Ethernet/eth_mjpeg.h:107` — `eth_mjpeg_set_source`
- `CM7/Service/Ethernet/eth_mjpeg.h:110` — `eth_mjpeg_release_frame`
- `Platform/Ethernet/arch/cc.h:11` — `eth_lwip_assert`
- `Platform/Ethernet/eth_board.h:7` — `EthBoard_Prepare`
- `Platform/Ethernet/eth_dma.h:74` — `eth_ring_reset`
- `Platform/Ethernet/eth_dma.h:82` — `eth_ring_tx`
- `Platform/Ethernet/eth_dma.h:91` — `eth_ring_reclaim`
- `Platform/Ethernet/eth_dma.h:98` — `eth_ring_rx`
- `Platform/Ethernet/eth_mac.h:59` — `eth_mac_init`
- `Platform/Ethernet/eth_mac.h:61` — `eth_mac_start`
- `Platform/Ethernet/eth_mac.h:65` — `eth_mac_stop`
- `Platform/Ethernet/eth_mac.h:67` — `eth_mac_get_link`
- `Platform/Ethernet/eth_mac.h:69` — `eth_mac_get_address`
- `Platform/Ethernet/eth_mac.h:71` — `eth_mac_get_statistics`
- `Platform/Ethernet/eth_mac.h:73` — `eth_mac_tx_submit`
- `Platform/Ethernet/eth_mac.h:79` — `eth_mac_tx_reclaim`
- `Platform/Ethernet/eth_mac.h:81` — `eth_mac_rx_poll`
- `Platform/Ethernet/eth_mac.h:89` — `eth_mac_service`
- `Platform/Ethernet/eth_mac.h:92` — `ETH_IRQHandler`
- `Platform/Ethernet/eth_memory.h:11` — `eth_memory_configure`
- `Platform/Ethernet/eth_memory.h:13` — `eth_memory_valid`
- `Platform/Ethernet/eth_netif.h:40` — `eth_netif_init`
- `Platform/Ethernet/eth_netif.h:41` — `eth_netif_poll`
- `Platform/Ethernet/eth_network.h:80` — `eth_network_init`
- `Platform/Ethernet/eth_network.h:83` — `eth_network_init_address`
- `Platform/Ethernet/eth_network.h:86` — `eth_network_poll`
- `Platform/Ethernet/eth_phy.h:34` — `eth_phy_probe`
- `Platform/Ethernet/eth_phy.h:40` — `eth_phy_reset_begin`
- `Platform/Ethernet/eth_phy.h:42` — `eth_phy_reset_poll`
- `Platform/Ethernet/eth_phy.h:45` — `eth_phy_read_link`
- `Platform/Ethernet/eth_tcp.h:39` — `EthTcpEvent_t` 回调
- `Platform/Ethernet/eth_tcp.h:71` — `eth_tcp_configure`
- `Platform/Ethernet/eth_tcp.h:74` — `eth_tcp_server_start`
- `Platform/Ethernet/eth_tcp.h:77` — `eth_tcp_client_connect`
- `Platform/Ethernet/eth_tcp.h:80` — `eth_tcp_stop`
- `Platform/Ethernet/eth_tcp.h:83` — `eth_tcp_poll`
- `Platform/Ethernet/eth_tcp.h:86` — `eth_tcp_handle`
- `Platform/Ethernet/eth_tcp.h:89` — `eth_tcp_get_state`
- `Platform/Ethernet/eth_tcp.h:92` — `eth_tcp_enable_fast_send`
- `Platform/Ethernet/eth_tcp.h:95` — `eth_tcp_send`
- `Platform/Ethernet/eth_tcp.h:98` — `eth_tcp_read`
- `Platform/Ethernet/eth_tcp.h:101` — `eth_tcp_close`
- `Platform/Ethernet/eth_tcp.h:104` — `eth_tcp_abort`
- `Platform/Ethernet/eth_tcp.h:107` — `eth_tcp_get_statistics`
- `Platform/Ethernet/eth_tcp.h:110` — `eth_tcp_listening`
- `Platform/Ethernet/eth_transport.h:28` — `eth_transport_init`
- `Platform/Ethernet/eth_transport.h:29` — `eth_transport_poll`
- `Platform/Ethernet/eth_transport.h:30` — `eth_transport_ready`
- `Platform/Ethernet/eth_transport.h:31` — `eth_transport_get_statistics`
- `Platform/Ethernet/eth_udp.h:15` — `EthUdpReceive_t` 回调
- `Platform/Ethernet/eth_udp.h:18` — `eth_udp_start`
- `Platform/Ethernet/eth_udp.h:19` — `eth_udp_stop`
- `Platform/Ethernet/eth_udp.h:20` — `eth_udp_send`
- `Platform/Ethernet/eth_udp.h:21` — `eth_udp_get_statistics`
