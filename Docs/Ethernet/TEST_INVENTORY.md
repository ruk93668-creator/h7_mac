# Ethernet 验证和证据清单

| 范围 | 测试源码与工具 | 历史证据 |
| --- | --- | --- |
| E4.1 | e4_main.c、e4.sct、test_e4_network.c、run_e4_host.py | [静态 IPv4、ARP 和 ICMP](E4_1_IMPLEMENTATION.md) |
| E4.2 | 同一 E4 入口与 Host 程序的 DHCP 模式、e42_lease_peer.py、test_e42_lease_peer.py | [DHCP 与租约](E4_2_IMPLEMENTATION.md) |
| E5 | e5_main.c、e5.sct、e5_bench.c/h、e5_protocol.c/h、eth_test_board.c/h、test_e5_network.c、run_e5_host.py、e5_peer.py、e5_lease_peer.py 及同名前缀 PC 测试 | [TCP/UDP 实板](E5_HARDWARE_VALIDATION.md) |
| E6 | e6_main.c、e6.sct、test_e6_network.c、run_e6_host.py、e6_peer.py、test_e6_peer.py、e6_jpeg_fixtures/、generate_e6_fixtures.py | [HTTP/MJPEG 实板记录](E6_HARDWARE_VALIDATION.md) |
| E7 | e7_main.c、e7.sct、e7_camera_provider.c/h、board_v1/、test_e7_camera_provider.c、run_e6_host.py 动态与 QVGA 模式 | [E7 复验步骤](E7_HARDWARE_VALIDATION.md) · [V1 实测与诊断](E7_V1_ADAPTATION.md) |
| 公共驱动早期 | test_phy.c、test_mac.c、test_dma.c、test_engine.c、test_mdio_clock.py、run_host.cmd、E3.5 专用入口及对端工具 | [E3.5 证据](E3_5_VALIDATION.md) |

上述源码路径位于 `Tests/Ethernet`。独立工程的生成逻辑在 build_firmware.py，源清单在 e4_sources.py。历史 JSON、PCAP、夹具和工程记录保留。

E7 已有的实板记录中，320×240 采集约 31 fps，快速提交版 9.33 fps，55 帧 CRC 匹配。该结果来自短测。实板上有 DMA FIFO 复现与 8 字节分段读取对照，相关证据见 V1 记录。本轮正式工程与重新编译的独立固件没有烧录到板上。

## 本轮 Host 回归

| 项目 | 结果 | 覆盖 |
| --- | --- | --- |
| E4.1 | PASS | 真实 lwIP、ARP/ICMP、pbuf、链路和计时 |
| E4.2 | PASS | DORA、NAK、ACD、T1/T2、过期、回退和资源耗尽 |
| E5 | PASS | TCP 双角色、UDP、零窗口、分段合并、资源回收和重连 |
| E6/E7 | PASS | 静态样片、动态源、快速提交三个路径通过，虚拟长测及 QVGA Provider |
| PC 工具 | PASS | DHCP、E5 对端和 E6 对端四组，共 10 个测试 |
| 正式 CM7 及本轮重新构建固件的实板回归 | NOT_RUN | 保留既有证据，待人工安排 |

历史 E4.1 有 1000 次 Ping 通过。E4.2 已记录 DHCP、回退、续租和租约到期。E5 的 DHCP 并发数据时段为 619.836 秒。独立 E6 板测仍按原报告标记 NOT_RUN。E7 的 9.33 fps 是 QVGA 短测数据，与 E6 的板上验收分别记录。

本轮完整 Host 日志保存在 `output/Ethernet/merge_readiness`，构建结果见 [BUILD_INTEGRATION.md](BUILD_INTEGRATION.md)。
