# Q0：Q1 实施边界建议

日期：2026-11-21；源码基线：`f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`。
状态：设计待审核，未实施，未获 Q1 批准。事实详见 Q0_ArchitectureAudit.md；硬件前提未关闭。

## 1. 具体变更清单（建议）

| 文件 | Q1 候选动作 | 限定 |
| --- | --- | --- |
| `CM7/BSP/bsp_qspi.h` | 新增 | SysResult_t API、常量、独立诊断，不公开 HAL/RTOS 类型 |
| `CM7/BSP/bsp_qspi.c` | 新增 | 私有 HAL 句柄、GPIO、时钟、间接轮询事务、故障锁存 |
| `CM7/Core/Inc/stm32h7xx_hal_conf.h` | 最小修改 | 仅启用第 67 行已有 QSPI 开关 |
| `MDK-ARM/h7_mac.uvprojx` | 最小修改 | 仅 h7_mac_CM7 target 的 HAL/BSP 分组增加对应 .c；不改 CM4 段 |
| `Tests/QspiFlashQ1/` | 新增 | mock、隔离测试固件入口、测试工程/脚本、范围审核和验收记录 |
| `Docs/QSPI/` | 更新 | 分步结果、硬件与地址授权记录 |

禁止顺带修改：生产 main.c、CM7/CM4 Application、ApplicationV3、Platform/Service/Framework、FreeRTOSConfig.h、MPU、Scatter、PLL1/PLL2 配置、SDRAM/SDMMC/DCMI/USB/Ethernet 模块、ST 驱动实现、CM4 HAL 配置、.ioc。若发现必需修改，先停止相关实施并补充具体差异供审核。

## 2. HAL、时钟和初始化

事实：HAL QSPI 尚未启用，工程未列源文件（ArchitectureAudit §3）。新增 BSP 包含既有 stm32h7xx_hal.h，复用原厂 Command/Receive/Transmit，采用 Bank1、24-bit 地址、间接模式；不启用 DMA、IRQ 异步或 memory mapped。

时钟建议：显式使用 `RCC_QSPICLKSOURCE_D1HCLK`，通过 `__HAL_RCC_QSPI_CONFIG` 仅修改 QSPI mux，读回 `__HAL_RCC_GET_QSPI_SOURCE()` 并检查实际 HCLK。依据 `Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_rcc_ex.h:1365`、`:2981`。不要提交包含 PLL2 参数的外设时钟初始化结构来借用 PLL2。

`HAL_RCCEx_GetPeriphCLKFreq()` 实现起于 `Src/stm32h7xx_hal_rcc_ex.c:1881`；全文只有配置和 GetPeriphCLKConfig 使用 RCC_PERIPHCLK_QSPI，无 QSPI 频率查询分支，不应盲调该通用函数。若读回 D1HCLK，用 HAL_RCC_GetHCLKFreq()；若遇其他 mux，Q1 初版应拒绝未知状态或在批准后按 PLL1Q/PLL2R/CLKP 的实际源分支计算。

主线 `CM7/Core/Src/main.c:239`–`:261` 给出 HSI64/4×60/2=480 MHz SYSCLK、HCLK=240 MHz 的静态推导；实际成功运行仍待验证。分频建议 `ceil(kernel_hz/target_hz)-1`，检查可表示范围；仅在 kernel=240 MHz 时 prescaler=7 对应 30 MHz。读取实际 DCR/CR 验证分频、Bank、FlashSize；16 MiB 对应 HAL FlashSize=23，需 mock 验证。CS high time、sample shift、dummy cycles 待适用手册及信号质量核定，不能因频率可算就认定通信通过。

Q1 不需要新增生产 Initcall。初始化由隔离测试固件的合法任务显式调用；Q1.1 只做只读识别。既有 Initcall 在调度前执行（main.c:174/179/194），以后若 Q2 注册静态同步资源可复用 PLATFORM 等级，不能在其中执行 Flash 长耗时操作。DeInit 不关闭共享 GPIOF/G 时钟。

