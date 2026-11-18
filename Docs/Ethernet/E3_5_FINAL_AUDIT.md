# Ethernet E3.5 最终审核

日期：2026-11-18。审计基线：本地 HEAD 与远端 `refs/heads/codex/ethernet-bringup` 均实查为 `416b88518973a3fa04427b69e0f9962cd712fa61`。工作区中已有 Keil 生成文件 `MDK-ARM/RTE/_h7_mac_CM7/RTE_Components.h` 的修改，保留。未修改生产驱动或测试固件，未重跑会话 371。

| 项目 | 状态 | 证据、影响及 E4 门禁 |
| --- | --- | --- |
| RX 路径与 IRQ/OWN/Tail 源码审核 | CLOSED | 已查 RX 交付、两种测试入口、环更新及 W1C；主机回归通过。此项不是硬件结论。 |
| 历史 RX 0/1000 根因 | OPEN | 历史快照不能对齐发包时刻、采样、CPU 和 DMA。尚不能认定驱动故障或操作问题；受控短测及残余风险须人工审核，当前阻塞 E4。 |
| RBU 从 1 增至 32 的阶段及原因 | OPEN | 已证明曾经耗尽、后来正常收齐；增量跨多个阶段，无同步证据。当前阻塞 E4，待分段短测。 |
| 板上 Flash 身份 | NOT_RUN | 本机 AXF SHA 已重算；尚无下载器 Verify 或 Flash 读回证据。阻塞 E4。 |
| Cortex-M7 MPU Region 4/5 原始寄存器读回 | NOT_RUN | 现有截图的 `memory_valid=1` 已经包含对 Region 4 的运行时检查，但没有原始寄存器证据，且不校验 Region 5 的预期缓存属性。阻塞 E4。 |
| 受控短测 | NOT_RUN | 等待现场人员完成读回并将板卡置于已知状态；只跑下述短会话。 |
| 会话 371 的 >=600 秒与 DCache 一致性 | CLOSED | 619.558 秒、62000/62000 和 61999/61999，原始 PCAP 可重放，板端错误为 0。沿用已取得的实测，不重复。 |
| E4 | BLOCKED | 上述遗留项待读回、短测及人工评审；本轮不进入 lwIP。 |

## RX 0/1000 源码路径

1. ETH RX DMA 使用非缓存 AXI SRAM，写入缓冲区并写回描述符状态、清 OWN。此次代码给每个已发布 RX 槽设置 IOC。中断记录 RI 等事件，但不交付数据。主循环不等待 IRQ 或 Service 才能 Poll：旧 Smoke 每轮调用 `eth_mac_rx_poll()`；E3.5 仅在 `g_e35_poll_enabled=1` 时调用，两者在循环中独立约每 10 ms 做 Service。Service 主要检查并处理链路、模式变化和锁存故障。
2. `eth_mac_rx_poll()` 先检查故障锁存、RUNNING 和 DMA 内存配置，再调用 `eth_ring_rx()`。头槽 DMA OWN=1 返回 EMPTY；清 OWN 后做屏障，读取状态。正常完整帧复制给调用者并增加 `rx_delivered`；错误帧增加 `rx_errors`、消费并归还槽；容量不足增加 `rx_small`、保留该帧供重试。RI 和 `rx_delivered` 无 1:1 约束。DMA 可以清 OWN、触发 RI，却没有任何软件交付。
3. 旧 Smoke 交付成功时先增加 `g_eth_test_rx_count`，然后才进入 `EthTestRx_Inspect()`。命令 5 清测试状态、设 missing=1000、置 active=1；命令 6 清 active、计算 passed。两条命令都不启停 RX DMA。inactive 直接返回，连 ignored 都不加；active 下检验固定源/目的 MAC、EtherType 0x88B5、60 字节、序号及固定载荷。此协议无 session 字段。
4. E3.5 使用不同的 `EthValidation_Check()`。命令 7 仅在 inactive 且配置合法、MAC RUNNING 时清会话计数并锁存 session/长度/VLAN、设 active=1；命令 8 清 active、记录结束时间并关测试 TX，RX Poll 不停。9/10 只暂停/恢复 Poll。每次成功交付都会增加 raw_rx，inactive 或无关会话增加 ignored；校验失败增加 corrupt。valid_rx 包含发现的重复帧，验收必须同时核对 late_or_duplicate、gaps、next_sequence 和对端总数。

