# STM32H747 Ethernet E2 + E3 实施报告

日期：2026-11-16。开始时实际分支为 codex/ethernet-bringup，工作区干净；HEAD 与 ls-remote 均为 b282c7cc42b1a02e154002b845cf5ab497f2a30b。PHY 历史及 eth_phy.c/h 保留不变。

## 范围与状态

| 项目 | 实现 | Cortex-M7 编译 | 软件验证 | 实板 |
| --- | --- | --- | --- | --- |
| MAC/RMII/MDIO、地址、协商模式、启停 | 完成 | 通过 | 寄存器模型通过 | 未验证 |
| TX/RX ring、buffer、OWN、完成回收 | 完成 | 通过 | ring/独立 cursor 模型通过 | 未验证 |
| DMA/MAC IRQ、耗尽、故障锁存与恢复 | 完成 | 通过 | 故障注入通过 | 未验证 |
| 专用 MPU/链接窗口 | 完成 | 地址检查通过 | 布局及属性检查实现 | 缓存开启实板流量未验证 |
| 测试固件和对端脚本 | 完成 | 固件通过 | Python 语法/CLI 通过 | 未执行 |

软件交付限定于 E2/E3；不是已经完成实板验收的生产驱动。本阶段没有 lwIP、TCP/UDP/DHCP、Network Module 或 Application V2 适配。

## 硬件与实现

接线依据用户提供原理图第 2、5 页（见 BRINGUP.md 的审图记录）：LAN8742A，25 MHz 晶体，nINTSEL 下拉选择 REFCLKO；PHY 输出 50 MHz 经 33 ohm 到 PA1。MCU 接收该参考时钟，不配置 MCO 驱动它。MDIO=PA2、MDC=PC1、CRS_DV=PA7、RXD0/1=PC4/5、TX_EN=PG11、TXD0/1=PG13/14，PG9 控制 nRST。PHYAD0 下拉推导地址 0，仍需实测 ID 和 strap。

驱动显式启用 ETH MAC/TX/RX、SYSCFG 和必要 GPIO 时钟，选择 RMII，GPIO AF11、无内部上下拉。PG9 拉低 2 ms、释放后等待 10 ms。MAC 软件复位有界等待；MDC 依实际 HCLK 选择合法分频（当前 HCLK 240 MHz 时 /102）。没有修改系统 PLL 或现有时钟树。

MAC 使用标准帧、硬件 padding/FCS insertion、接收 FCS stripping（CST+ACS，长度格式帧还会去除 padding）、store-and-forward、本站单播和广播过滤；不启用 promiscuous、checksum offload、TSO、PTP 或 jumbo。协商模式读取 PHY BCR/BSR 和 LAN8742A register 31；不把 Link Up 等同于 100/full。MAC/DMA 状态与 PHY link 通过不同接口表达。

