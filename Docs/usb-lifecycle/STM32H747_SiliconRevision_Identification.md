# STM32H747 Silicon Revision 身份核验

## 2026-11-19 后续正文核验更新

**VERIFIED（PDF/正文）**：RM0399 Rev5完整原件现已保存；2747页图794确认USB2 OTG_HS别名OTG_FS，2780页DMAEN定义与当前slave/PIO配置相符。此前本文“RM缺件/实例映射缺证据”状态已由[RM正文核验报告](RM0399_Revision_and_EndpointTiming_Analysis.md)更新。REV_ID仍为截图及两次用户确认的0x2003（U/V组）；精确工具版本/CPU状态/实物丝印与Reset/Download过程偏差未因手册取得而关闭。**NOT_TESTED**：实际勘误触发与Endpoint停止。

> 基线：`3a765feeee8d4cb3c1cc553337caf8cf1b63f6b4`；研究分支：`codex/usb-device-lifecycle-manager`；记录日期：2026-11-19，Asia/Shanghai。
> VERIFIED须区分用户截图、用户陈述、源码、官方手册；INFERRED为推断；UNRESOLVED为缺证据；NOT_TESTED为未验证。

## 1. 原始值与三次一致性

**VERIFIED（用户截图）**：Keil Memory显示地址`0x5C001000`开始的四个字节按地址顺序为`50 64 03 20`。按小端解码，完整32位值为`0x20036450`。保留原始值，不丢弃中间保留位。

| 观察 | 完整32位值 | 证据与限制 |
| -- | -- | -- |
| 第一次 | `0x20036450` | VERIFIED：用户提供的Memory截图 |
| 第二次 | `0x20036450` | VERIFIED：用户陈述刷新后相同；没有第二张独立截图或导出 |
| 第三次 | `0x20036450` | VERIFIED：用户陈述再次刷新相同；没有第三张独立截图或导出 |

**VERIFIED（陈述）**：用户回复“刷新了两次结果一样”。因此可记录三次一致，但不能写成代理独立实时读取三次。

原始截图：`D:/qqwenjian/Tencent Files/2906233823/nt_qq/nt_data/Pic/2026-11/Ori/fc4eca5a35c34ba18c7b9e17f14e99f2.png`。

本机证据副本：`D:/32/STM32H7/h7_mac_lifecycle/output/MSC24/udlm-hardware-evidence/user_idcode_read1.png`。

截图SHA256：`65e5a9434675e9ad30c5fac34d65e4f05a2a153be2ee3ebc73450f78dbddd9e5`。

原始记录：`D:/32/STM32H7/h7_mac_lifecycle/output/MSC24/udlm-hardware-evidence/idcode_read1.json`。首次入库时间2026-11-19 17:31:43.629890 +08:00，更新记录17:33:27.103228 +08:00；这些是证据记录时间，实际三次读取时间均UNRESOLVED。原始证据保存在忽略的output目录，不将其作为构建产物上传Git。

## 2. 解码与型号边界

**VERIFIED（数值计算）**：

```text
IDCODE = 0x20036450
DEV_ID = IDCODE & 0xFFF = 0x450
REV_ID = (IDCODE >> 16) & 0xFFFF = 0x2003
```

**VERIFIED（源码）**：`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h:2542`给出DBGMCU基址；同文件`:25057`给出预期DEV_ID，`:25061`为低12位DEV_ID掩码，`:25064`为高16位REV_ID掩码。`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c:466`通过右移16位获取修订号。

**VERIFIED（官方勘误）**：ES0445 Rev7，October2026，60页，第1页表2将`0x2003`对应到U/V修订组，`0x2007`对应T/4/5/6组。当前记录属于U/V组，不能仅凭此数值区分U与V，也不能因为HAL符号叫REV_ID_V就断言实物为V。

**UNRESOLVED**：完整实物型号/封装/订货后缀及丝印未提供。`MDK-ARM/h7_mac.uvprojx:3098`配置为STM32H747XIHx:CM7，但配置不等同实物身份；DEV_ID也是系列识别值，不独立证明完整型号。

## 3. 调试环境与过程偏差

| 项目 | 记录与状态 |
| -- | -- |
| 调试器 | VERIFIED（用户陈述）：DAPLink；不是ST-Link |
| DAPLink型号/固件/连接协议 | UNRESOLVED：未提供，不推断SWD/JTAG |
| Keil版本 | UNRESOLVED：用户仅称“应该是最新”，无数字版本；不使用本机构建工具版本代替实际会话版本 |
| CPU读取前/后Running或Break | UNRESOLVED：未提供 |
| Reset/Download | VERIFIED（用户陈述）：进入了自动Reset/Download的新会话；是否下载成功、具体镜像及范围未独立核实 |
| 代理动作 | VERIFIED（工具过程）：UI工具初始化失败，没有对目标执行读取、暂停、复位、下载或寄存器写入 |
| 三次读取是否独立留档 | 第一次截图，其余两次仅用户陈述 |

**UNRESOLVED（过程符合性）**：任务要求在不复位/下载的新动作下读取；用户已报告自动Reset/Download，所以本次不能作为“全程无复位/无下载”的合规过程证据。保留读数用于身份分析，同时明确该偏差，不声称恢复了先前CPU状态或原先故障现场。代理没有要求再次复位或下载。

## 4. ES0445适用性边界

官方来源：[ST ES0445 Rev7](https://www.st.com/resource/en/errata_sheet/es0445-stm32h745xig-stm32h755xi-stm32h747xig-stm32h757xi-device-errata-stmicroelectronics.pdf)。

**VERIFIED（手册）**：表3第5页对上述修订组列出2.24.2及2.24.5适用并有规避措施；实际触发还取决于控制器实例与运行条件。

| 条目 | 官方位置及条件 | 当前结论 |
| -- | -- | -- |
| 2.24.2 | 第43页，ISO IN在Buffer DMA取包期间禁用的条件 | VERIFIED（源码）：`CM7/BSP/bsp_usb.c:108`关闭USB DMA。INFERRED：不能将该DMA专用规避顺序直接作为当前PIO停止算法 |
| 2.24.5 | 第44页，ZLP相关限制覆盖Buffer DMA与slave模式 | INFERRED：不能因PIO就排除此项；具体实例和控制传输/数据路径仍需RM0399核验 |

**UNRESOLVED**：上述勘误不是完整Endpoint停止契约，未取得RM正文时，不能据此认证Bulk IN/OUT、ISO IN或EP0的停止证明。当前实例源码为USB2_OTG_FS、embedded PHY、FULL speed、USB DMA关闭（`CM7/BSP/bsp_usb.c:99`、`:105`、`:108`、`:111`），仍需核验手册中的实例映射。

## 5. 本轮关闭项与保留项

- **VERIFIED（限定来源）**：有实际用户截图的IDCODE，DEV_ID/REV_ID解码及官方修订组映射；用户确认额外两次一致。
- **UNRESOLVED**：RM0399完整原件、停止时序、完整实物型号、精确调试工具版本、CPU状态、独立第二/三次留档，以及无复位/下载过程要求未满足。
- **NOT_TESTED**：代理实时三次读取、任何新Endpoint停止实现及板级停止/重连验收。
- **UNRESOLVED（交付判定）**：身份数值缺口已有受限定的证据补充，但硬件证据闭环整体未完成；不进入P1-B/P2。
