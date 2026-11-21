# Q1.1 纯软件实施与验证报告

日期：2026-11-21。审核基线：`ce27b00062cba53ea90f137acc17ee360b8383ee`。
本轮用户明确批准软件实施，替代 Q0 文档中此前“未批准 Q1 开发”的历史状态；Q0 硬件 NOT_VERIFIED 不变。
交付状态：Q1.1 软件检查已执行，待 GPT 技术审核；未下载、未执行实板通信或擦写，不进入 Q1.2。

## 1. 变更与接口

| 文件 | 作用 |
| --- | --- |
| CM7/BSP/bsp_qspi.h | 冻结四 API、阶段枚举、纯数据诊断结构，无 HAL/RTOS 公共类型 |
| CM7/BSP/bsp_qspi.c | 私有 HAL 句柄、静态状态、GPIO/RCC 准备与核验、9F 识别、清理与故障锁存 |
| CM7/Core/Inc/stm32h7xx_hal_conf.h | 只启用已有 HAL_QSPI_MODULE_ENABLED |
| MDK-ARM/h7_mac.uvprojx | 仅 CM7 添加 bsp_qspi.c/.h 与原厂 stm32h7xx_hal_qspi.c 三项 |
| Tests/QspiFlashQ1/ | Mock、实际 HAL 链接/超时验证、隔离双核构建、历史回归、范围审计、证据收集 |
| 本报告 | 接口、状态契约、真实结果与未验证范围 |

生产 main、Application、Platform、Framework、FreeRTOS 配置、其他 BSP、CM4、PLL、MPU、Scatter、原厂 HAL 实现均未修改。Q0 五份文档作为历史审计保留，本轮不扩大存储架构设计。

生产实现提交：`1f0c116`；测试及报告随后独立提交，完整最终 SHA 以 Git 提交记录和交付回复为准。

- `SysResult_t BSP_QSPI_Init(void)`：合法线程显式调用，完整识别成功返回 SYS_OK，已 READY 返回 ALREADY_EXISTS。
- `SysResult_t BSP_QSPI_DeInit(void)`：撤销 READY、归还资源；清理失败保留占用/FAULT。任何已锁存 FAULT 不因成功 DeInit 消失，本阶段没有 Recover API，需要系统复位后重新识别；未执行复位。
- `SysResult_t BSP_QSPI_GetId(uint32_t *jedec_id)`：仅返回 READY 时缓存的 24-bit ID，不重新触发事务，非法状态不修改输出。
- `SysResult_t BSP_QSPI_GetDiagnostics(BspQspiDiagnostics_t *diagnostics)`：复制快照，含阶段、READY/FAULT、MSP 次数/结果、HAL 首错、清理结果、实际配置推导频率、计时和接收长度。

NULL 返回 INVALID_ARG；ISR、PRIMASK、FAULTMASK、TIM6/NVIC 不可用或 BASEPRI 屏蔽 Tick 返回 INVALID_STATE；占用返回 BUSY。临界区只获取/释放状态占用，HAL 执行时不关中断。诊断和缓存读取也遵守同一上下文与占用约束。返回码来自既有 SysResult_t，不新增公共错误体系。

## 2. 初始化与状态

NOT_READY → PREPARE → HAL → COMMAND → RECEIVE → VERIFY → READY。
先取得本核操作权，清空本轮 MSP 标记，再由返回 SysResult_t 的私有函数配置六脚。只配置 PF6/7/10 AF9、PF8/9 AF10、PG6 AF10，AF_PP/NOPULL/VERY_HIGH；NCS 上拉由已审外部电阻提供。保存所属 pin 的寄存器位，失败/释放时按位恢复，不复位整个 GPIO 端口或关闭共享 GPIO 时钟。

拒绝抢占已经开启的 QUADSPI；取得资源后仅复位 QUADSPI 控制器以清除遗留 DMA/IRQ 配置，不发送器件复位命令。选择 D1HCLK，按实际 HAL_RCC_GetHCLKFreq 向上取整计算分频，确保目标不超过 30MHz，读回 RCC、GPIO、CR、DCR 和控制器 BUSY。240MHz 下计算得到 prescaler=7、30MHz，仅为寄存器/时钟模型推导值，未实测。

Bank1、单颗 16MiB、FlashSize=23、24-bit 容量，FIFO=1、mode0、CS high=8 周期、sample shift=none。9F 指令和数据均为单线、无地址/交替字节、无 dummy、长度3。Command 成功后先核验 DLR=2，再接收，避免 HAL 依 DLR 写出三字节缓冲边界；接收后同时检查 RxXferSize=3、RxXferCount=0、ID=EF4018、HAL 错误/状态和配置仍有效，才提交 BSP READY。

MSP 为 void，仅检查本私有句柄与准备资源，独立记录本轮执行次数和结果。HAL_OK 但 MSP 未执行/失败、HAL READY 但配置错误、错误 ID 或短接收均不能提交 BSP READY。HAL ErrorCode 在初始化中会清零，不能用它替代 MSP 独立结果。

准备失败且清理可证明成功时保持 NOT_READY，可重新尝试。进入 HAL 后任何失败保守锁存 FAULT；清理失败/状态不确定还保留资源占用。Init 无法绕过该故障；DeInit 可尝试安全清理，但不清除锁存。失败前保存原始 HAL/控制器快照，清理结果单独记录，不覆盖原始 result。

本阶段仅发 9F；未实现 Program/Erase/WriteEnable/状态写入/解锁/Flash 软件复位，也未增加普通数据读取、Quad 读取、SR1/2/3 公共接口。状态查询限于 BSP 和控制器诊断，不把它描述为器件 BUSY/保护状态已验证。

