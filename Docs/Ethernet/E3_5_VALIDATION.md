# E3.5 MAC/DMA 验收报告

日期：2026-11-17。当前结论：**软件基线和独立验收工具已准备；本轮已完成 60 字节双向帧内容与计数核对，驱动错误/IRQ 统计待补，其余实板用例 NOT_RUN，E3.5 尚未关闭，E4 仍被阻塞。** 之前聊天、抓包和板端“符合预期”不计为本轮结果。

## 版本和证据

开始时实际分支 `codex/ethernet-bringup`，本地与 `origin` 查询结果均为 `5d8ed49e540b94a0714a5982323f2bd3bc409948`。基线比较版本 `f097d386cdab1fee78ed65b8c84e92373c4feb8a`。初始工作区只有用户已有 `MDK-ARM/RTE/_h7_mac_CM7/RTE_Components.h` 项目名称注释变化，已保留且不提交。

驱动源 SHA：`5d8ed49e540b94a0714a5982323f2bd3bc409948`，本轮未修改 `Platform/Ethernet`。新增测试源 SHA：`175b14da6e98a63702a6d09846986ba10e03ef26`。构建发生在测试源提交前，原始结果中的 source_head 因而是 5d8ed49；构建前后输入哈希相同，随后已逐文件与 175b14d 的 Git blob SHA256 核对一致，详见 `Evidence/E3_5_20261117/manifest.json`，没有用提交号代替源文件一致性检查。

| 本轮文件 | AXF SHA256 |
| --- | --- |
| CM7 生产固件 | `22893f388b0246f755fad423caf1edaadbdb383123f628c345b8a2c754af8297` |
| 原独立 smoke 基线 | `1b88bf89934371f890a1a7fa1740b0c164dc2508db88be7692df99a0bdbb2e57` |
| **E3.5 待下载固件** | `2cb678e48429b6e12b56f30186f360c7da18f236e348fa61ab2310a49f06980c` |

下载路径：`D:\32\STM32H7\h7_mac_wk\output\Ethernet\e35\eth_e35.axf`。报告列出的 AXF 哈希是本轮实际文件哈希，重新链接后可能改变，重新测试须另存新的哈希、结果和日志。尚未确认板上运行的文件与此 AXF 相同。

完整构建日志、输入哈希、实际链接摘要和审计结果已保存在同目录 `Evidence/E3_5_20261117/`。原始 AXF、MAP、生成工程和 PCAP 留在本地 `output/Ethernet/`，按 .gitignore 不提交；其 SHA、布局摘要和日志提交至 Git。主机 Npcap API 自检仅监听 1 秒，发送 0 帧，不能计入板端验收。

## 重构行为审核

对比 f097d38 与 5d8ed49。审计记录 16 个关键函数的语句比较，展开明确的变量/类型/函数重命名、指针 NULL 写法、声明拆分、常量改名及超时值后相同。这是受限文本比较及人工顺序审阅，不是所有执行路径的形式化证明。

- TX：同步复制调用者数据，填写描述符，屏障，发布 OWN，屏障，推进下一槽尾指针；没有复用 DMA 持有的槽。清 OWN 后仍须软件 reclaim 才释放容量；按序检查 ES/FD/LD/上下文异常。
- RX：三个已发布槽、一个 guard；消费头部完成帧后发布旧 guard，已消费槽成为新 guard，再更新尾指针。短容量保留帧、异常帧丢弃与重新发布顺序未改变。
- Start：校验状态/MPU/PHY/链路，完成硬件复位后才重建环；累计统计和 ticket 序列保留；写基址/长度/Tail、协商模式、刷新 FIFO、启用 MAC/DMA 并回读，最后开放 IRQ。
- Stop/恢复：先完成有界硬件复位，成功后才将未完成发送计入 aborted；IRQ 仅记数/锁存故障/W1C，不操作环；service 对故障或掉线停止硬件。未增加自动 Start 或自动重发。
- MPU 基址、大小、属性、链接窗口、DSL 步长、DMA IRQ 掩码及 W1C 表达式保持一致。MDC 分频由三目展开成分支，12 个边界初始化路径本轮通过。

结论：在上述检查范围内未发现重构导致的行为变化；不能据此替代硬件 DMA 和缓存一致性验收。

## 本轮软件与编译