| 候选 | 可观测组合 | 排除或定位 |
| --- | --- | --- |
| 采样 inactive、开始太晚、提前结束 | CPU/驱动 RX 继续走；旧 Smoke received/ignored 不动，E3.5 raw_rx/ignored 可增长；valid 不增 | 记录开始/结束、命令回执与对端首末帧时间；单个结束快照的 active=0 本来就可能正常 |
| CPU Halt 或 Watch 陈旧 | Halt 时主循环 heartbeat/poll_calls 与 ISR 计数不执行，DMA 若仍运行则可能交付至最多三个已发布槽并锁存 IRQ 状态；恢复后软件计数可能补增，不能重建逐帧时间 | 用运行状态、两次独立刷新和 PCAP 核对；不能把静态 Watch 当成 Halt 证明 |
| Poll 暂停 | heartbeat 增长、poll_calls 不增；RI 可能增加直到三个槽耗尽；OWN→0，rx_delivered/raw_rx 不增 | E3.5 命令 9/10 可受控复现；旧 Smoke 没有此开关 |
| IRQ 被屏蔽 | NVIC 启用/挂起、PRIMASK/BASEPRI、DMACIER、DMACSR 可见差异；RUNNING 时 Poll 不依赖 IRQ，仍可能交付 | Reset/Stop 屏蔽 NVIC 和 DMA IRQ，Start 成功后开启；故障 ISR 可关 DMACIER 并锁存，Service 停机。统计/MPU 临界区保存并恢复 PRIMASK |
| 错网卡、地址、协议或 session | 有 PC 发包但板端 OWN/RI/交付不动，或背景帧交付而被忽略；错误长度/内容可触发测试校验 | 需要匹配会话的可重放 PCAP、正确对端及同时间窗；PC 本机出站抓包不能单独证明入板 |
| 硬件/驱动没有交付 | CPU/Poll 活跃且窗口匹配；状态非 RUNNING、`service_result`/错误增量、头槽状态或 DMA 游标异常之一出现；头槽 OWN 长期不变时也要排除 MAC 过滤和物理路径 | 保存状态、IRQ、描述符写回和 DMA 首尾指针；区分没有 DMA 写回、写回错误、容量不足及应用不认帧 |

历史记录只有 `active=0, received=0, unique=0, missing=1000` 与累计 `rx_count/irq_rx=0x402`，之后 `rx_count` 见到 `0x405→0x408`。只能说明某些帧曾交付，不能证明是测试的 1000 帧、也不能确定当时 Poll、CPU 或采样窗口。首次复测通过不等于根因已确认。新短测能判断当前基线是否复现并补足区分证据；若无历史同步证据，历史根因继续 OPEN，是否接受残余风险由人工审核。

## RBU、W1C 与 RX 环时序