## 3. 超时及适用限制

首次 HAL_QSPI_Init RESET 路径重设 Timeout=5000ms；实际 HAL 测试验证忽略预设1ms。BSP 初始化总预算5200ms，准备阶段检查100ms，Command/Receive 各不超过100ms且不超过同一总 deadline 的剩余值；无 HAL_MAX_DELAY。实际 HAL 的等待超时返回 HAL_ERROR 并置 HAL_QSPI_ERROR_TIMEOUT，BSP 同时依据错误位映射 SYS_ERR_TIMEOUT。

清理另设100ms预算及 Tick 粒度容差；Abort 只处理 MCU 控制器，不声称中止 Flash 内部操作。若控制器仍 BUSY 或清理异常，保留 FAULT/占用。elapsed_ms 与 cleanup_elapsed_ms 分开统计，总调用耗时需相加，并计入 Tick 粒度和普通指令执行开销。所有有界性以 TIM6 中断持续运行、系统不在事务中途屏蔽 Tick 为前提；入场检查不能保证其他代码之后不暂停 Tick，此限制与现有 BSP 相同，实板最坏耗时尚未验证。

## 4. 本次软件证据

全部原始可审核日志、命令、结果和哈希清单在 `Tests/QspiFlashQ1/evidence/`。二进制及运行库仅在独立 output；重跑方式见测试 README。

| 检查 | 实际结果 |
| --- | --- |
| ARMCC5 + Unicorn BSP Mock | 51/51 用例通过，981 条实际断言，failure_line 全0；每条命令检查只允许9F |
| 真实 ST HAL/BSP/GPIO/MDMA 链接 | 0 错误、0 警告；链接图确有四 API、强 MSP、HAL Init/Command/Receive/Abort/GPIO |
| 真实 HAL 首次 Init 超时执行 | HAL_ERROR=1、ErrorCode TIMEOUT=1、Timeout=5000；5002 次模拟 Tick 调用后返回，独立检查通过 |
| 最终 CM7/CM4 正式配置重建 | verified 快照各0错误、0警告；只编译/链接/生成内部 HEX，没有执行下载 |
| 范围/风格 | CM4 target XML 完全不变；去除 CM7 三个新增文件节点后整个工程 XML 与基线一致；HAL conf 仅开关变更；生产源无堆、Task、Mutex、DMA/映射调用 |

用例覆盖正常/重复初始化、DeInit 后重新初始化、GPIO/RCC 失配、MSP 缺失/失败但 HAL_OK、HAL失败及错误位、控制器忙、错误ID、Command/Receive BUSY/ERROR/TIMEOUT、DLR错误/短接收、ISR/屏蔽/TIM6/NVIC、重入与占用、BASEPRI允许Tick、回绕、整体预算、清理失败及故障不能绕过。仿真寄存器不实现真正外设副作用，ISR 号由测试宏注入，不能宣称电气/总线/RTOS 调度已通过。

## 5. 资源变化和历史回归

| 正式镜像 | 基线 Code / RO / RW / ZI（bytes） | 最终 | 差值 |
| --- | --- | --- | --- |
| CM7 | 137690 / 3114 / 1340 / 100932 | 相同 | 0 / 0 / 0 / 0 |
| CM4 | 26302 / 1438 / 936 / 3576 | 相同 | 0 / 0 / 0 / 0 |

原因是生产启动路径未接入 QSPI，链接器移除了未引用 QSPI 代码；构建日志证明文件参与编译，不能把正式镜像的零增量解释为已接入运行。另用独立链接入口显式引用 BSP，真实 HAL 链接镜像 ROM=4428B、RAM=240B（包括测试入口/时钟替身及C库，非正式工程增量）；其中 BSP 对象静态 RW=12B、ZI=208B，合计220B。HAL MDMA 是原厂 Abort 的链接依赖，BSP hmdma 保持空，不启用 DMA。没有链接段冲突或动态分配。

三组未修改历史回归在隔离基线/verified 中执行，原始失败全部保留：

| 套件 | 基线 | 当前 | 比较 |
| --- | --- | --- | --- |
| SdramRange | 26项，18通过/8失败 | 相同 | 排除隔离路径不同的 command 字段后结果相同；旧窗口布局/非法窗口检查失败 |
| DcmiReplaceTarget | 133例，88通过/45失败，5475断言 | 相同 | cases 全部相同 |
| UsbUvcDescriptors | 汇总8项，7通过/1错误 | 相同 | 同为缺 camera_mjpeg_fifo.h 的测试构建包含路径错误；子测试8/8不代表整套通过 |

这三组套件均返回非零；没有观察到新增失败，但不能标全套回归 PASS，也未修订无关历史测试或扩大生产修改。Q1.1 的专项检查结果与这些历史失败分开供审核。

风格采用四空格、函数大括号另行、BSP_QSPI/Qspi/s_ 命名和中文关键原因注释。原两文件保留原 CRLF，`git -c core.whitespace=cr-at-eol diff --check` 验证；不批量转码或改变行尾。新增 C 为 UTF-8，公开头文件无 HAL 类型。

## 6. 后续准入

软件验证不等于实板 EF4018 已读到。实际板版本与送审 PCB 对应、U5 实装/焊接、供电与CS波形、通断、运行时 HCLK/分频/信号质量、TIM6/双核并行和通信仍 NOT_VERIFIED。
等待 GPT 技术审核及用户另行批准后，才能准备独立测试入口/下载范围并做实板只读识别；不得自动下载、启动 Q1.2 或擦写。Flash 原有内容和擦写地址保持未知且禁写，留后续单独授权。
