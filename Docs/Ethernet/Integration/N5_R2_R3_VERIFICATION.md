# N5-R2/R3 联合验收报告

**READY_FOR_REVIEW**。软件候选为 5768c9f9afaf248cdcf4e1d155dce11c69a71b2c，起点为 R1 1b1035aa45394d108c8953968df671beea99c4a6。构建和主机回归在该软件提交的干净工作区执行。交付提交另外归档报告和证据。实板为 **NOT_RUN**，历史硬件 MDIO 触发原因为 **NOT_PROVEN**。

## 变更范围

| 文件 | 函数与变更 |
| --- | --- |
| Platform/Ethernet/eth_mac.c、eth_mac.h、eth_types.h | EthMac_AccessPhyRegister 首次现场，eth_mac_get_link 区分读失败和 PHY 状态，eth_mac_service 保持瞬时失败前的链路状态。 |
| Platform/Ethernet/eth_network.c、eth_network.h | EthNetwork_Service 统计实际连续失败，eth_network_drain_poll 分离停止期间的 RX/定时器推进。 |
| CM7/ApplicationV3/Services/Network/network_service.c 和 .h | NetworkService_Fault、NetworkService_AdvanceStop、NetworkService_AdvanceRecovery 驱动退休/恢复，观察快照和 Mailbox 仍由单一 NetworkTask 更新。 |
| MDK-ARM/h7_mac_CM7_custom.sct | 新增 64 B 诊断 UNINIT 区域，原 DTCM 链接容量缩小 64 B。 |
| Tests/ApplicationV3/network_fixture.c、startup_test.c、run_tests.py | 使用真实 Service、Mailbox、lwIP、HTTP、Camera 归还协议，模拟 MAC MMIO 及 Camera 数据。 |
| Tests/Ethernet/test_mac.c、run_host.cmd、build_firmware.py、build_production.py | 真实驱动的 Busy/Timeout 注入及首次现场核验。验收构建增加 DTCM 区域检查，独立工程继续使用各自链接配置。 |

根因、E5/N5 函数级差异、状态图和逐项资源退出条件见[诊断与恢复分析](N5_R2_R3_DIAGNOSTICS_AND_RECOVERY.md)。Camera FIFO、8 字节复制策略、JPEG 源数据验证器、TCP 环、lwIP、MPU 属性、DMA、USB、Storage、CM4 和 RTE 与 R1 无源码差异。原始工作区的用户 RTE 前后 SHA-256 为 270b61dded9e5eeb243f633224ced83b1c81bc11f38abf5bc68e9d03d072ba8d。

## 故障注入与历史回归

ApplicationV3 ARMCC O0/O2 共 **186/186 PASS**。场景 129–135 各在两种优化下执行。另由 MSVC 用真实 eth_mac.c 和假 MMIO 测试 Busy、等待超时、非法协商、PHY Missing、MAC/DMA IRQ、正常掉线和首次记录经外设复位保留。MDC 分频的 12 个既有边界用例通过。[ApplicationV3 结果](Evidence/N5R2R3/application-v3-results.json) · [真实驱动主机日志](Evidence/N5R2R3/phy_dma_mac.log)

| 场景 | 断言 |
| --- | --- |
| 129 | 单次 MDIO 不关闭 Listener。同一 tick 重读不计为新样本，下一有效样本清除 streak。Link Down/Up 走正常路径。 |
| 130 | HTTP 正借用私有 JPEG 时连续失败。退休过程不重新启动 MAC。TCP 资源、MJPEG 私有借用退出后重建 Listener，新浏览器会话收完新的 MJPEG 帧。 |
| 131 | 连续失败时真实 lwIP 退休 TCP Client 和 UDP 端点。本轮只自动恢复一次，再次故障等待显式 RESTART。 |
| 132 | 自动停止遇到驱动返回失败。保留实例且不循环调用 MAC stop/start。故障期间不接受新 START，显式 STOP 成功后资源清零。 |
| 133 | DHCP 已取租约。故障后清理租约及 PCB，自动恢复等不到 DHCP 时有界退出，无 Listener 或 netif 遗留。 |
| 134 | Camera 已提供真实帧租约，但延迟应答。130 s 后保留所有权，迟到应答只允许 Camera 归还一次。显式 STOP 可完成待处理的接口退出。 |
| 135 | 自动退休后的冷却窗口收到显式 STOP，取消恢复且不再次启动 MAC。 |

E4.1、E4.2、E5、E6、E7、E7 QVGA 主机回归通过。Camera I2C、Media Lease、Frame Dispatcher、Storage FS、UVC Reader、OLED 和 IPC V2 一并通过。旧 IPC Status 仍因缺少 app_status.h 为 **FAIL / BASELINE_FAIL**。早期场景 131 曾发现就绪计数与 lifecycle 跨两轮发布，原失败保留，最终候选在同一轮发布后重测通过。[主机回归矩阵](Evidence/N5R2R3/host-results.json) · [旧 IPC Status 日志](Evidence/N5R2R3/ipc_status_legacy.log) · [初轮失败记录](Evidence/N5R2R3/initial-fault-injection-failures.json)

R1 CRC 用例在最终候选重新执行。同一序号、5294 B JPEG 分别取自 Camera FIFO、AXI 私有副本、TCP 应用发送环和模拟 PC 接收端。O0/O2 均为 **0582F333**，FIFO 尾部拆分为 4 B。这是软件同帧证据。[同帧 CRC 结果](Evidence/N5R2R3/same-frame-crc.json)

## 构建、固件和 MAP

独立固件 E4、E4.2、E5、E6、E7、E7 V1、E7 V1 AXI、E7 V1 QVGA 均为 0 Error / 0 Warning。E3.5 在中间提交构建同样通过，逐项验证其输入 SHA-256 与最终版本一致。[独立固件编译结果与输入哈希](Evidence/N5R2R3/independent-firmware-builds.json)

正式 CM7 JPEG、CM7 Raw、CM4、生产网络配置和 N5 实板验收配置均为 **0 Error / 0 Warning**。AXF 中正式 JPEG 为 320×240，Raw 为 160×120。相对 R1，正式 CM7 Code 增加 1148 B，ZI 增加 196 B，RO/RW 容量相同。CM4 Code/RO/RW/ZI 与 R1 相同。[构建汇总](Evidence/N5R2R3/build-results.json) · [完整构建日志](Evidence/N5R2R3/) · [MAP 与固件 SHA-256](Evidence/N5R2R3/map-and-flash-audit.json)

| 区域 | 地址 | 分配 / 上限 |
| --- | --- | --- |
| 诊断记录 DTCM UNINIT | 0x2001FFC0 | 44 / 64 B |
| R1 私有 JPEG AXI SRAM UNINIT | 0x24020000 | 0xF000 / 0xF000 |
| Ethernet DMA | 0x24078000 | 0x3100 / 0x8000 |
| Ethernet CPU/lwIP | 0x24010000 | 0xA8E0 / 0x10000 |

验收 AXF、HEX、MAP 由同一次构建生成，哈希在[硬件验收固件清单](Evidence/N5R2R3/hardware_acceptance-manifest.json)。待独立审核和用户批准后，再依据[实板验收方案](N5_R2_R3_HARDWARE_PLAN.md)核对 MDIO 首故障、真实恢复、100 帧 CRC、长稳、USB/SD/Ethernet 并行和 100 轮运行。