### 2.1 MSP 失败传播与硬件准备契约（建议，未实施）

已核验事实：`Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_qspi.h:564` 将 `HAL_QSPI_MspInit()` 声明为 `void`；实现 `Src/stm32h7xx_hal_qspi.c:332`–`:355` 仅在句柄 RESET 时调用 MSP，未接收任何 MSP 结果；弱实现位于 `:438`。当前回调注册开关为 0（`CM7/Core/Inc/stm32h7xx_hal_conf.h:207`）。HAL 随后直接写控制寄存器，在自身初始化通过后清除 ErrorCode 并将句柄设为 READY（qspi.c:362–388）。因此不能通过修改句柄 ErrorCode/State 期待可靠传递 MSP 失败，`HAL_QSPI_Init() == HAL_OK` 或 HAL 句柄 READY 均不是 BSP 的完整成功证明。

推荐采用“返回 SysResult_t 的 BSP 显式硬件准备 + MSP 只核验 + HAL 返回后独立复核”，不修改 ST HAL 源码、不增加公共错误码：

1. 在 BSP 原子取得本次操作权，保持 BSP NOT_READY；保存本轮阶段、硬件准备结果、MSP 检查结果和清理结果。MSP 检查结果初始为未执行，不能沿用上一次成功值。上下文、TIM6、参数、已有占用及硬件批准前提不满足时，直接返回相应 SYS_ERR_*，不触碰 Flash。
2. 私有 `Qspi_PrepareHardware()` 建议返回 SysResult_t，在 HAL Init 之前完成本模块时钟/GPIO 准备。仅配置批准的六脚和 QSPI mux/时钟门控，不复位整个 GPIO 端口或共享 PLL；逐项登记已获取资源。HAL_GPIO_Init 等 void 配置调用也必须通过 RCC 和 GPIO MODER/AFR/OTYPER/PUPDR/OSPEEDR 等相关位的读回核验，不能虚构其返回值。时钟源、实际频率、GPIO 配置任一不符，保存首错和快照，按已取得资源有限清理，不再调用 HAL_QSPI_Init 或发送 Flash 命令。
3. 准备成功后再对私有句柄调用 HAL_QSPI_Init。BSP 提供的 void MSP 钩子仅处理本私有句柄及 QUADSPI，确认显式准备已成功并复核相关资源；结果写入独立静态诊断字段，而非 HAL ErrorCode。钩子不重复配置 GPIO、不重配 PLL、不发 Flash 命令，对非本模块句柄不操作。该旁路状态不是阻止 HAL 继续执行的返回通道，所以一切可能失败的配置必须提前完成；不能把它当作在 MSP 中执行危险配置后的补救机制。
4. HAL 返回后同时检查硬件准备结果、MSP 已执行且成功、HAL 返回/状态/错误、控制器空闲，以及 RCC/GPIO/CR/DCR 的关键配置读回。Q1.1 的首次识别或经完整清理后的重新识别必须使用已确认 RESET 的私有句柄，保证本轮 MSP 确实执行；已经 BSP READY 的重复 Init 直接返回 SYS_ERR_ALREADY_EXISTS，故障态不靠再次调用 Init 绕过恢复。如果 HAL_OK 但 MSP 未执行、MSP 失败或配置不符，也必须返回失败，禁止继续 JEDEC 事务。
5. 仅在上述核验通过后进行只读 JEDEC（以及本阶段批准的只读状态检查），最后再复核故障与配置并提交 BSP READY。任一阶段失败均保持 NOT_READY；原始结果、MSP 结果与 cleanup_result 分开保留，清理失败锁存 FAULT，不能通过 DeInit 或重新初始化清除未处理故障。清理只回收本模块已确认取得的资源；资源状态不确定时保留占用并要求显式恢复。

