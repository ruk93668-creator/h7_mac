# R2-EP0 Hardware Clarification — 提交前核验

**VERIFIED（范围）**：基于`58aaeb367923b6a488d421e6e0a15cf26ebf09d3`，仅新增本报告及英文工单草稿。没有对外提交、生产/测试代码修改、构建或烧录。H1/H2和实板门禁不解除，软件基线仍是cc0ad357。

## 本轮新增官方证据

VERIFIED（在线PDF正文）：2026-11-22重新打开[ST英文官方PDF](https://www.st.com/resource/en/errata_sheet/es0445-stm32h745xig-stm32h755xi-stm32h747xig-stm32h757xi-device-errata-stmicroelectronics.pdf)，阅读器返回Rev7、60页；封面为October 2026，修订表p54记30-Oct-2026。§2.24.5在p44，仍列20/15 AHB周期的ZLP规避步骤。表2保留U/V共用REV_ID=0x2003；表3将该条标为A。

VERIFIED（目录元数据）：[ST同系列产品页](https://www.st.com/content/st_com/ja/products/microcontrollers-microprocessors/stm32-32-bit-arm-cortex-mcus/stm32-high-performance-mcus/stm32h7-series/stm32h747-757/stm32h747ai.html)列7.0、02-Nov-2026。目录更新日期、PDF封面月份及修订日期分别记录，不混为同一时间。

UNRESOLVED（本机字节取证）：一次原始PDF下载超时，未取得可计算SHA的本地Rev7文件；记录位于`output/TinyUSBReplacement/EP0-HardwareClarification/capture-*.json`。在线正文可核验，不等于本地PDF完整性验证已完成。未把搜索缓存中的Rev6当作当前Rev7。

UNRESOLVED（版本差异）：Rev7已读内容与用户提供的Rev6摘要关键时序一致，但没有独立取得两版固定字节做逐节比较。修订表未列USB条目变化，不能单独证明§2.24.5完全未改。

## 对粘贴草稿的修订

| 草稿条目 | 修订 | 状态 |
| -- | -- | -- |
| Rev7正文尚不可读取 | 改为本轮已读官方在线Rev7；本机原文件/SHA仍未取得 | VERIFIED / UNRESOLVED分列 |
| 要求ST提供尚不可读Rev7 | 改问Rev6到Rev7技术变化及术语/实例适用性 | 工单问题更新 |
| 芯片完整型号写成已确认 | 区分工程目标与尚未独立核验的实物丝印 | 不虚构实测 |
| 未记录已有REV_ID线索 | 列用户历史报告IDCODE=0x20036450、DEV_ID=0x450、REV_ID=0x2003，并注明本轮未读取 | 用户提供线索 |
| Slave/PIO标签 | 请ST明确原文target mode在此条的含义及USB2_OTG_FS/EP0 Status适用性 | UNRESOLVED |
| H1/H2 | 保留逐分支寄存器、终态、FIFO问题 | BLOCKED |

## 交付与门禁

INFERRED：新的版本证据能够纠正工单的资料状态，不能证明EP0停止序列。工单必须取得ST对H1/H2的具体前置条件、等待事件、超时/异常及资源退休条件答复，才可能改变BLOCKED。

英文草稿：`TinyUSB_R2EP0_HardwareClarification_ST_Support_Draft.md`。去除了粘贴文本中的转义符，并附已公开的设计证据链接。**DRAFT — NOT SUBMITTED**，供人工审核/提交；没有登录ST账户、发工单或发送邮件。

本轮没有重读整理整份RM0399，没有新硬件结果，没有实施ZLP规避时序。未独立核验的完整丝印、GSNPSID及Rev7逐字差异继续保留为UNRESOLVED/NOT_TESTED。
