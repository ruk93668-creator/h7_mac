# N5-R2/R3 H0/H1 原始证据补归档

验收软件候选为 5768c9f9afaf248cdcf4e1d155dce11c69a71b2c。补归档前的联合分支提交为 e57b4c44cf1ae18316dd4f5943760fa6051f89a4。原验收结论和 H1 历史失败保留。H3 状态为 NOT_RUN。

## 核验结果

- 按[原始运行清单](RUN_FILE_LIST.original.json)检查 41 项文件，逐一核对存在性、字节数和 SHA-256。41 项一致，原始清单本身也按字节保留。
- H0/H1 待补原始证据缺失 0 项。补归档包括 AXF、HEX、MAP、硬件 Manifest、原始构建日志、两轮 H1 下载证据、逐字节 Flash Readback、MPU/DCache 原值、六张 Watch 原图、复测脚本及执行记录。
- Keil 打开后保存了三份工程或选项文件。使用已核验候选和原配置步骤生成文本快照，三份历史配置逐一与原先记录的 SHA-256 完全一致。现场保存版本单独列入清单。此次工作只生成配置归档，未调用编译器或下载器。
- 本目录的[新 SHA-256 清单](RUN_FILE_LIST.json)列出远端归档文件。原清单中另有 10 项 H2 分析和 HTTP 日志属于本地完整运行证据，在新清单的 local_only_h2_files 字段列出。

## 审核入口

| 类别 | 核验及原始资料 |
| --- | --- |
| H0 固件与清单 | [H0 SHA-256 报告](H0_Firmware_SHA256.md) · [原始哈希数据](H0_Firmware_SHA256.json) · [硬件 Manifest](hardware_acceptance_manifest_reference.json) · [AXF](firmware/h7_network_acceptance.axf) · [HEX](firmware/h7_network_acceptance.hex) · [MAP](firmware/h7_network_acceptance.map) |
| H0 Keil 配置 | [哈希匹配的工程](Keil/H0_verified/h7_mac_ethernet_acceptance.uvprojx) · [验收控制源码](Keil/H0_verified/service_tasks_acceptance.c) · [原始构建日志](Keil/H0_verified/build.log) |
| H1 首轮故障 | [用户提供的 Keil 原始输出](H1_flash_verify_user_paste_1738.log) · [首轮失败记录](H1_Flash_Verify.json) · [当时的 CM4 预下载脚本](H1_observed_program_cm4.ini) · [Keil 启动错误记录](H1_retest/H1_Keil_Launch_Error.json) |
| H1 单镜像复测 | [Flash Readback](H1_retest/retry_20261128_180000_895066/cm7_flash_readback.bin) · [Programmer Log](H1_retest/retry_20261128_180000_895066/H1_flash_programmer.log) · [Program Verify JSON](H1_retest/retry_20261128_180000_895066/H1_program_verify.json) · [Flash 验证复核](H1_CM7_Flash_Verify_Review.json) |
| H1 寄存器和服务状态 | [最终报告及原图索引](H1_Flash_Verify.md) · [DCache、MPU 和网络 Watch 原值](H1_DCache_MPU_Snapshot.json) · [最终结论](H1_Final_Summary.json) |
| H1 复测配置 | [Watch 工程](Keil/H1_verified/h7_mac_ethernet_h1_watch.uvprojx) · [Watch 选项](Keil/H1_verified/h7_mac_ethernet_h1_watch.uvoptx) · [符号脚本](Keil/H1_verified/h7_mac_ethernet_h1_watch_symbols.ini) · [配置记录](H1_retest/H1_Keil_Config.json) · [复测脚本及执行说明](H1_retest/H1_RETEST_PREPARATION.md) |

待独立审核。