GPIO/RCC 读回只证明 MCU 配置符合设计，不能证明 PCB 连通、供电、信号质量或实装型号。2026-11-21 补交原理图已核验图纸六脚和供电外围（见 HardwareAudit §6）；PCB/实板一致性、供电实测、Flash 实装仍为 NOT_VERIFIED，EP pin 9 不连接的适用性待核实。取得原理图不等于硬件批准前提自动满足，需审核剩余证据后再决定是否允许 Q1.1。

### 2.2 Q1.1 的 BSP READY 判据（必须全部满足，待批准）

- 本次调用上下文合法，硬件接线/供电前置证据已经审核批准，本模块操作权有效。
- 硬件准备结果及本次 MSP 独立检查均为成功；实际时钟源、频率与分频、六脚配置及 QUADSPI Bank/FlashSize 等关键配置读回符合批准值。
- HAL_QSPI_Init 返回 HAL_OK，HAL 状态/错误及控制器空闲检查通过；HAL READY 不替代 BSP READY。
- 有界的只读 JEDEC ID 事务成功，实际 ID 符合预期 EF 40 18；本阶段批准的其他只读检查也无失败。该 ID 不证明完整订货后缀或 Quad 数据线已验证。
- 全过程无未处理故障或超时，未残留资源清理失败；在提交 BSP READY 前任何检查不满足都返回失败，不允许先置 READY 再补做检查。Q1.1 全程禁止擦写、状态寄存器写入、解保护及设备复位。

后续 Q1.1 验证须注入“准备失败但 HAL mock 返回 HAL_OK”“MSP 未执行/失败但 HAL_OK”“配置读回失配”“JEDEC 失败”和“清理失败”，逐项证明 READY 不会提交；这些测试目前均未执行。正常、重复 Init 与 Recover 后重新初始化也必须分别覆盖。

## 3. BSP 契约草案（待批准）

候选 Init/DeInit/GetId/GetDiagnostics/Read/Program/EraseSector/Recover 延续前轮接口，新增独立的寄存器快照和 Quad 读取测试能力；详细声明 Q1.1 审核时冻结。

- 所有同步操作拒绝 ISR、PRIMASK、FAULTMASK 或屏蔽 TIM6 的 BASEPRI 条件，检查 TIM6/NVIC/Tick 可用，参考 bsp_sdmmc.c:651；入场检查不能保证调用中无人暂停 Tick，测试必须覆盖该系统约束。
- Init 成功前保持 NOT_READY，重复成功初始化返回 SYS_ERR_ALREADY_EXISTS；参数错误不接触总线。所有 API 的操作占用需短临界区原子取得，诊断也要避免撕裂。
- Read/Program 建议拒绝 NULL、size=0；按 `address < capacity && size <= capacity-address` 防回绕。Read 明确普通/Quad 路径；Program 按页拆分且共享总 deadline。EraseSector 只接受 4 KiB 对齐的单扇区。
- Program 对全范围先检查旧值，建议 Q1 要求目标全 FF，拒绝未擦除覆盖；不能只检查前一页就写前页，直到后页才发现参数/授权不满足。设备故障仍可能导致部分写入，必须报告已确认字节数。
- BSP 只接受已配置授权窗口内擦写；默认窗口为空，生产构建默认禁写。地址白名单配置不得被普通调用者任意打开；Q1.1 不提供有效写入授权。具体审批令牌/测试 manifest 与构建开关在 Q1.3 前审核。
- 错误复用 SYS_ERR_INVALID_ARG/INVALID_STATE/BUSY/TIMEOUT/IO/PROTOCOL/NOT_SUPPORTED；映射时保存原始 HAL status/error、阶段、地址、耗时、SR、实际频率、原始结果及 cleanup_result，清理错误不能覆盖首错。

## 4. 超时与故障恢复策略（建议）

