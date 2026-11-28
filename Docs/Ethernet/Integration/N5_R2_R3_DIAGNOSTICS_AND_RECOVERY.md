# N5-R2/R3 MDIO 诊断与网络资源恢复

软件基线为经审核的 R1 提交 1b1035aa45394d108c8953968df671beea99c4a6。整改及其构建和试验状态见[联合验收报告](N5_R2_R3_VERIFICATION.md)。

## 根因与证据范围

旧实板报告出现网络生命周期 FAULT，MDIO 错误累计 9 或 11，MAC/DMA 错误 IRQ 为零。一次测试中还能 Ping 到板端，TCP 80 返回 RST。旧固件没有逐次 MDIO 失败阶段及寄存器原值，故最先出现的是 Busy-on-entry、硬件事务超时还是其他板级因素仍为 **NOT_PROVEN**。源码与故障注入能证明两处放大故障的软件路径。旧 JPEG 线端 CRC 失败与本问题分别验证。[历史现场](https://github.com/ruk93668-creator/h7_mac/tree/12e21347db5d6a6988a037f1a17fd82a9953328f/Docs/Ethernet/Integration/Evidence/N5Hardware_2026-11-26) · [原始以太网抓包](https://github.com/ruk93668-creator/h7_mac/tree/38e1e478640c5b2de9f17cebd162ef7d44ee4b83/Docs/Ethernet/Integration/Evidence/N5Hardware_2026-11-26/Captures)

1. 原 EthMac_AccessPhyRegister 对 MB 进入即忙和提交后等待超时都返回 -1，eth_mac_get_link 又将非法协商状态和 MDIO 读失败一起映射为 ETH_IO。运行中 eth_mac_service 任意读失败就停止 MAC，单次失败也会打断正常连接。
2. 原 AppV3_NetworkAdvance 调用 NetworkService_BeginStop 后仍执行完整网络轮询。只有 Mailbox 收到 STOP/RESTART 时才调用 NetworkService_Stop。在 READY 的 MAC 可被完整轮询重新启动，而已关闭 Listener 的 HTTP 服务没有经过重新绑定。这样会出现新地址可用而浏览器无法连接，并留下停止中的资源。

## MDIO 分类与首故障

| 观测 | 驱动结果或阶段 | 处理 |
| --- | --- | --- |
| 本次提交前 MACMDIOAR.MB 已置位 | ETH_MDIO / ETH_MDIO_BUSY_ON_ENTRY (1) | 加 mdio_busy_on_entry，保持上次已确认链路。 |
| 提交后超过原有 10 ms 或原有自旋上限 | ETH_MDIO / ETH_MDIO_WAIT_TIMEOUT (2) | 加 mdio_wait_timeouts。记录实际 MB，等候返回瞬间可能已变化。 |
| 指定 PHY 寄存器读操作失败 | phy_register_read_errors | 上述阶段中读操作的子计数，同一失败也计入 mdio_errors。 |
| 管理事务完成，协商模式编码非法 | ETH_PHY_STATUS | 加 phy_status_errors，进入故障退休。 |
| PHY 标识不匹配 | ETH_PHY_MISSING | 初始或启动阶段拒绝继续，保留实际读出的 PHY ID。 |
| 正常拔线、协商未完成、链路模式改变 | ETH_LINK_DOWN / ETH_BUSY | 沿用链路管理，由 netif/Transport 撤销在线端点并等链路返回。 |
| MAC、DMA 硬件 IRQ 或外围复位失败 | ETH_IO / ETH_TIMEOUT | 从专用 IRQ、原始状态及起停计数交叉确认，立即升级。 |

现场由任务在 MDIO 失败分支读取，在任何 MAC 重置之前保存 MACMDIOAR、MACMDIODR、HAL Tick、HCLK、MDC CR 编码、PHY 地址、寄存器号和读写类型。Busy-on-entry 的原始 AR 中，PA、RDA 与操作表示前一在途事务，单列地址、寄存器、操作表示被拒绝的本次请求。读失败时 DR 不作有效 PHY 值使用。尚未初始化 lwIP 时也会发布诊断，Watch 入口为 g_network_status.driver.first_mdio_fault。ISR 沿用原 IRQ 处理。

CR 编码 0x000、0x100、0x200、0x300、0x400、0x500 分别对应 HCLK 除以 42、62、16、26、102、124。驱动未改分频选取、MDIO 等待上限、BMSR 双读、PHY 协商和 RMII 板级时序。

正式 CM7 将 44 B 首故障写入 DTCM 的 64 B RW_ETH_MDIO_FAULT UNINIT 窗口，起点 0x2001FFC0。记录带校验，最后发布有效标记。重置 MAC、停止服务和恢复流程均不会覆盖有效记录。链接规则跳过 C 启动清零，记录可在保留 DTCM 的暖复位中存续，实际板上暖复位读回 **NOT_RUN**。计数属于本次开机，保留的首故障 Tick 属于捕获它的那次开机。保留记录只用于诊断，恢复控制依据当前运行结果。

## E5 与 N5 函数级调用链

| 层次 | 独立 E5 | 正式 N5 修改后的路径 |
| --- | --- | --- |
| PHY/MAC | eth_mac_service → eth_mac_get_link → eth_phy_read_link → EthMac_AccessPhyRegister | 同一串行接口。MDIO 失败独立返回，正常掉线和硬件故障保留底层处理。 |
| Network | eth_network_poll → EthNetwork_Service → eth_netif_poll / sys_check_timeouts / DHCP | 完整轮询统计真实采样。停止期改调用 eth_network_drain_poll，只处理当前 netif 的在途 RX 和已有 lwIP 定时器。 |
| 传输 | 独立入口逐轮调用 eth_transport_poll | NetworkTask 负责同一 Transport 实例的轮询和退休。停止期拒绝新业务。 |
| HTTP/MJPEG | E5 提供 TCP/UDP，E6/E7 分别由独立入口提供 HTTP | eth_http_begin_stop 归还私有 JPEG 借用，eth_http_finish_stop 等 TCP 已退休后解除 Source。 |
| ApplicationV3 | 独立入口自行轮询 | AppV3_NetworkAdvance 每轮驱动停止、超时和恢复。沿用 Mailbox 和硬件状态报告。 |

## 状态和升级策略

正常轮询周期为 10 ms，MDIO 升级门槛为 3 次连续的实际失败采样。正常样本、真实 Link Down 或协商等待会清零连续计数。同一 tick 多次轮询不会累加。门槛只决定应用层何时退休，不改变单次 MDIO 访问。

激活会话的连续 MDIO 故障允许每轮显式 START/RESTART 后自动重建一次。完整退休后冷却 500 ms，在重建后最多等待 30 s 才确认 Link、IPv4 与 Listener 就绪。该重建失败、超时或同一轮再次故障时收尾并等待显式命令。初始化阶段的 MDIO 故障、非法 PHY 模式和 DMA/MAC 故障直接退休。显式 STOP/RESTART 可加入当前退休并取消自动重建。无效 RESTART 先验证参数。重复请求沿用 Mailbox 的事务身份与队列背压。

~~~mermaid
stateDiagram-v2
    READY --> ACTIVE: 显式 START 成功接纳
    ACTIVE --> ACTIVE: 单次 MDIO 或正常链路变化
    ACTIVE --> QUIESCING: 连续 3 次 MDIO 或严重错误
    ACTIVE --> QUIESCING: STOP 或 RESTART
    QUIESCING --> READY: 显式 STOP 且资源已退休
    QUIESCING --> ACTIVE: 显式 RESTART 且新实例成功接纳
    QUIESCING --> FAULT: 退休失败、超时或自动处理完成
    FAULT --> STARTING: 符合恢复条件且完整退休，冷却 500 ms
    STARTING --> ACTIVE: 确认 Link、IPv4 和 Listener
    STARTING --> QUIESCING: 就绪超时或重建错误
    FAULT --> QUIESCING: 显式 STOP 或 RESTART
~~~

## 资源退休完成条件

| 资源 | 唯一拥有者与收尾证明 |
| --- | --- |
| HTTP/MJPEG | NetworkTask 停止新业务，HTTP 归还私有 JPEG 借用。借用及 TCP 退休后解除 Source。重建实例可接受下一次浏览器请求。 |
| TCP、发送环和 UDP | Transport 关闭监听和业务 UDP，终止未建连的 Client。已连接 PCB 进入正常关闭及有界 TCP 超时。确认两个角色 PCB 及 active/TIME_WAIT/bound/listen 列表均为空再解除端点。发送环所有权按正常关闭或 Abort 契约退出。 |
| Camera Frame Lease | NetworkTask 撤销需求，匹配 command、epoch 和 lease，仍处理迟到 OFFER 并发送 RETURN。Camera 拥有者执行归还并发 RETURN_ACK，Network 处理后送 RETURN_ACK_CONSUMED。原 JPEG FIFO、8 字节复制、DSB 和 Frame Lease 协议继续使用。 |
| DHCP、netif、MAC/DMA | 继续处理 TCP 退休所需的 RX 和定时器。跳过 PHY 探测、MAC Start 和地址轮询。上层资源退出后撤销 link/admin，停止 DHCP，清理 ARP，确认 MAC/DMA 停止后移除 netif。全局 lwIP 定时器仍由同一任务推进。 |
| 超时或停止失败 | 单轮最多等待 130 s。记录 stop_result，保留未退休资源和原故障，取消自动启动。迟到帧应答和 TCP 定时器仍可推进，显式 STOP/RESTART 可发起新一轮有界退休。 |

R1 的 JPEG .network_jpeg 保持 0x24020000 / 0xF000。DMA/MPU/cache 配置和 USB、Storage、Camera、CM4 源码维持 R1。此轮新增 DTCM 诊断窗口，常规 DTCM 链接容量减 64 B。[R1 内存与 CRC 审核](N5_R1_AXI_JPEG.md)
