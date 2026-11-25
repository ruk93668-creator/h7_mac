# 正式 Keil 构建接入

## 工程和配置

`MDK-ARM/h7_mac.uvprojx` 的现有 `h7_mac_CM7` 仍以正式 `CM7/Core/Src/main.c` 为入口。新增三组源文件：

1. Ethernet lwIP port。Netif、Network、TCP、UDP、Transport、`sys_now()` 时基和生产断言适配。
2. Ethernet Service。HTTP、MJPEG 和头文件搜索路径。
3. Ethernet lwIP 2.2.1。现有上游 core、IPv4、ARP、ICMP、DHCP、UDP、TCP 与 Ethernet netif。

CM7 继续编译原 Ethernet 底层组。新增 `ETH_LWIP_DHCP` 和 `ETH_LWIP_E5` 选择现有协议配置。Keil 链接保留项覆盖 network、transport、HTTP、UDP 发出等入口，使链接器验证可达实现。生产断言映射到现有 `Error_Handler()`。嵌入式正式目标和 E4–E7 使用同一份 `sys_now()` 实现，Host 仍使用模拟时基。

独立工程生成器移除正式协议分组、正式断言、正式保留项和正式协议配置，按测试目标选择适用配置及入口。E6 文件清单只在 E6/Host 验证含静态样片。E7 构建继续选择相机测试适配、指定 V1 BSP、测试私有缓冲及诊断开关。CM4 目标沿用原工程定义，已与基线 XML 逐项比较。

## 内存、MPU 和启动

| 区域 | 正式链接地址及上限 | 本轮接入 |
| --- | --- | --- |
| SDRAM Region 2 | 0xD0000000，8 MiB | 正式 Camera 等原用途与属性 |
| SDMMC DMA | 0x24000000..0x24008000 | 原 scatter 与 MPU 契约 |
| Ethernet DMA Region 4 | 0x24078000..0x24080000 | 原非缓存区，描述符和数据 12544 B |
| Ethernet CPU 预留 | 0x24010000..0x24020000 | 现有 E7 Region 5 窗口中的正式链接区；保存 lwIP 和协议静态状态，链接器检查容量 |
| JPEG 私有缓冲 | 无正式分配 | E7 原 SDRAM 或 AXI 诊断分区仅用于独立目标 |
| IPC / DTCM / Flash | 原 scatter 限制 | 按实际 MAP 审核 |

生产启动没有配置 Region 5。它当前是 scatter 预留，测试属性在 E4–E7 独立入口内。生产 Region 2/4、SD、IPC 和 DCache 配置沿用原代码；未在正式 CM7 源码中找到显式启用 DCache 的调用，板上 CCR 和 MPU 读回待验证。正式运行批准前需在正式上下文核对 CPU 区的 MPU 和 DCache 属性。正式静态数据经现有 scatter 加载，新增模块没有 HAL 初始化、Initcall 或 Device Registry 注册。IRQ 向量使用现有 Ethernet ISR。

## 复验方式

Keil 分别对 `h7_mac_CM7`、`h7_mac_CM4` 执行全量编译。独立测试使用 `python -B Tests/Ethernet/build_firmware.py --e5`，并分别替换为 `--e6`、`--e7` 和 `--e7-v1-qvga`。本轮验证的 Keil 临时项目保持正式目标配置，只重定向输出，历史工件和 Keil RTE 用户内容保留。

## 编译与 MAP 实测

| 目标 | 结果 | Error / Warning | Code / RO / RW / ZI（B） |
| --- | --- | --- | --- |
| 正式 CM7 原基线 | PASS | 0 / 0 | 137998/3114/1396/113484 |
| 正式 CM7 当前集成 | PASS | 0 / 0 | 200778/4894/1636/156716 |
| 正式 CM4 | PASS | 0 / 0 | 26302/1438/936/3576 |
| E4.1 独立目标 | PASS | 0 / 0 | 26842/1110/156/38388 |
| E4.2 独立目标 | PASS | 0 / 0 | 38030/1242/212/38720 |
| E5 独立目标 | PASS | 0 / 0 | 67546/1614/348/68588 |
| E6 独立目标 | PASS | 0 / 0 | 70262/51434/332/64380 |
| E7 原独立目标 | PASS | 0 / 0 | 109938/4154/516/127968 |
| E7 V1 QVGA | PASS | 0 / 0 | 112014/4154/520/128052 |
| E3.5 原始帧补充编译 | PASS | 0 / 0 | 68398/2862/37096/81824 |