HAL 等待由 HAL_GetTick 驱动（qspi.c:2483）；Command :784、Transmit :973、Receive :1056 各有起始时间，不能每次传入完整超时来伪造整体有界。一次逻辑事务采用统一起点和剩余预算，HAL_MAX_DELAY 禁用；Init 内部等待用到句柄 Timeout（:367），Abort 同样使用句柄 Timeout（:2159），也必须纳入有界清理预算。

补充事实：HAL_QSPI_Init 在 RESET 路径调用 MSP 后，会在 qspi.c:358–359 将 Timeout 重设为 HAL_QSPI_TIMEOUT_DEFAULT_VALUE；该值在 `Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_qspi.h:444` 为 5000U（5 s）。不能声称 Init 前或 MSP 内设置更小的 Timeout 就能限制首次 Init。Q1.1 必须将此默认等待及 Tick 粒度/退出开销计入初始化最坏时限，返回后检查总耗时；返回后检查不能追溯中止已经发生的等待。若批准的总预算小于此默认等待上限，当前方案不能宣称满足预算，需重新审核调用方案，不能擅改 HAL；初始化后的其他事务及清理仍按剩余预算设置 Timeout。

Q1.3 事务为：确认非 BUSY/非挂起且保护状态允许→06h→读回 WEL→02h 按页或 20h 单扇区→轮询 SR1 BUSY→读回比较。读取 SR2/SR3 的挂起、保护相关位；如 WPS 选择单块锁，必须正确读取对应锁状态或拒绝写入，不自动解锁。轮询 SPI 传输完成不等于 Flash 内部完成。

各操作最长等待根据适用 Winbond 手册最大值与裕量冻结（原厂 Rev F §9.6；最新版复核待办），目前不编造最终超时数值。独立的总超时与每次 HAL 事务超时均需故障注入证明。CPU 同步轮询可能占用当前任务较长时间，Q1 量测耗时/调度影响；任务级等待设计留至 Q2，不能擅自改同步配置。

任何可能已发送写命令的超时都视为结果未知，锁存 FAULT；Abort 只停止 MCU 外设事务，不宣称终止芯片内部擦写。清理后确认控制器空闲、能安全读取状态、Flash 不再 BUSY/SUS，再由显式 Recover 决定重新识别；仍忙或无法读状态则保持故障和禁止写入，不无限重试。66h/99h 不作通用恢复，擦写/挂起状态不执行；必要断电流程需评估数据丢失并单独授权。依据原厂手册 §8.2.37 及 HAL Abort 实现。

## 5. 不修改生产 Application 的测试入口

建议在 Tests/QspiFlashQ1 下建立独立 test_main.c 和独立 Keil 测试工程。使用基线启动文件、SystemInit/HAL Tick、必要双核启动配合、既有 RTOS port，测试任务采用静态 TCB/stack，开始时仅调用 Init/GetId/状态读取，结果写入可由调试器读取的静态报告。测试 main 是测试目录新文件，不替换生产 main，不从生产 Application 注册隐藏测试任务。

这条路径在 Q0 仅是方案，尚未创建或证明可启动；Q1.1 必须先验证系统时钟、双核握手、TIM6 与调度器启动。单模块入口不能证明全系统共存；后续隔离集成测试 target 再包含现有 SDRAM/SDMMC/DCMI/USB 组件和最小测试调度，禁止借用未合并 ApplicationV3。Ethernet 要在明确整合基线后另审。测试不调用 Chip Erase，不调用自动解锁、状态写入或恢复出厂命令。

## 6. 分步批准点

Q1.1：硬件接线证据与最小四个生产文件范围批准后，只读初始化及 JEDEC。
Q1.2：寄存器、普通读与 6Bh Quad 读对照，确认四线证据。
Q1.3：独立明确授权窗口、备份和恢复方案后，再实现擦写及读回。
Q1.4：超时、故障锁存、清理失败和恢复注入。
Q1.5：CM7/CM4 构建、范围核验、实板共存与验收。

上述顺序是设计建议，不是本轮执行授权；任何阶段缺证据均保持 NOT_VERIFIED。