依据 [LAN8742A DS00001989A](https://www.microchip.com/content/dam/mchp/documents/OTH/ProductDocuments/DataSheets/DS_LAN8742_00001989A.pdf) 核对 ID、reset、latch-low 和协商模式；依据 [STM32H747 RM0399](https://www.st.com/resource/en/reference_manual/rm0399-stm32h745755-and-stm32h747757-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) 和仓库实际 CMSIS/HAL 核对 MAC/DMA 字段、总线路径和配置。官方大 PDF 的本地下载超时，采用官方索引可读取章节及本仓库寄存器定义进行交叉审计；没有把其他开发板配置作为接线依据。

## 接口合同

所有任务接口由一个调用者串行执行。没有新增任务、队列或锁管理架构。

| 接口 | 结果与责任 |
| --- | --- |
| eth_mac_init | 校验 MAC/MPU、复位并识别 PHY；缺失 PHY、MDIO IO、初始化 timeout 分开返回；失败为 FAULT |
| eth_mac_start | 仅 READY；再次确认 PHY 和链路/协商模式，重建 ring 并启动 MAC/DMA；不等待 Link Up，不隐式重试 |
| eth_mac_stop | 有界 abortive reset，成功确认 quiescent 后才统计 aborted；失败不触碰 DMA-owned buffer |
| eth_mac_get_link | PHY 独立状态，包括协商完成、10/100 和 half/full |
| eth_mac_get_address | 返回驱动配置 MAC 副本 |
| eth_mac_tx_submit | 同步 copy + 异步发送；OK 取得 ticket，BUSY 不接收该帧；调用者保留原始输入 |
| eth_mac_tx_reclaim | 按提交次序返回一个 ticket 的完成；OWN 未清或无 pending 返回 EMPTY；错误完成返回 FRAME_ERROR 并计数 |
| eth_mac_rx_poll | copy-mode；EMPTY 无数据，CAPACITY 保留帧并报告所需大小；错误帧丢弃/归还容量 |
| eth_mac_service | 定期任务调用；link loss/mode change 停止；IRQ 故障在任务中 abort，随后需显式 stop/start |
| eth_mac_get_statistics | 短 IRQ 屏蔽窗口内快照；区分 IRQ event、ring frame、abort 和工作状态 |

tx_complete 只说明 DMA writeback 被软件回收；线上送达仍需对端证据。IRQ 计数可能合并多帧，不作为帧计数。计数器与 ticket 为 32-bit，自然回绕。

DMA ISR 处理 TI/RI、TBU/RBU、FBE/CDE、TPS/RPS 和 MAC RX/TX status，记录 raw status、W1C 确认并锁存故障。没有 ISR copy、descriptor 扫描、MDIO、printf 或 RTOS 调用。没有改变既有 NVIC priority。

## 验证与可移植文件

软件证据在 Tests/Ethernet/software_results.json；构建证据在 Tests/Ethernet/build_results.json。测试覆盖 descriptor 初始化、OWN、按序回收、错误/异常完成、full/empty、buffer 耗尽、重复 reclaim、错误所有权、DMA 内存别名拒绝、1000 次 wrap、独立 cursor/tail 模型每方向 3000 帧、初始化失败、PHY 缺失、Link Down、MDIO/reset/flush 超时、IRQ 故障、停止与恢复。

主机测试运行实际 eth_dma.c 和 eth_mac.c；fake_hal 只替换寄存器/板级 HAL，不模拟真实总线时序、缓存、PHY 或网络。Cortex-M7 严格 ARMClang C99 编译使用 -Wall -Wextra -Werror。完整固件使用当前工程 ARMCC 5.06u7；基线、常规 CM7 和独立帧固件分别构建。

| 完整构建 | Code | RO | RW | ZI | Error / Warning |
| --- | ---: | ---: | ---: | ---: | --- |
| b282c7c CM7 基线 | 137690 | 3114 | 1340 | 100932 | 0 / 0 |
| 常规 CM7 + Ethernet 内存配置 | 137998 | 3114 | 1396 | 113484 | 0 / 0 |
| 独立原始帧固件 | 66682 | 2834 | 1220 | 82468 | 0 / 0 |

常规工程 Flash（Code+RO+RW）增加 364 字节，RAM（RW+ZI）增加 12608 字节，其中 DMA 对象 12544 字节；另有未使用但专门保留的窗口容量，不算已使用 ZI。该映像尚无 Application 调用 MAC，链接器会移除未引用 MAC/DMA 函数，不能把这个差值当作完整驱动成本。独立帧固件实际链接和调用收发路径；由于它替换 Application 入口，其总大小不能直接与生产基线比较。两个最终构建均检查 g_eth_dma_memory=0x24078000/12544 bytes，源码哈希稳定。

rx_errors 是交给 DMA 的异常 descriptor 帧计数；MTL 在 DMA 前直接丢弃的 CRC/过滤帧不保证进入该计数。硬件验收还须读取 MMC CRC/alignment 计数并结合对端 PCAP；不能以软件错误计数为零宣称线上零丢包。正常计数不包含尚未 reclaim 的完成帧。

便携核心：Platform/Ethernet/eth_phy.c/h、eth_dma.c/h。H747 移植层：eth_mac.c/h、eth_memory.c/h；需按目标板适配 GPIO、PHY 地址、时钟、linker 和 MPU。移植不需要 Application/Media/Framework/RTOS 代码。测试文件不属于生产移植清单。

详情：内存布局、缓存审计和状态图见 MEMORY_AND_OWNERSHIP.md；公共配置影响见 CONFIGURATION.md；八步实板验收见 HARDWARE_VALIDATION.md。当前停在 E2/E3，等待人工审核和硬件验收，不进入 E4。