| 检查 | 状态 | 本轮证据 |
| --- | --- | --- |
| PHY 回归/MDIO 故障注入 | PASS | baseline_host.log / extended_host.log |
| Descriptor 初始化、OWN、TX/RX 回收、满/空、容量、重复提交、异常完成 | PASS | DMA 测试；1000 次回绕 |
| MAC 启停、模式、初始化失败、MDIO/复位/FIFO 故障、IRQ/恢复 | PASS | MAC 正常路径及两个独立失败进程 |
| 独立 DMA 游标/Tail 模型、耗尽和恢复 | PASS | 每方向 3000 帧；软件模型不是硬件 |
| RX 原采样器会话/缺帧/重复/乱序/损坏门禁 | PASS | 原采样器测试 |
| MDC 12 个边界 | PASS | 真正调用主机 MAC 初始化路径 |
| 新协议 60/512/1514/1518 VLAN/1522 扩展长度、方向、会话、损坏 | PASS | C 测试与 Python 3 个测试；10 组 C/Python 实际字节向量相等 |
| Windows Npcap API 打开/非阻塞读取/统计/关闭 | PASS | npcap_api_only.json；0 发送，不含板端证明 |
| CM7 生产全量编译 | PASS | 0 Error / 0 Warning，DMA 链接检查通过 |
| 独立旧 smoke 全量编译 | PASS | 0 Error / 0 Warning，DMA 链接检查通过 |
| E3.5 独立全量编译 | PASS | 0 Error / 0 Warning，源码冻结、DMA 和缓存区链接检查通过 |

| 目标 | Code | RO | RW | ZI |
| --- | ---: | ---: | ---: | ---: |
| CM7 生产 | 137998 | 3114 | 1396 | 113484 |
| 原 smoke | 67194 | 2846 | 1220 | 82636 |
| E3.5 | 68398 | 2862 | 37096 | 81824 |

上表是编译器 Program Size 原始口径，不把它简单等同于片上某一个连续 RAM 区间。E3.5 相对 smoke Code +1204、RO +16、RW +35876、ZI -812；增加的是独立测试的缓存缓冲区、32 KiB 压力数组、计数与重启记录。生产源码、scatter 和 MPU 没有新增改动，本轮对生产代码增量为 0。首次开发构建在源码变化门禁处被拒绝；随后一次编译为零错误但 MAP 大小写匹配门禁误拒绝（实际地址正确），已修正检查并重新全量构建，最终 PASS；没有采用被拒绝结果作为最终固件。

## DMA、MPU、Cache 与实际内存

以下地址来自本轮 MAP；区间尾地址不含。

| 对象/区域 | 地址 | 大小 | 属性 |
| --- | --- | ---: | --- |
| Ethernet 保留窗 | 0x24078000..0x24080000 | 32768 | AXI SRAM，MPU region 4 |
| TX descriptors | 0x24078000..0x24078080 | 128 | 4 个，步长/对齐 32 |
| RX descriptors | 0x24078080..0x24078100 | 128 | 4 个，步长/对齐 32 |
| TX 内部副本 | 0x24078100..0x24079900 | 6144 | 4×1536，槽与 descriptor 一一对应 |
| RX DMA buffer | 0x24079900..0x2407B100 | 6144 | 4×1536 |
| 完整 DMA 对象 | 0x24078000..0x2407B100 | 12544 | 链接 UNINIT，复位后显式初始化 |
| E3.5 缓存保留窗 | 0x24010000..0x24020000 | 65536 | 测试专用 region 5，生产工程不增加此区 |
| 调用者 TX | 0x24010000..0x24010600 | 1536 | 缓存，32 字节对齐 |
| 调用者 RX | 0x24010600..0x24010C00 | 1536 | 缓存，32 字节对齐 |
| 压力数组 | 0x24010C00..0x24018C00 | 32768 | 缓存，32 字节对齐，volatile 写读 |
| E3.5 普通数据/栈执行区 | 0x20000000..0x20009248 | 37448 | DTCM，非 DMA 池 |

region 4 为 Normal TEX=1、C=0、B=0、shareable、XN、全访问。所有 DMA buffer/descriptor 独占该非缓存窗口，不与普通变量共享 Cache Line。提交后不做逐帧 Cache Clean/Invalidate；依赖 MPU 非缓存映射及 DSB 的 OWN 发布顺序，不能把 volatile 当作 Cache 方案。每个环有效容量均为 3，第四槽是 Tail guard。

测试 region 5 为 Normal TEX=1、C=1、B=1、non-shareable、XN、全访问（缓存写回/写分配），与 DMA 窗不重叠。测试入口先配置 MPU 再启用 I/DCache，每 10 ms 写读一遍 32 KiB 压力数组；TX 成功提交后立即污染调用者缓冲区。测试只通过生产复制接口收发，不直接写 DMA OWN、Tail 或 buffer。实板必须读取 CCR.DC=1、memory_valid=1，检查 MPU 4/5 属性、两个调用者地址、连续完整载荷与错误数，才能判定缓存一致性。

