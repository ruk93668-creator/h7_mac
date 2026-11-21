# Q0 硬件补充审计

日期：2026-11-21；基线：`f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`。
状态：NOT_VERIFIED。以下明确区分芯片能力、软件配置与实际板级连接。

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

| 信号 | 候选 MCU 脚 / AF | Flash 预期 pad | 板级连通性 |
| --- | --- | --- | --- |
| BK1_NCS | PG6 / AF10 | 1 | NOT_VERIFIED |
| CLK | PF10 / AF9 | 6 | NOT_VERIFIED |
| BK1_IO0 | PF8 / AF10 | 5 | NOT_VERIFIED |
| BK1_IO1 | PF9 / AF10 | 2 | NOT_VERIFIED |
| BK1_IO2 | PF7 / AF9 | 3 | NOT_VERIFIED |
| BK1_IO3 | PF6 / AF9 | 7 | NOT_VERIFIED |

本轮取得 [Winbond W25Q128JV Rev F 原厂数据手册](https://www.winbond.com/resource-files/w25q128jv%20revf%2003272018%20plus.pdf)，可用来交叉检查封装 pad 定义（§3.2/3.3）、型号（§11.1）和事务（§8）。前轮报告声称使用 Rev M，本轮访问其原厂链接失败，未将其视作本轮已复核版本。Q1 冻结时应保存适用最新版证据并复核时序差异。

规格摘要：JV 为 128 Mbit（16 MiB），2.7–3.6 V；page 256 B，sector 4 KiB；E 封装为 WSON-8 8×6 mm。IQ 的 QE 出厂固定 1；JEDEC 预期 EF 40 18，但不能证明完整订货后缀或实装型号。依据为上述手册 §1、§7.1.4、§8.1.1、§10.4、§11.1；“TR”是否对应实装采购包装、本板实际料号均未验证。

## 3. 板级审计缺口

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

候选连线不是已确认网络；30 MHz 不是板级实测值；Flash 全 FF 也不能推定该地址已获授权。先取得本板资料，再做 Q1.1 只读识别；擦写另行批准地址。不得依据本报告执行擦除、状态寄存器改写、解保护或软件复位。
