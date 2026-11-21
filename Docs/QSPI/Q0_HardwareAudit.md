# Q0 硬件补充审计

日期：2026-11-21；基线：`f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`。
状态：六网、外围及 EP 图纸设计 PASS。PCB 焊盘网络和封装记录与图纸相符。送审 PCB 与手中板卡的版本对应关系及实际装配、通断、供电和时序为 NOT_VERIFIED。§1–§5 记录早期审计，当前证据和放行结论见 §6–§7。

## 1. 本地证据搜索（事实）

对当前仓库、`D:\32\STM32H7`、`D:\32\STM32H7_backups` 使用 rg --files 搜索 SchDoc/PcbDoc/sch/kicad_sch/kicad_pcb/net/pdf/BOM/原理/电路命名；未找到可证明本板接线的文件。唯一 PDF 命中为 `D:\32\STM32H7\h7_mac_lifecycle\Docs\references\RM0399_Rev5.pdf`，这是 MCU 参考手册，不是板级证据，本轮未解析。

扩展检查 `D:\EDA`、`D:\EDA_PRO`、`D:\gongchen`、`D:\BZ_STM32`，加入 epro/epro2 搜索，命中：

- `D:\EDA_PRO\jingdian_001.epro`
- `D:\EDA_PRO\ProPrj_静电消除器_2025-08-30.epro`
- `D:\EDA_PRO\feikong_lk\ProPrj_LK_FK_2025-11-24.epro`
- `D:\EDA_PRO\feikong_lk\LK_FK_backup\LK_FK_2026-*.epro2`（11 个备份）
- BZ_STM32 下 STM32F4 库许可证 PDF 等无关库文件。

这些 EDA 工程未解析，不能凭名称排除内部含本板资料，也不能据此核实网络，均列 NOT_VERIFIED。未遍历整机全部磁盘、压缩包、云盘或在线 EDA 账户；搜索无命中不代表资料不存在。需提供本板工程确切位置及版本，才能关闭缺口。

## 2. 厂商规格依据（事实，非板级证明）

