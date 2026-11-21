# Q0 硬件补充审计

日期：2026-11-21；基线：`f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`。
状态：NOT_VERIFIED。以下明确区分芯片能力、软件配置与实际板级连接。2026-11-21 用户补交 SVG 原理图后的核验见 §6；§1 为此前本地搜索记录，不能再解读为当前仍无原理图。原理图网络已核验，PCB/实装/测量尚未验证。

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

仍未验证：实装料号/封装与丝印、该图是否对应当前 PCB/实板版本、实际 PCB 网表与布局、EP 适用连接要求及实板处理、电源与信号测量、Flash 原有数据及写入授权。SVG 的型号文字不能证明实装型号；图中 EP 不连接也不是符合性证明。

建议下一步针对 EP 与板卡版本补充 PCB/封装库证据，并由用户/GPT 复核这次原理图结论和 Q1.1 契约后决定是否放行。Q0 总体继续 NOT_VERIFIED，本文件不授予 Q1 开发、下载或擦写权限。