正式 CM7 实际 Flash 装载从 142508 B 到 207308 B，增加 64800 B。编译统计的静态 RAM 增加 43472 B，其中 Ethernet CPU 区实际 43232 B，剩余 22304 B。正式 DTCM 执行区使用 69488 B。SDMMC 32768 B、Ethernet DMA 12544 B、IPC 320 B 的实际区域占用与基线一致。

正式 CM7 基线与候选 MAP 中，main 与启动对象的引用集合一致，Initcall 192 B 和 Device Export 4 B 的贡献对象一致。HAL_Init、Framework_InitRuntime、Device_RegisterAll、ETH_IRQHandler 与核对的协议入口各有一个最终符号。RAM heap、PBUF 池、HTTP 与 TCP 状态在 CPU 窗口内。硬件功能回归为 NOT_RUN。

初轮正式全量构建出现 6 条 L6314W，来自三个没有 RW/ZI 的对象在 Scatter 中的选择器。移除选择器后全量编译达到 0 错误、0 警告。

正式候选 AXF SHA-256 为 `550a8cd0bce14f565fb0fa6c5be78a214e9c4555afd91f6807c8891aa83f4118`。

## 正式 Ethernet 源码清单

**Ethernet independent driver**

- `Platform/Ethernet/eth_phy.c`
- `Platform/Ethernet/eth_dma.c`
- `Platform/Ethernet/eth_memory.c`
- `Platform/Ethernet/eth_mac.c`
- `Platform/Ethernet/eth_board_stm32h747.c`

**Ethernet lwIP port**

- `Platform/Ethernet/eth_netif.c`
- `Platform/Ethernet/eth_network.c`
- `Platform/Ethernet/eth_udp.c`
- `Platform/Ethernet/eth_tcp.c`
- `Platform/Ethernet/eth_transport.c`
- `Platform/Ethernet/eth_lwip_time.c`
- `Platform/Ethernet/eth_lwip_assert.c`

**Ethernet Service**

- `CM7/Service/Ethernet/eth_http.c`
- `CM7/Service/Ethernet/eth_mjpeg.c`

**Ethernet lwIP 2.2.1**

- `ThirdParty/lwip/src/core/def.c`
- `ThirdParty/lwip/src/core/init.c`
- `ThirdParty/lwip/src/core/inet_chksum.c`
- `ThirdParty/lwip/src/core/ip.c`
- `ThirdParty/lwip/src/core/mem.c`
- `ThirdParty/lwip/src/core/memp.c`
- `ThirdParty/lwip/src/core/netif.c`
- `ThirdParty/lwip/src/core/pbuf.c`
- `ThirdParty/lwip/src/core/raw.c`
- `ThirdParty/lwip/src/core/stats.c`
- `ThirdParty/lwip/src/core/sys.c`
- `ThirdParty/lwip/src/core/timeouts.c`
- `ThirdParty/lwip/src/core/ipv4/etharp.c`
- `ThirdParty/lwip/src/core/ipv4/icmp.c`
- `ThirdParty/lwip/src/core/ipv4/ip4.c`
- `ThirdParty/lwip/src/core/ipv4/ip4_addr.c`
- `ThirdParty/lwip/src/netif/ethernet.c`
- `ThirdParty/lwip/src/core/udp.c`
- `ThirdParty/lwip/src/core/ipv4/dhcp.c`
- `ThirdParty/lwip/src/core/ipv4/acd.c`
- `ThirdParty/lwip/src/core/tcp.c`
- `ThirdParty/lwip/src/core/tcp_in.c`
- `ThirdParty/lwip/src/core/tcp_out.c`

平台接口头文件和 lwIP 配置所在目录为 `Platform/Ethernet`。Service 接口位于 `CM7/Service/Ethernet`。Keil 编译、MAP、独立目标 JSON 和完整链接审计保存在 `output/Ethernet/merge_readiness`。