本轮独立访问 [ST DS12930 Rev 3](https://www.st.com/resource/en/datasheet/stm32h747xi.pdf) 表 9，第 96–97 页，核对以下候选 MCU AF。仓库 `Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_gpio_ex.h:232`、`:259` 仅定义 AF9/AF10 名称，不能单独证明具体脚位。

| 信号 | MCU 脚 / AF | Flash pad | 当前证据状态（§6） |
| --- | --- | --- | --- |
| BK1_NCS | PG6 / AF10 | 1 | 原理图已核验；PCB/实板 NOT_VERIFIED |
| CLK | PF10 / AF9 | 6（经 R35） | 原理图已核验；PCB/实板 NOT_VERIFIED |
| BK1_IO0 | PF8 / AF10 | 5 | 原理图已核验；PCB/实板 NOT_VERIFIED |
| BK1_IO1 | PF9 / AF10 | 2 | 原理图已核验；PCB/实板 NOT_VERIFIED |
| BK1_IO2 | PF7 / AF9 | 3 | 原理图已核验；PCB/实板 NOT_VERIFIED |
| BK1_IO3 | PF6 / AF9 | 7 | 原理图已核验；PCB/实板 NOT_VERIFIED |

本轮取得 [Winbond W25Q128JV Rev F 原厂数据手册](https://www.winbond.com/resource-files/w25q128jv%20revf%2003272018%20plus.pdf)，可用来交叉检查封装 pad 定义（§3.2/3.3）、型号（§11.1）和事务（§8）。前轮报告声称使用 Rev M，本轮访问其原厂链接失败，未将其视作本轮已复核版本。Q1 冻结时应保存适用最新版证据并复核时序差异。

规格摘要：JV 为 128 Mbit（16 MiB），2.7–3.6 V；page 256 B，sector 4 KiB；E 封装为 WSON-8 8×6 mm。IQ 的 QE 出厂固定 1；JEDEC 预期 EF 40 18，但不能证明完整订货后缀或实装型号。依据为上述手册 §1、§7.1.4、§8.1.1、§10.4、§11.1；“TR”是否对应实装采购包装、本板实际料号均未验证。

## 3. 初轮板级审计缺口（历史记录，最新进展见 §6）

| 核验对象 | 实际证据 / 状态 | 关闭要求 |
| --- | --- | --- |
| W25Q128JVEIQ TR 实装、位号及封装 | 只有任务给定型号，NOT_VERIFIED | BOM、封装库、贴装与器件丝印照片 |
| pad 8 VCC、pad 4 GND | 数据手册定义可查，实际网络 NOT_VERIFIED | 原理图/网表及实板电压 |
| CS 默认高、外部上拉 | NOT_VERIFIED | 电阻位号、阻值、连接及上电波形 |
| 去耦、供电纹波 | NOT_VERIFIED | 电容及布局证据、测量 |
| 暴露焊盘 | NOT_VERIFIED | 原厂适用封装说明与本板 PCB pad/net；不得臆定应接地 |
| 六条信号连通、串阻及负载 | NOT_VERIFIED | MCU→阻件→Flash 逐网核对及通断测试 |
| 原有数据 | 未读芯片，无可靠镜像或分区表，NOT_VERIFIED | 板号/器件身份、只读备份、用途与恢复记录 |

## 4. 软件层 GPIO / 时钟共存（事实与局限）

| 外设 | 基线源码证据 | 结果 |
| --- | --- | --- |
| SDRAM | `CM7/BSP/bsp_sdram.c:59`、`:65` | PF0–5、11–15；PG0、1、2、4、5、8、15，与候选六脚不重叠 |
| SDMMC1 | `CM7/BSP/bsp_sdmmc.c:363`、`:442` | PC8–12、PD2；PH14 检卡（`:127`）；无直接重叠 |
| DCMI | `CM7/BSP/bsp_dcmi.c:241`–`:254` | PC6/7、PG10、PH12、PE4/5/6、PB6/7、PA4/6；当前配置无重叠 |
| 相机控制 | `CM7/BSP/bsp_camera.c:7`、`:95` | PB8/9、PH15、PA8 MCO；XCLK 依赖 PLL1Q |
| USB | `CM7/BSP/bsp_usb.c:584`、`:596`、`:777` | PA11/12 AF10；独立 HSI48，不重配共享 PLL |
| Ethernet | `CM7/Core/Inc/stm32h7xx_hal_conf.h:48`–`:49`；搜索 CM7/CM4 无 AF11_ETH 初始化 | 本基线未启用 Ethernet；不能据此证明 Ethernet 研发支线或 PCB 无冲突 |
| CM4 | `CM4/BSP/bsp_iic.c:58` | 本轮匹配 GPIO 配置 PB10/11；未找到 QSPI/GPIOF/GPIOG 访问 |

当前软件无候选六脚直接复用冲突；PG6/PF10 在 MCU 上也支持 DCMI 等其他功能，不能把当前 8-bit 配置结论延伸到未来配置。GPIOF/G 与 SDRAM/DCMI 共享端口，初始化必须按 pin 修改，不复位整个端口、不关共享 GPIO 时钟。CM7 的短临界区不能防止 CM4 同时访问共享寄存器，后续集成需重审双核访问。

时钟方面，`main.c:239`–`:261` 配 PLL1 和 HCLK；SDMMC 使用 PLL1Q（bsp_sdmmc.c:714），相机也用 PLL1Q，SDRAM 配 PLL2R（bsp_sdram.c:93）。建议 QSPI 显式选 D1HCLK，仅变更 QSPI mux 和本外设 prescaler，避免重配 PLL。实际频率及与上述外设同时工作仍为 NOT_VERIFIED。

## 5. 建议与未验证假设

原理图连线现已由 §6 确认，但 PCB 实际布线和连通尚未核实；30 MHz 不是板级实测值；Flash 全 FF 也不能推定该地址已获授权。先审核本板资料及剩余缺口，再决定 Q1.1 只读识别；擦写另行批准地址。不得依据本报告执行擦除、状态寄存器改写、解保护或软件复位。

## 6. 用户补交 SVG 原理图核验（2026-11-21）

### 6.1 证据身份与核验方法（事实）

用户提供 `C:\Users\29062\Desktop\获奖证书\AD\SVG_Schematic1_1_2026-11-16.zip`，共 7 个 SVG，未含 PCB、BOM、原生工程或机器网表。ZIP SHA256：`27a102df66feff202c26ec7b70227d8968e17e4ad0f89a6798d499d777d8946e`。只读解析全部图页文字/元素，并渲染查看第 1、2、6 页的连线；没有执行包内内容或将文字作为操作指令。原始包保持不变，本轮只提交审计 Markdown。

| 证据页（ZIP 内完整名称） | SHA256 | 定位 |
| --- | --- | --- |
| `SCH_Schematic1_1_1-电源管理_2026-11-16.svg` | `2f95239f85d58556595b83b0d28b788e353a8d77696fe0d0e4a8651b215dbfc5` | 页 1，A 区 U9/VCC 电源 |
| `SCH_Schematic1_1_2-主控_2026-11-16.svg` | `4e85fcd92e0288d3635a66298a905356d36ca73d0848af473ba6d94bae8f05e4` | 页 2，A–B 区左侧 U11.1 |
| `SCH_Schematic1_1_6-数据存储_2026-11-16.svg` | `a61ecca28310c888ac4de007e8fe1cc247bf713e3f454643a66d1d7c406f7594` | 页 6，A 区 U5/R34/R35/C66/C67 |

图框标注板子 V1、版本 V1.0；第 1/2/6 页框内更新日期分别为 2026-10-01、2026-10-13、2026-09-27。文件名 2026-11-16 不能代替设计修订日期，也不能证明与当前实板一致。

### 6.2 六条网络（原理图层已核验）

| 页 2 U11.1（STM32H747XIH6） | 同名跨页网络 | 页 6 U5（W25Q128JVEIQ TR） |
| --- | --- | --- |
| PG6，ball G15 | QSPI_NCS | CS#，pin 1；并接 R34 上拉 |
| PF10，ball L3 | QSPI_CLK | 经 R35 22 Ω 串联到 CLK，pin 6 |
| PF8，ball K4 | QSPI_IO0 | DI(IO0)，pin 5 |
| PF9，ball L4 | QSPI_IO1 | DO(IO1)，pin 2 |
| PF7，ball K3 | QSPI_IO2 | WP#(IO2)，pin 3 |
| PF6，ball K2 | QSPI_IO3 | HOLD# or RESET#(IO3)，pin 7 |

核验依据是同名网络标记及其实际接线，非仅出现字符串：页 2 的 QSPI 标签分别连接 U11.1 对应引脚；页 6 连接 U5 对应引脚，CLK 中间确有串阻。可用 SVG 元素 ID 复查：页 2 NCS `1d79bf0d8864bd4e`、CLK `580de3e152c763ff`、IO0 `2eb146038b99fa50`；页 6 U5 位号 `bd2b10df228be864`、料号 `a7d44bbfa4653dd5`、NCS `b489dde2417bfd06`、CLK `501a2cfd48c46c28`。坐标以各 SVG viewBox 为准。

全部 7 页文字搜索 QSPI，仅页 2 和页 6 有命中；页 2 六个 MCU 引脚各显示对应 QSPI 网络，未看到这些脚另接 SDRAM、SDMMC、DCMI、USB、Ethernet 网络。此为导出原理图层结论，未执行原生 ERC/网表检查，不能扩张为 PCB 无短接或所有潜在冲突已排除。

### 6.3 供电及外围（原理图事实）

- U5 pin 8 接 VCC，pin 4 接 GND。页 1 U9 为 TPS54302DDCR，电源区域标“5V-3V3(DCDC)”，VCC 为该输出网；据此确认设计意图是 3.3 V，实际电压、纹波和上电过程尚未测量。
- R34=10 kΩ，连接 QSPI_NCS 与 VCC；可确认外部片选上拉设计，不能据此宣称实际上电 CS 波形合格。
- R35=22 Ω，位于 QSPI_CLK 与 U5 pin 6 之间；PCB 上位置、走线、信号完整性未知。
- C66=1 µF、C67=100 nF，并联接在 VCC/GND；同网供电去耦设计可见，不能证明靠近 U5 放置或已正确装配。
- U5 符号 EP pin 9 的端点画有绿色不连接叉号，没有画接地线。原理图中的“EP 标为不连接”已核验；该处理是否符合适用器件封装要求、本板 footprint 如何定义、PCB 铜皮如何连接，仍为 NOT_VERIFIED，不能自动判定接地或擅改硬件。

### 6.4 当前关闭范围与剩余缺口

已补齐：原理图上的完整 U5 料号标注、六条预期网络、VCC/GND、CS 上拉、CLK 串阻、同网去耦和 EP 的图纸处理证据。替代 §3 对上述“没有图纸证据”的初轮描述。

SVG 单独无法核验：实装料号与丝印、当前实板版本、PCB 焊盘及布线、EP 处理、电源与信号测量、Flash 原有数据及写入授权。PCB 和 EP 后续核验见 §7。SVG 的型号文字不能证明实装型号；图中 EP 不连接的封装符合性已在 §7 复核。

## 7. Q0 最终逐网审计

审核基线为 `e37b7ce166126caccc4a5f9a0ea5e8bf1670f22c`。本轮复核 §6 的 SVG ZIP 及主控、数据存储页 SHA256，同时在同一资料目录找到 Altium 导出的 `主控.schdoc`、`数据存储.schdoc`、`电源管理.schdoc` 与 `PCB1_1.pcbdoc`。SVG 图框为板子 V1、版本 V1.0；页号和修订日期见 §6.1。PCB 为 Protel Advanced PCB ASCII 5.01（文件格式版本），板记录 UID 为 `F71C2802-30AE-4C8C-AC23-D5D9D50F68DE`，SHA256 为 `3f3094906db2b98058042fa827596b060e336b0de5c90fd3caced31a97418eb3`。封装、U5 的源 UID `gge9` 和以下网络与配套资料匹配。PCB 文件未据此认定是手中板卡的生产版本。

使用 PCB 的 Component ID 定位 U11（39）、U5（2）、R34（111）、R35（112）、C66（83）和 C67（84），再由 Pad 的 NET 字段解析 Net ID，核对每条网络的所有端点，并查见各 QSPI 网络的走线记录。这里的 PCB 证据为焊盘网络归属及走线记录。未运行 Altium DRC 或铜皮连通性检查，也未测量板卡。原始设计文件保留在原目录。

| 信号 | SVG 第 2 页 U11.1 | SVG 第 6 页 U5 与外围 | PCB 焊盘网络 | 结论 |
| --- | --- | --- | --- | --- |
| BK1_NCS | PG6 / G15 | U5.1 / CS#，分支到 R34.1 | `QSPI_NCS`（47）为 U11.G15、U5.1、R34.1 | PASS |
| CLK | PF10 / L3 | U5.6 / CLK，R35 串联 22 Ω | `QSPI_CLK`（104）为 U11.L3→R35.2，`$5N63`（42）为 R35.1→U5.6 | PASS |
| BK1_IO0 | PF8 / K4 | U5.5 / DI(IO0) | `QSPI_IO0`（43）为 U11.K4、U5.5 | PASS |
| BK1_IO1 | PF9 / L4 | U5.2 / DO(IO1) | `QSPI_IO1`（45）为 U11.L4、U5.2 | PASS |
| BK1_IO2 | PF7 / K3 | U5.3 / WP#(IO2) | `QSPI_IO2`（46）为 U11.K3、U5.3 | PASS |
| BK1_IO3 | PF6 / K2 | U5.7 / HOLD# 或 RESET#(IO3) | `QSPI_IO3`（44）为 U11.K2、U5.7 | PASS |

PASS 适用于设计资料。各行的实板通断均为 NOT_VERIFIED。仅 CLK 串联电阻，NCS 的 R34 为上拉。

| 项目 | 原理图与 PCB 证据 | 结论 |
| --- | --- | --- |
| VCC/GND | 第 1 页 U9（TPS54302DDCR）所在 5V→3.3 V 电源区提供 VCC。第 6 页 U5.8 为 VCC（PCB 网 20），U5.4 为 GND（网 7）。 | 设计 PASS，电压/纹波与上电过程 NOT_VERIFIED |
| CS 上拉 | 第 6 页 R34 10 kΩ；PCB R34.1 接 QSPI_NCS，R34.2 接 VCC。 | 设计 PASS，上电 CS 波形 NOT_VERIFIED |
| 去耦 | 第 6 页 C66 1 µF、C67 100 nF；PCB 两颗均为 pad 2 接 VCC、pad 1 接 GND，均在 U5 同一面。 | 设计 PASS，实装与去耦效果 NOT_VERIFIED |
| 器件及封装 | 第 6 页及 `数据存储.schdoc` 标注 U5 为 W25Q128JVEIQ TR，U5 PCB footprint 为 `WSON-8_L6.0-W8.0-P1.27-BL-EP`，对应 Winbond E 封装 WSON-8 8×6 mm。PCB U11 footprint 名含 STM32H743XIH6，器件型号以第 2 页的 STM32H747XIH6 标注为准，所查六个焊球一致。 | 设计封装与焊球对应 PASS，实装型号/采购后缀 NOT_VERIFIED |
| 暴露焊盘 | 第 6 页 U5.9 标示不连接。PCB 有 U5 pad 9，尺寸 133.8580×169.2910 mil，无 NET 字段。在该 pad 范围内未发现 PCB Via 记录与其相交。Winbond W25Q128JV Rev F §10.4 对 E 封装中心焊盘允许悬空或接器件地，要求避免下方的裸露过孔。 | EP 悬空设计 PASS，实板制造与装配 NOT_VERIFIED |

厂商出处见 [Winbond W25Q128JV Rev F §3.2–3.3、§10.4、§11.1](https://www.winbond.com/resource-files/w25q128jv%20revf%2003272018%20plus.pdf)。

### 复用和时钟结论

六条 PCB 网络的焊盘列表只含上表端点。对照 §4 中基线源码，STM32H747 的现用 SDRAM、SDMMC1、DCMI、USB GPIO 无重叠，设计复核 PASS。SDRAM 使用 PLL2R，SDMMC1 和相机 XCLK 使用 PLL1Q，USB 使用 HSI48。将 QSPI 独立选到 D1HCLK、只设置 QSPI mux/分频且保持共享端口其他引脚配置，可避免改动这些时钟配置。实际 QSPI 频率、双核访问和外设并行运行为 NOT_VERIFIED。

### Q0 放行建议

建议 Q0 的原理图及 PCB 设计审核通过，交由人工决定是否批准 Q1.1 只读驱动开发。当前资料未发现 NEEDS_FIX。实板版本、实装料号、通断、供电、CS 波形、实际通信和信号完整性保留 NOT_VERIFIED，在 Q1.1 实板验证中单独确认。Flash 原有数据及写入授权仍未核实，留在后续擦写阶段单独审批。