STM32H747 CMSIS 中 `DMACSR.RBU` 和 `DMACIER.RBUE` 均为 bit 7（0x80）。RBU 表示 DMA 无可取得的接收描述符/缓冲区而暂停 RX 描述符处理；它是状态事件，不按以太网帧记账。硬件可合并待处理中断；W1C 后再次置位也可能再次计数。掉帧须用发送序号总数和本次唯一交付核对。参考 [STM32H747 RM0399 Ethernet 章](https://www.st.com/resource/en/reference_manual/rm0399-stm32h745755-and-stm32h747757-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)（RX DMA、描述符环、DMACSR；在线 PDF 未能抽取该寄存器全文）及 [ST H7 HAL 的 RBU 清除实现](https://github.com/STMicroelectronics/stm32h7xx-hal-driver/blob/master/Src/stm32h7xx_hal_eth.c)。

ISR 读取 DMACSR/MACISR，保存 DMACSR 原样到 last_dma_status，见到 RBU 则计数一次，然后仅以 `snapshot & W1C_mask` 写回 DMACSR，并执行 DSB。RBU 本身不锁存故障或关闭 DMA；FBE/CDE/TPS/RPS 或 MAC 收发状态异常是单独故障路径。W1C 清状态不归还描述符、不写 Tail；周期快照可能已经读不到 W1C 清过的 RBU，last_dma_status 也会被下一次中断覆盖。

四槽步长 32 字节，RX 基地址 `0x24078080`。下面是从空闲重建环开始的**预期**序列；下标按模 4 轮转，实板已有流量时起点可能旋转。掩码是假设列出的 DMA 更新之间无其他帧：

| 时点 | RX head / guard | OWN 掩码 / Tail | 预期 |
| --- | --- | --- | --- |
| Start 稳定后 | 0 / 3 | 0x7 / 0x240780E0 | 发布槽 0、1、2，槽 3 保留；SR=1。 |
| 9 后收前三帧 | 0 / 3 | 0x6 → 0x4 → 0x0；Tail 不变 | DMA 依次写回三槽；Poll 停用，交付不增加。达到可用容量边界后才期待观察 RBU，另外送足够帧供触发。 |
| 仍暂停、等待零测试流量 | 0 / 3 | 0x0；Tail 不变 | 留出窗口记录 RBU、RI 和接收状态；期间不据 IRQ 次数推算丢帧。 |
| 10 后首次消费槽 0 | 1 / 0 | 先发布槽 3（0x8），Tail=0x24078080 | 复制或丢弃旧槽，DSB、设置旧 guard OWN、DSB，再写新 guard 地址以提供接收轮询机会。无需重启 MAC。 |
| 继续消费 1、2 | 2 / 1 → 3 / 2 | 若 DMA 没再消费，0x9 → 0xB；Tail=0x240780A0 → 0x240780C0 | 有 FIFO 积压时发布槽可马上被 DMA 消耗，观测掩码不必等于示例，RBU 可再次出现。 |
| 排空后、给新序列 | head 与 guard 继续轮转 | 安静时预期三个 OWN；新帧应写回并交付 | 比较阶段增量与新序列是否丢失；持续无进展或孤立低速负载下无法解释的 RBU 不视作通过。 |

旧证据：356 暂停前 OWN=7、RBU=0、rx_delivered=24；耗尽后 OWN=0（用户确认）、RBU=1、rx_delivered=24、IRQ RX=27。357 结束时 RBU=32、RX/所有权/DMA/MAC 错误为 0，另收到新会话 1000/1000。中间缺恢复完成、下一批发包前的边界快照。多出的 31 次可能发生在暂停等待、恢复时处理积压或下一批期间；它们既不能记作 31 帧丢失，也不能先判为全属正常。主机模型能验证发布和轮转约定，不能复现硬件 FIFO 或解释这 31 次。

## 现场取证：Flash 和 MPU

先停止 PC 测试发包并确保测试未激活。保留当前板上镜像，不要先下载。对本地 `output/Ethernet/e35/eth_e35.axf` 执行 `Get-FileHash -Algorithm SHA256`；本次实查是 `2cb678e48429b6e12b56f30186f360c7da18f236e348fa61ab2310a49f06980c`（AXF 容器哈希，不是板上 Flash 哈希）。

1. **Flash 身份：**用 CM7/CMSIS-DAP 的 **Verify Code Download**，禁用 **Update Target before Debugging**，确认选择的是上述 AXF。记录实际校验输出、目标/镜像路径和校验范围；不要把“无报错”“Load finished”或项目名当成独立证据。如果驱动不提供可审查的校验输出，采用只读 Flash 回读，对 AXF 中属于 Flash 的实际加载地址和文件字节按段逐字节比较，逐段记录地址、长度、镜像与读回 SHA256。两者都不能把 AXF 的 ELF/调试信息或 ZI 当作 Flash 内容。若先重新烧录再 Verify，只能证明新烧录镜像，不能追认历史会话。参见 [Keil 下载与 Verify 配置](https://www.keil.com/support/man/docs/uv4cl/uv4cl_fl_usingflashmenu.htm)。
2. **MPU：**确保 C-M7 已执行到测试主循环，空闲时 Halt；此处与任何收发测量分离。用 Keil Command 按顺序读下列地址。唯一写入是 `MPU_RNR` 的区域选择；不要写 CTRL、RBAR、RASR 或 Ethernet 寄存器。先记下原 RNR，读完恢复原值，再 Run。如果 Command 不支持这些函数，改用 CM7 System Viewer/Memory Window 等效读取。参见 [Keil 调试器读写函数](https://www.keil.com/support/man/docs/uv4cl/uv4cl_df_predeffunct.htm)。

    ```text
    _RDWORD(0xE000ED94)         // MPU_CTRL
    _RDWORD(0xE000ED98)         // 原 MPU_RNR，先记下
    _WDWORD(0xE000ED98, 4)
    _RDWORD(0xE000ED98)
    _RDWORD(0xE000ED9C)         // Region 4 RBAR
    _RDWORD(0xE000EDA0)         // Region 4 RASR
    _WDWORD(0xE000ED98, 5)
    _RDWORD(0xE000ED98)
    _RDWORD(0xE000ED9C)         // Region 5 RBAR
    _RDWORD(0xE000EDA0)         // Region 5 RASR
    _RDWORD(0xE000ED14)         // SCB_CCR
    ```

    用 `_WDWORD(0xE000ED98, 原值)` 恢复，复读 RNR。保存带命令与读值的原始日志或截图。基址以 `RBAR & 0xFFFFFFE0` 解码；SIZE 的实际字节数为 `1 << (SIZE+1)`，子区禁用位应全为 0。下表是**预计值**：

    | Region | Base | Size | TEX | C | B | S | AP | XN | Enable | RASR 预计 |
    | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
    | 4 | 0x24078000 | 32 KiB | 1 | 0 | 0 | 1 | 3 | 1 | 1 | 0x130C001D |
    | 5 | 0x24010000 | 64 KiB | 1 | 1 | 1 | 0 | 3 | 1 | 1 | 0x130B001F |

    另核对 `MPU_CTRL=0x5`、`CCR & 0x10000 != 0`。Region 4 非缓存 DMA 窗，Region 5 是测试专用缓存调用者窗口。现有 `eth_memory_valid()` 会实读 Region 4、检查覆盖并恢复 RNR/PRIMASK；对 Region 5 仅确认未覆盖 DMA 窗，不能代替上述属性读回。

## 现场取证：最少短测

启用 Watch 周期刷新，禁用断点，保持 CPU Run。使用现有固件：`g_e35_metrics` 提供 heartbeat、poll_calls、raw_rx、OWN、DMACSR、active、service_result；`g_e35_statistics` 提供 RBU、RI、rx_delivered 和错误数。可用 Keil Search/Symbol Window 找到 `s_config.session`、`s_ring.rx_head`、`s_ring.rx_guard`（文件内静态符号）；从 ETH System Viewer 只读 `DMACRDTPR`、`DMACCARDR`、`DMACIER`、`DMACSR`/最后中断状态。每个安静边界均保存两次快照、PC 时间和目标 tick；Watch 不是原子快照，遇到变化需保留顺序，不能合成为一个瞬时状态。该方案已有足够字段区分复现阶段，暂不增加测试入口字段。

| 阶段 | 操作及记录 | 通过或定位 |
| --- | --- | --- |
| A：active 对照（372） | RUNNING/Poll=1/TX=0，60 字节、无 VLAN，命令 7 锁存 372。捕获初值，PC 低速发 16 帧，保存 active=1 的结束前快照，PC 停发后命令 8。可再用**同一已锁存 372** 发单独一批 16 帧给 inactive 状态，录独立 PCAP 与前后快照，不重写命令 7；它只作负对照。 | active 时 16/16、序号连续、错误为 0；inactive 时 raw_rx/交付仍增而 valid_rx 不变。若 active 时复现 0，先保留现场，禁止复位或切换命令。 |
| B0：新会话、暂停前（373） | active=0，切换 373，命令 7，待 PC 静默保存基线；命令 9 后确认 heartbeat 增而 poll_calls 不增。保存 OWN/head/guard/指针、RBU/RI、raw_rx/rx_delivered。 | 环尚有 DMA 槽，CPU 没有 Halt；若开始前已耗尽，先记录背景流量，另选干净短测窗口。 |
| B1/B2：暂停下发包、随后静默 | PC 发送 32 帧并保存可重放 PCAP；**停发后仍保持 9 的状态**，保存两次快照（发完立即、静默约 2 秒）。 | OWN 可降为 0、RBU 增、交付不增；分开计 `RBU(B1)-RBU(B0)` 与 `RBU(B2)-RBU(B1)`。 |
| B3：只恢复 Poll | PC 仍静默，命令 10；尽快记录，待 raw_rx 和 RBU 稳定再记录排空终值。期间不写 2/3、不复位。不硬规定待交付帧数，因 MTL 可能有积压。 | Poll、交付和 Tail 恢复推进；单独记 `RBU(B3)-RBU(B2)`，以及回到安静状态的 OWN。若 RBU 持续增，或 OWN/交付无进展，保留现场。 |
| B4：新短会话（374） | 等 B3 排空后命令 8，再用 374、命令 7；记录本会话发送前基线，PC 以不小于 25 ms 间隔发送 64 帧并保存 PCAP，结束后命令 8。 | 无 MAC 重启；64/64、next_sequence=65、无错、无损坏/重包/缺序。单独报告本段 RBU 增量，仍异常时结合流量和 OWN/Tail 继续定位。未重现旧数值不能自动关闭历史异常。 |

各阶段 PC 在工程根目录使用 `python Tests/Ethernet/e35_peer.py --session <会话> --length 60 --count <帧数> --interval-ms <间隔> --capture-seconds 12 --output <新目录>`；A 和 B4 用 25 ms，B1 用 1 ms。下一阶段必须上一段已停发并保存快照才开始；不覆盖证据，也不重复长测。不通过改 OWN/Tail、屏蔽 IRQ 或关闭错误检测来消除 RBU。

## 本轮验证与边界

在当前源码上重新运行 `Tests/Ethernet/run_host.cmd`：PHY；DMA OWN、guard 和 1000 次回绕；MAC 正常及两个故障入口；独立游标模型耗尽/恢复（每向 3000 帧）；旧 RX 采样检查；12 个 MDC 边界；新帧协议及 Python 3 项测试全部通过。它们是本轮主机测试。未重编/重烧 CM7，未运行上述实板短测，未观察到实际 Verify 或 MPU 原始读值。只提交审核和现场步骤；等待现场证据再更新门禁。
