# STM32H747 Ethernet E5 实施与测试报告

开发分支 `codex/ethernet-bringup`。开始时本地和远端均为 `125e30587119fb23ac8d0043f39c8273d48c8abd`。检查了起始 Git Diff，保留用户的 `MDK-ARM/RTE/_h7_mac_CM7/RTE_Components.h` 本地修改。其 SHA-256 为 `0e4f69cf9d4bbac5027e7edf287b4232e96b65ba13815608fdd4b7f195efb508`。

## 交付与复现

- E5 使用 lwIP 2.2.1 Raw API 和 `NO_SYS=1`，一个 UDP 应用 PCB、一个 TCP listener、两个 TCP 活动角色与既有 DHCP 共存。TCP 端口 5003/5004，UDP 端口 5001/5002。链路或地址变化时释放应用传输资源，重新就绪后重绑并按策略重连。
- 在仓库根目录运行 `python -B Tests/Ethernet/build_firmware.py --e5` 生成 `MDK-ARM/h7_mac_ethernet_e5.uvprojx` 和 `output/Ethernet/e5/eth_e5.axf`。固件 MAP 位于同一目录。`output/Ethernet/e5_result.json` 记录构建时的源码提交、逐文件 SHA-256、固件 SHA-256 和链接检查结果。详见[内存预算](E5_MEMORY_BUDGET.md)、[架构](E5_ARCHITECTURE.md)、[API 契约](E5_API_CONTRACT.md)及[实板流程](E5_HARDWARE_VALIDATION.md)。
- 本轮 AXF 构建源提交为 `05d2998cdc420068d1fcc823b400dcce772b7a5d`，本机 AXF 的 SHA-256 为 `97f3e13214b57f34811166752a665f91d00adccb01ece88bfc7a4b895b4d083e`。板上镜像身份在人工下载验证时记录。
- 用 MSVC x64 Developer Command Prompt 运行 `python -B Tests/Ethernet/run_e5_host.py`。PC 工具使用 Python 标准库，运行 `python -B Tests/Ethernet/test_e5_peer.py -v` 验证。
- 上游提交 `77dcd25a72509eb83f72b033d219b1d40cd8eb95`。核对了 190 个导入文件与固定上游 tar.gz，逐字节一致。仓库自有的 `UPSTREAM.md` 记录来源和许可证；完整 C 源码编译列表由测试构建脚本维护。

## 实测结果

| 验证 | 状态 | 证据 |
| --- | --- | --- |
| 原 CM7 完整构建 | PASS | 0 Error / 0 Warning。Code 137,998 B、RO 3,114 B、RW 1,396 B、ZI 113,484 B。 |
| E5 独立 CM7 完整构建及 MAP | PASS | 0 Error / 0 Warning。Region 5 余 14,760 B，`g_eth_dma_memory` 仍位于 `0x24078000`。 |
| E4.1/E4.2 现有主机回归 | PASS | 运行两套现有测试，静态 ARP/ICMP、pbuf 错误路径、DHCP 地址生命周期及回退等断言通过。 |
| 真实 lwIP 并发与 UDP | PASS | DHCP DORA、两条 TCP 三次握手、第二个 Server 会话拒绝；活动期间核对 UDP 单播、广播、双向 Echo、1/16/64/256/512/1200/1472 B、1473 B 拒绝、重复初始化和 PCB 耗尽。 |
| TCP 数据及背压 | PASS | 两个角色均完成拆分/合并 Echo，链式 pbuf 和 2920 B 发送/接收，窗口收缩和重开，发送环满，延迟 ACK、重传及实际 TCP segment/heap/PBUF_POOL 资源耗尽后恢复。 |
| 关闭、错误和重连 | PASS | FIN、RST、FIN 分配失败进入 CLOSEPEND 后重试、超过 PCB 容量的 TIME_WAIT 轮换、连接拒绝、连接超时、PCB/定时器回收、掉线后重新获得 DHCP 租约和重开服务。 |
| PC 测试工具 | PASS | 3 项测试，含本机并行 TCP Server、TCP Client 和 UDP 三个真实 Socket 通道，每通道 20 个已验证 Echo；另测流帧拆分/合并、CRC 和序号统计。 |
| 板端 Flash Verify、DCache/MPU 及池峰值 | NOT_RUN | 需人工确认实际板上镜像并保存调试器读回。 |
| 实板静态 IPv4 三通道短测 | PASS | 30 秒，UDP 及两条 TCP 各 280/280 个应用 Echo，丢失或未确认均为 0。见实板验收报告及本地 PCAP、JSON。 |
| 实板 DHCP 并发、10 分钟并发及性能上限 | NOT_RUN | 待采集对应 PCAP、JSON、运行峰值及恢复证据。 |

主机故障注入峰值为 lwIP heap 12,240 B、Timeout 7、TCP PCB 6、TCP Listen PCB 1、TCP segment 24、UDP PCB 2、PBUF_POOL 24。上述峰值包含人为占满各池的用例。板端自然流量下的峰值应在人工验收时单独采集。

生产入口、MAC/DMA、MPU 配置、RTOS、USB、Storage、CM4、Application V2 和 `main` 分支均保留。E5 实板验收仍是进入后续阶段前的待办项目。