生产 DMA 池选 AXI SRAM；DTCM 不作 Ethernet DMA buffer，D2 SRAM 不重分配以避免 CM4 别名空间冲突，D3 IPC、SDRAM 和 SDMMC 区域不动。这里的可达性沿用已有 STM32H747 总线/内存审计，不以 CPU 能访问作为 DMA 可访问证据。

## VLAN 配置审核

用户本轮明确 VLAN 1522 指**线上包含 FCS**，故软件提交与常见去 FCS 抓包长度为 **1518**。代码使用单标签 0x8100、VID 7，软件不生成 FCS。当前 MACCR 保持 CST|ACS，JE/S2KP/GPSLCE=0；MACPFR=0，没有 VLAN 过滤；未启用标签插入/剥离。CMSIS 寄存器定义及原配置已检查。参考依据为 ST RM0399 Ethernet 章节的 MACCR、MACVTR、MACPFR 和帧长规则；[官方 RM0399](https://www.st.com/resource/en/reference_manual/rm0399-stm32h745755-and-stm32h747757-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)。本轮浏览工具无法完整提取超大 PDF，未把其他 MCU 的规则冒充 H747 的完整手册复核结果；现场需核对寄存器快照及真实 1518 字节带标签包，VLAN gate 仍为 NOT_RUN。

工具支持额外不含 FCS 的 1522 字节边界调查，这对应线上 1526，不属于用户确认的标准 VLAN 验收，不为接收该扩展帧打开 jumbo 或改动生产判错。PC 网卡如剥离 VLAN 标签，必须先解决对端观察条件，不能直接认定板卡异常。Npcap 统计口径参考 [Npcap API 文档](https://npcap.com/guide/npcap-api.html)，ps_recv 是句柄处理包数，ps_drop 才用于主机捕获资源丢弃证据，二者都不是板端接收数。

## 实板用例与当前结果

用户已运行 E3.5 测试入口并提供启动及会话结束截图：state=2、PHY=0x0007C131、Link Up、自动协商完成、100 Mbps 全双工；CCR=0x00070200、memory_valid=1。尚未独立读回板上固件 SHA，不能把本机 AXF 哈希直接当成板端读回证明。

| 用例 | 状态 | 双向数量/错误/丢帧率 | 缺失证据 |
| --- | --- | --- | --- |
| PHY ID、Link、MAC/DMA Start 基线 | PASS（截图所示状态） | PHY 0x0007C131，100 Mbps 全双工，RUNNING | 板上固件 SHA 未独立读回 |
| 60 字节 | PASS（帧内容/计数）；完整验收待补统计 | PC→板 1000/1000，板→PC 1499/1499；两向丢帧率 0%，损坏/重复/乱序 0 | 驱动 ring 错误与 IRQ 错误前后快照 |
| 512 字节 | NOT_RUN | 未取得 | 同上 |
| 1514 字节 | NOT_RUN | 未取得 | 同上 |
| VLAN 线上 1522（软件 1518） | NOT_RUN | 未取得 | MAC/VLAN 寄存器、PC 标签观察、双向数据 |
| TX 满/BUSY/reclaim 后再次提交 | NOT_RUN | 未取得 | 四次结果、tx_full 增量、4 帧实际完成 |
| RX 耗尽、恢复 Poll 后收新会话 | NOT_RUN | 有意丢包数未知 | 三阶段 OWN/RBU/IRQ/交付快照，恢复 1000 帧 |
| 100 次 Stop/Start 与重启后通信 | NOT_RUN | 未取得 | 200 个返回值、最后状态、新会话通信 |
| Link Down / Up 与通信恢复 | NOT_RUN | 未取得 | 链路/状态变化、显式 Start 后双向通信 |
| 连续双向 >=10 分钟 | NOT_RUN | 数量、损坏、丢帧率、带宽均未知 | PCAP 双向时间交集 >=600 s 与板端总数 |
| 真实 DCache 开启下完整数据一致性 | NOT_RUN | 未取得 | CCR、MPU、压力循环、全载荷比较和错误数 |

详细可执行步骤见 `E3_5_STEPS.md`。正常负载 PC→板丢帧率按 `(PC sent - 板有效唯一数)/PC sent`，板→PC 按 `(板 tx_accepted - PC board_unique)/板 tx_accepted`；须先排除重复/乱序/损坏、核对完成数和捕获窗口，不能得到负数后截零。尾部丢帧通过两端总数核对，不靠内部序号缺口。RX 耗尽阶段单独报告有意丢包，恢复新会话要求零丢包。100 fps/620 秒是稳定性负载，不能作为最大带宽结果。

## 本轮 60 字节现场证据（session 351）

PC 原始目录 `output/Ethernet/E3_5/board_60_20261117_130941`，PCAP SHA256 `034866aefbda64ea021fc883ade7743b7db0afca5eea68d4ddd5df7e7c63389f`。用户两张原始截图、PC JSON 和逐字段转录保存在 `Evidence/E3_5_20261117/board_60_run1/`。PC JSON 的 hardware_pass=false 是工具生成时尚未取得板端数据的保守标记，保留原文件，不改写为实板通过。

用户结束采样截图 active=0，valid_rx=0x3E8=1000、next_sequence=0x3E9=1001、raw_rx=1003、ignored=3；corrupt/gaps/late_or_duplicate/errors/first_error/service_result 均为 0。TX accepted=completed=0x5DB=1499、BUSY=0，与 PC 唯一收到的序号 1..1499 相等；PC 已捕获本机发送的 1000 帧，无抓包丢弃。两向帧内容、序号及数量核对 PASS，无尾部缺帧。完整 60 字节验收仍待驱动 ring.rx_errors/tx_errors/ownership_errors 和 irq_dma_error/irq_mac_error 统计及初值依据，不能把 metrics.errors=0 等同于所有硬件错误计数为零。

CCR.DC=1、memory_valid=1、调用者缓存地址为 0x24010000/0x24010600，压力循环计数 0x2D31；TX OWN mask=0、RX OWN mask=7。DMACSR=0x400 对应本芯片 ETI（Early Transmit Interrupt）状态，不是 RBU 或总线错误，不能据此单独判断异常。以上为短帧运行证据，不能关闭所有长度/长时间缓存一致性项目。

PCAP 双向时间交集约 9.354 秒，板端发包跨度约 14.976 秒，绝不是 10 分钟测试。历史 RX 0/1000 未在本次短帧会话中出现，根因仍未证明，保留 OPEN。

## 历史 RX 0/1000 与遗留项

历史截图 active=0、received=0、unique=0、missing=1000，raw_rx/irq_rx 为 0x402，之后 raw_rx 从 0x405 到 0x408；后续显式 active=1 再测曾反馈通过。这些线索能说明采样状态和后台接收活动值得排查，**不能证明历史 1000 帧抵达或根因已修复**。旧 PCAP 的对端发送记录也不能代替板端接收。

| 项目 | 状态 | 是否阻塞 E4 |
| --- | --- | --- |
| 历史 RX 0/1000 无同时段采样/CPU/Poll/OWN 证据，根因未证明 | OPEN | 是，至少需完成复现调查与本轮受控恢复验收，不能标 FIXED |
| 本轮板上固件 SHA 未独立读回、完整 MPU 属性及驱动错误/IRQ 统计待补；速率/双工和 CCR/memory_valid 已有截图 | OPEN | 是 |
| 帧长/VLAN、资源耗尽恢复、100 重启、Link 恢复尚未实测 | OPEN | 是 |
| >=600 秒实际双向重叠及错误/丢帧/数据一致性未实测 | OPEN | 是 |
| LAN8742A 模式解析仍位于 MAC 后端（已有职责问题） | OPEN | 本身不阻塞 E3.5；后续单独评估，不混入本轮驱动修改 |

新增观测可区分采样 inactive、CPU Halt/Watch 不刷新、Poll 暂停、会话/时间窗口不符与真实 DMA 停滞候选。没有复现时可记录“本轮未复现”，仍不能把历史缺陷写成 FIXED。

## 交付边界

本轮只增加 Ethernet 测试工具、生成工程脚本、报告和证据。没有修改生产 OWN/Tail/错误判定、正式 Keil/scatter、FreeRTOS、USB、Storage、CM4 OLED 或 Application V2；未操作另外两个工程，未进入 lwIP，未合并 main。保留用户原有 RTE 文件变化。本报告提交后推送 `codex/ethernet-bringup` 并核对远端；最终回执记录实际 SHA。

下一步补齐 60 字节驱动 ring/IRQ 错误统计，再按顺序进行 512 字节及余下实板项目，依据原始证据更新 PASS/FAIL。当前仍为**部分实板结果，E3.5 未关闭**。
