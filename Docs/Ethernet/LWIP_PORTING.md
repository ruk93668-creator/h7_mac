# lwIP E4.1 移植说明

本阶段使用上游 lwIP 2.2.1 的 `NO_SYS=1` Raw API，启用 Ethernet、ARP、IPv4 和 ICMP，禁用 DHCP、IPv6、IGMP、TCP、UDP、Netconn、Socket、IPv4 重组和分片。只支持本阶段的非 VLAN MTU 1500。版本、SHA 和 [COPYING](../../ThirdParty/lwip/COPYING) 见 [第三方来源](../../ThirdParty/lwip/UPSTREAM.md)。

**执行上下文。** `eth_network_init()` 只注册一个默认 netif，先初始化 MAC/PHY 和 lwIP，通过 `netif_add(..., ethernet_input)` 注册输入，取驱动 MAC 地址，设置广播、ARP 和 Ethernet 标志与 `etharp_output`，并设置 administrative up。之后同一循环调用 `eth_network_poll()`。周期性检查驱动/PHY，先有有效协商结果再调用 Start，Start 成功才发布 netif link up。Service 检测掉线或速率/双工改变并按驱动契约停机，适配层发布 link down、停止发送及清理本接口 ARP 表；正常掉线回到 READY 后自动重试启动。驱动 FAULT 保持可见，不做隐式重启或重发。ETH ISR 不调用 lwIP；独立入口的 SysTick ISR 只更新 HAL 毫秒时钟。`sys_check_timeouts()` 在同一循环运行。测试固件持续 Poll，未加入可能延迟 RX 回收的睡眠。后续 V2 集成时由 Network Module 成为唯一执行者，其他模块通过消息或请求访问；本阶段未集成 V2。

**TX 所有权。** `linkoutput` 仅在 admin/link 都 up 时受理，校验 14–1514 字节，用 `pbuf_copy_partial()` 线性复制链条到上下文内 32 字节对齐帧缓冲。MAC 提交同步复制到驱动自己的非缓存槽，故临时缓冲可立即复用。lwIP 始终负责其 pbuf，不把 ticket、DMA 指针或 pbuf 交给对方保管。BUSY 对应 `ERR_MEM`，链路不可用对应 `ERR_RTE`，其它 MAC 失败对应 `ERR_IF`，长度或复制不足对应 `ERR_BUF`；不做应用级自动重试。每轮最多回收 4 次，成功提交和成功完成分别计数。

**RX 所有权。** 每轮最多尝试 4 次 Poll。MAC 先复制到 1536 字节临时区并归还内部 RX 槽；临时区足以消费驱动允许的超长帧，随后按本 netif 的 1514 字节边界拒绝，避免把超长帧留在环首。合法帧通过 `pbuf_alloc(PBUF_RAW, ..., PBUF_POOL)` 和 `pbuf_take()` 复制到可链式分配的 16×512 字节数据池。池耗尽则丢弃已交付的当前帧、增加 `rx_no_pbuf`，DMA 描述符不受影响。复制失败或输入返回非 OK 时适配层释放；输入返回 OK 时所有权已移交，即使其内部决定丢弃。应用不得把 DMA buffer 直接作为 pbuf。

**内存。** 独立入口初始化 HAL 和时钟，沿用 Region 4/5 配置并在开启 DCache 之前装载 MPU。Region 4 为 `0x24078000–0x2407FFFF` 的 32 KiB 非缓存 AXI SRAM 窗口，仍仅供 DMA 描述符与内部缓冲。Region 5 为 `0x24010000–0x2401FFFF` 的 64 KiB CPU 缓存窗口，放 lwIP heap（8 KiB 配置）、memp/PBUF_POOL 和 E4 测试上下文；窗口与 Region 4 不重叠。ARMCC 端对协议结构打包，主机端 MSVC 使用成对 pack pragma。正常数据路径不额外执行 cache clean/invalidate，也不关闭 cache。生产工程的 scatter 和 MPU 没有修改。

**构建与测试。** 在工程目录运行 `python Tests/Ethernet/build_firmware.py --e4`，打开生成的独立 Keil 工程 `MDK-ARM/h7_mac_ethernet_e4.uvprojx`，固件为 `output/Ethernet/e4/eth_e4.axf`。不要打开生产工程后误下载其目标。脚本只生成和编译，不烧录。主机验证在 x64 Visual Studio 开发者命令行执行 `python -B Tests/Ethernet/run_e4_host.py`，使用相同 lwIP C 源和适配层，仅替换 MAC 接口和毫秒时钟。目标编译、主机结果和现场步骤见 [E4.1 实施报告](E4_1_IMPLEMENTATION.md)。
