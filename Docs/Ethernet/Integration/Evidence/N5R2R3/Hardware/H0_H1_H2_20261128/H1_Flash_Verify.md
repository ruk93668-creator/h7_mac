# H1 Flash Verify 和寄存器读回

**H1 PASS。** 本轮 CM7 Flash Verify、MPU/DCache 与 NetworkService Watch 均通过。H2 为 NOT_RUN。

18:00:16 使用单镜像入口完成烧录。重新解析 H0 的 HEX 并逐字节比对实际 Flash 读回文件，0x08000000–0x08035387 内的 217,992 字节全部一致。CM4 首扇区在本次重试前后的 SHA-256 相同。该比较只覆盖本次重试。日志中的 Board ID/AP#3 枚举信息保留，CM7 实际读取、擦除、编程和校验已完成。[独立核验](H1_CM7_Flash_Verify_Review.json) · [原始工具日志](H1_retest/retry_20261128_180000_895066/H1_flash_programmer.log) · [下载器结果](H1_retest/retry_20261128_180000_895066/H1_program_verify.json)

## Watch 读回进度

CCR 为 0x00050200，DCache 位已置位。MPU CTRL 为 0x00000005，MPU 已启用。Region 3 的 RNR/RBAR/RASR 依次为 0x00000003、0x24000003、0x130C001D，按架构掩码得到基址 0x24000000，符合预期。Region 2/3/4/5/6 硬件读回均通过。 [原截图](H1_Watch_R3_QQ_1795860254438.png)
Region 4 的 RNR/RBAR/RASR 依次为 0x00000004、0x24078004、0x130C001D。基址为 0x24078000，核验 PASS。[原截图](H1_Watch_R4_QQ_1795860373598.png)

Region 5 的 RNR/RBAR/RASR 依次为 0x00000005、0x24010005、0x130B001F。基址为 0x24010000，核验 PASS。[原截图](H1_Watch_R5_QQ_1795860484439.png)

Region 6 的 RNR/RBAR/RASR 依次为 0x00000006、0x24020006、0x130B001F。基址为 0x24020000，核验 PASS。[原截图](H1_Watch_R6_QQ_1795860543229.png)

Region 2 的 RNR/RBAR/RASR 依次为 0x00000002、0xD0000002、0x130C002D。基址为 0xD0000000，核验 PASS。[原截图](H1_Watch_R2_QQ_1795860607896.png)


启动 H2 之前，硬件 Watch 显示 heartbeat=0x000098F8、lifecycle=0x00000001（READY）、flags=0x00000000。首次 MDIO 记录 stage=0（ETH_MDIO_NONE），其余成员均为零。该读回为启动网络前的基准状态。[原截图](H1_Watch_Network_Ready_QQ_1795860754106.png) · [H1 最终结果](H1_Final_Summary.json)

## 历史尝试（保留 FAIL）

| 时间 | 观察 | 验收判定 |
| --- | --- | --- |
| 17:38:10 | Internal DLL Error，Target DLL cancelled | 下载失败。DLL 错误的触发原因待确认。 |
| 17:38:40 | 默认加载 CM7 AXF，预下载脚本额外加载旧 CM4 HEX，工具随后报 Verify OK | 实际输入范围与本轮唯一允许的 CM7 HEX 不符。CM4 的板上内容仍待确认。 |

已查实原验收 Keil 工程 Utilities 的 Flash4 设置指向 program_cm4.ini。该脚本加载旧工程的 CM4 HEX，地址范围见 JSON。工程哈希和本轮 CM7 HEX 哈希仍匹配 H0。已有日志不能独立证明板上 CM4 版本。所有工程选项和板上状态均由用户保留，软件核验未执行下载或读取板上 Flash。

[用户粘贴的下载器记录](H1_flash_verify_user_paste_1738.log) · [H1 核验报告 JSON](H1_Flash_Verify.json) · [预下载脚本原件](H1_observed_program_cm4.ini)

## 重试准备

[CM7 单镜像重试入口和步骤](H1_retest/H1_RETEST_PREPARATION.md) · [离线核验结果](H1_retest/H1_Offline_Retest.json)

## 重试后的取证步骤

1. 确认 CMSIS-DAP 和「以太网 3」连到同一块供电的 V1 板。用下载器明确选取本目录 [h7_network_acceptance.hex](firmware/h7_network_acceptance.hex)，仅擦除文件覆盖的扇区，启用 Program 和 Verify。应留存同一路径的 Program 与 Verify 结果。若 Verify 失败，保存原始日志和截图，停在 H1。
2. 用重试准备中列出的专用 Watch 工程打开 CM7 调试。如需手工加载符号，在关闭进入调试前自动下载的情况下，用相同版本 AXF 的 LOAD NOCODE 载入符号。这样 Watch 使用本轮符号，板上身份由前一步 Flash Verify 确认。[Keil LOAD](https://www.keil.com/support/man/docs/uv4/uv4_cm_load.asp) · [Keil Flash 设置](https://www.keil.com/support/man/docs/uv4cl/uv4cl_fl_usingflashmenu.htm)
3. 运行到系统初始化完成且 g_network_status.heartbeat 已变化后暂停。保留 Watch 的 CCR、MPU_CTRL 和 g_network_status 原值。CCR 的 0x00010000 位应置位，MPU_CTRL bit 0 应置位。
4. 暂停时先记原 RNR，再依次将其选为 2、3、4、5、6。每次保存 RNR、RBAR、RASR 原值，最后恢复原 RNR。仅选择要读取的 Region，属性和基址寄存器只读。以下表格是候选源码的对照值，最终报告以实际读回为准。
5. 记录 lifecycle、flags、memory_valid、driver.state、first_fault 和 driver.first_mdio_fault 全部成员。首故障 stage 为 0 表示当前未读到有效记录。非零时保存其所有字段、计数和时间，再判断是否属于本次上电。暂时保持现状以便复审。

| Watch 写法 | 含义 |
| --- | --- |
| *((unsigned int *)0xE000ED14) | SCB CCR |
| *((unsigned int *)0xE000ED94) | MPU_CTRL |
| *((unsigned int *)0xE000ED98) | MPU_RNR 区域选择 |
| *((unsigned int *)0xE000ED9C) | MPU_RBAR 原值 |
| *((unsigned int *)0xE000EDA0) | MPU_RASR 原值 |

| Region | 基址（RBAR & 0xFFFFFFE0） | RASR 预期 | 用途 |
| --- | --- | --- | --- |
| 2 | 0xD0000000 | 0x130C002D | 8 MiB SDRAM |
| 3 | 0x24000000 | 0x130C001D | 32 KiB SDMMC DMA |
| 4 | 0x24078000 | 0x130C001D | 32 KiB Ethernet DMA |
| 5 | 0x24010000 | 0x130B001F | 64 KiB lwIP CPU 内存 |
| 6 | 0x24020000 | 0x130B001F | 64 KiB JPEG CPU 私有区 |

将原始日志保存为 H1_flash_verify.log，并保留 Watch 截图或 Keil Command 原始输出。MPU_RBAR 的低位也保留，便于复核。
