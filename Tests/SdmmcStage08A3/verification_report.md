# 阶段08A-3：SDMMC1软件验收

## 结论与基线

分支`codex/stage08a3-sdmmc1`，基线`main@cbc4c415b58619dce9f2c95dd5dc3f3a258fd405`。
本轮只实现SDMMC1低速1-bit识卡、信息与错误诊断。软件通过，真实插拔电平与识卡仍待实板；没有卡文件系统、StorageTask、DMA或扇区读写。
默认关闭测试，不自动识卡；不合并main。

## 修改文件

| 文件 | 作用 |
|---|---|
| CM7/BSP/bsp_sdmmc.c/.h | 私有HAL句柄；检测、识卡、卡信息、诊断、DeInit、重入保护和失败清理 |
| CM7/BSP/bsp_sdmmc_hal_config.h | CM7预包含启用HAL SD；将软件等待默认值从0xFFFFFFFF改为1000ms |
| Tests/SdmmcStage08A3/sdmmc_probe.c/.h | 默认关闭、一次冷启动测试；失败仅记录，不中止Framework |
| MDK-ARM/h7_mac.uvprojx | CM7加入BSP、探针、hal_sd.c、ll_sdmmc.c及预包含；CM4显式排除这些文件 |
| Tests/SdmmcStage08A3/ | 专项、原回归归档、构建日志、范围审计及报告 |
| Docs/Design/SDMMC1_Stage08A3.md | 接口契约、Watch和实板操作步骤 |

HAL SD扩展源码未使用，无需为本轮添加hal_sd_ex.c。未修改任何ST HAL/LL文件、既有CM7/CM4生产源码、HAL Tick、Camera、USB、RTOS任务、IPC、OLED、Scatter或MPU。
Keil多Target会共享文件列表，CM4新增的File条目均明确IncludeInBuild=0；这属于构建隔离，不是向CM4移植SD。
首次构建暴露探针误归入继承的CM4 Application组，已移到CM7 Application并补齐CM4显式排除，最终两核重新全量编译，不以首轮结果冒充通过。

## 硬件与时钟依据

- 原始V0图纸：SVG_Schematic1_2026-10-25.zip内第2页主控、第6页数据存储。SD_DETECT连接PH14，卡座开关有R49 10k上拉。
- 后续卡座图纸确认SW插卡接通VDD：默认改为高有效，PH14内部下拉，需用户拆除R49上拉后验证无卡0、插卡1。支持宏BSP_SDMMC_DETECT_ACTIVE_LOW=0或1。
- 1-bit只配PC8 DAT0、PC12 CLK、PD2 CMD的AF12推挽；PC9～PC11不驱动。CMD/DAT使用外部上拉；PH14拆除R49上拉后使用GPIO_PULLDOWN。
- PLL1Q沿用240MHz，RCC_SDMMCCLKSOURCE_PLL只选现有PLL1Q。核对PLL1 ready、实际频率与MUX回读，不写PLL1/PLL2参数。
- SDMMC1/2共用时钟选择器：SDMMC2已启用且使用另一时钟时返回BUSY，不擅自切换。
- ClockDiv=300，SDCLK=240000000/(2×300)=400000Hz。HAL初始化后继续回读验证400kHz、1-bit、MASK=0、IDMACTRL=0。
- MSP只复位SDMMC1自身，清除旧DPSM/IDMA；不复位DMA1或其他外设。DeInit不关闭共用GPIOC/D/H端口时钟，不关闭PLL1Q。

HAL_SD_Init内部会读取SCR/SD Status等协议小数据，这是PIO识卡协议，不是应用扇区读写；本BSP没有调用ReadBlocks/WriteBlocks或DMA接口。

## 接口与调用链

```text
BSP_SDMMC_Init
  → 检查IPSR、PRIMASK、TIM6 Tick及BASEPRI屏蔽关系
  → 本核busy保护，长操作期间中断保持开启
  → 读取PH14并判定有卡
  → 选择/校验现有PLL1Q
  → 私有句柄配置低速1-bit
  → HAL_SD_Init
      → HAL_SD_MspInit（本BSP）
      → HAL_SD_InitCard（HAL自动低速1-bit）
      → HAL_SD_GetCardStatus / ConfigWideBusOperation(1-bit)
      → HAL内部检查TRANSFER
  → BSP回读CLKCR、MASK、IDMACTRL
  → HAL_SD_GetCardInfo并检查逻辑容量
  → HAL_SD_GetCardState再确认TRANSFER
  → 提交有效卡信息
```

GetCardInfo只有初始化成功且当前检测仍有卡时交付完整副本；失败不改输出。容量先转uint64再乘LogBlockNbr×LogBlockSize，类型0=SDSC，1=HAL合并的SDHC/SDXC，不能声称已精确区分HC与XC。
GetDiagnostics不重新发送卡命令，初始化并发时返回BUSY，避免半更新副本。原始HAL错误/状态、STA/CLKCR/RESP1在清理前锁存，RCC失败另存rcc_status。
DeInit先撤销卡信息有效性；清理失败保留硬件占用，禁止新Init，允许显式重试DeInit。

| 条件 | SysResult |
|---|---|
| 空参数 | INVALID_ARG(-1) |
| 按检测极性无卡 | NOT_FOUND(-2) |
| 已成功初始化 | ALREADY_EXISTS(-3) |
| ISR/Tick不可运行/PLL不符合/卡非TRANSFER | INVALID_STATE(-4) |
| 同时操作或SDMMC2异源占用 | BUSY(-5) |
| HAL超时或HAL_ERROR带超时位 | TIMEOUT(-8) |
| 普通HAL错误/CRC | IO(-9) |
| 非法卡信息或HAL实际配置不符 | PROTOCOL(-10) |

## 不能忽略的时间约束

本工程HAL Tick来自TIM6，优先级4；新BSP不改TIM6或FreeRTOS。只拒绝真正屏蔽Tick的BASEPRI，任务创建后BASEPRI=80且TIM6优先级4仍可运行。
SDK的SDMMC_SWDATATIMEOUT原值0xFFFFFFFF，通过CM7预包含改为1000ms，HAL/LL源码不改。它是若干内部等待的单次超时，**不是整个HAL_SD_Init的总耗时上限**；命令超时、供电协商等还遵循原HAL边界。
默认测试关闭没有同步识卡等待。开启完整探针会延长调度器启动前时间，可能短暂触发CM4 IPC启动等待超时；本轮不改心跳阈值，不伪造Ready，正常固件关闭测试不受此等待影响。
HAL内部某些失败只返回HAL_ERROR且没有完整保留底层错误位，此时需结合stage、RCC/寄存器快照及HAL断点定位，不能伪造具体故障原因。

## 专项实际执行

直接以ARMCC5编译实际BSP、实际CMSIS类型/寄存器地址、实际测试入口；只Mock HAL函数、时间和引脚输入。Unicorn运行真实ARM代码，非复制一份BSP逻辑。
50场景×4组合（关闭/仅检测/完整探针/反向检测极性）×O0/O2=400次，全部PASS。4种组合都执行同一份完整场景集合，非400个不同测试编号。
Mock不验证实际SD命令波形、TIM6硬件递增、真实卡响应、总线电气、DMA或NVIC硬件时序；IPSR/BASEPRI/MMIO是仿真控制输入。

| 编号 | 场景 | 8轮结果 |
|---|---|---|
| 0 | 成功识卡及32GiB容量 | 8/8 PASS |
| 1 | 无卡不调用HAL | 8/8 PASS |
| 2 | PLL1Q不符 | 8/8 PASS |
| 3 | RCC失败 | 8/8 PASS |
| 4 | RCC超时 | 8/8 PASS |
| 5 | 内核频率无效 | 8/8 PASS |
| 6 | MUX回读失败 | 8/8 PASS |
| 7 | HAL初始化CRC错误 | 8/8 PASS |
| 8 | HAL_ERROR包装命令超时 | 8/8 PASS |
| 9 | HAL_TIMEOUT | 8/8 PASS |
| 10 | HAL_BUSY | 8/8 PASS |
| 11 | 卡信息读取失败 | 8/8 PASS |
| 12 | 卡信息超时 | 8/8 PASS |
| 13 | 零块数 | 8/8 PASS |
| 14 | 非法逻辑块大小 | 8/8 PASS |
| 15 | 未知卡类型 | 8/8 PASS |
| 16 | 非TRANSFER状态 | 8/8 PASS |
| 17 | 状态超时 | 8/8 PASS |
| 18 | 状态CRC错误 | 8/8 PASS |
| 19 | 实际分频为零 | 8/8 PASS |
| 20 | 实际4bit | 8/8 PASS |
| 21 | 意外IDMA使能 | 8/8 PASS |
| 22 | 意外中断使能 | 8/8 PASS |
| 23 | 清理失败与重试 | 8/8 PASS |
| 24 | 初始化内部重入 | 8/8 PASS |
| 25 | 重复初始化 | 8/8 PASS |
| 26 | 失败后重新识卡 | 8/8 PASS |
| 27 | DeInit使信息失效 | 8/8 PASS |
| 28 | 空参数 | 8/8 PASS |
| 29 | 初始化前读取 | 8/8 PASS |
| 30 | ISR拒绝 | 8/8 PASS |
| 31 | PRIMASK拒绝 | 8/8 PASS |
| 32 | BASEPRI屏蔽Tick拒绝 | 8/8 PASS |
| 33 | BASEPRI允许TIM6 | 8/8 PASS |
| 34 | TIM6停止 | 8/8 PASS |
| 35 | TIM6中断停用 | 8/8 PASS |
| 36 | NVIC未使能 | 8/8 PASS |
| 37 | 外部HAL句柄忽略 | 8/8 PASS |
| 38 | 拔卡拒绝旧信息 | 8/8 PASS |
| 39 | PH14原始输入 | 8/8 PASS |
| 40 | 关闭探针无副作用 | 8/8 PASS |
| 41 | 检测模式 | 8/8 PASS |
| 42 | 完整探针成功 | 8/8 PASS |
| 43 | 探针HAL失败不阻断 | 8/8 PASS |
| 44 | 探针无卡不阻断 | 8/8 PASS |
| 45 | 探针只执行一次 | 8/8 PASS |
| 46 | SDMMC2异源保护 | 8/8 PASS |
| 47 | SDMMC2同源允许 | 8/8 PASS |
| 48 | Tick回绕 | 8/8 PASS |
| 49 | PLL1未运行拒绝 | 8/8 PASS |

## 原回归与构建

| 组 | 执行/PASS | FAIL |
|---|---:|---:|
| IpcV2_run_tests | 512 | 0 |
| IpcRuntime_run_tests | 260 | 0 |
| IpcRuntime_run_task_tests | 20 | 0 |
| IpcStatus_run_tests | 448 | 0 |
| IpcStatus_run_kernel_tests | 41 | 0 |
| OledAsyncAcceptance_run_tests | 50 | 0 |
| OledMonitor_run_tests | 122 | 0 |
| SDMMC新专项 | 400 | 0 |
| 合计 | 1853 | 0 |

原回归源码和断言未修改，本次结果另存本目录，原报告已还原。未运行全部历史Camera/USB专项，本轮以既有业务源码哈希未变、两核正式构建及上述回归确认修改边界。

命令：

```powershell
python Tests/SdmmcStage08A3/run_tests.py
python Tests/SdmmcStage08A3/run_regressions.py
python Tests/SdmmcStage08A3/run_probe_builds.py
python Tests/SdmmcStage08A3/run_build.py
```

编译器ARM Compiler 5.06 update7 build960，UV4正式工程全量重编。默认CM7/CM4结果见build_results.json；开启检测及完整识卡CM7见probe_build_results.json。两份测试镜像在output/SdmmcStage08A3/firmware/，不提交仓库；最后重编默认配置，常规输出目录保留默认关闭镜像。

## 待实板验收

按Docs/Design/SDMMC1_Stage08A3.md先验证PH14，再完整识卡。必须由用户烧录验证：无卡、检测有效电平、成功读容量/状态、失败后再次启动，以及Camera/USB/IPC/OLED保持正常。
当前没有接实板，没有识别任何真实卡，不宣称硬件通过；也没有做扇区读写、FatFS或速度测试。

构建后的项目尾随空白清理已通过XML规范化语义一致性检查，见project_format_check.json；随后以最终项目执行增量检查，见incremental_build_results.json。

## 最终编译结果

| 配置 | Error | Warning | 结果 |
|---|---:|---:|---|
| 默认 h7_mac_CM7 完整重编 | 0 | 0 | PASS |
| 默认 h7_mac_CM4 完整重编 | 0 | 0 | PASS |
| CM7 detect 完整重编 | 0 | 0 | PASS |
| CM7 probe 完整重编 | 0 | 0 | PASS |

最终项目增量检查两核同为0 Error/0 Warning。默认CM7 Code=97810、RO=2802、RW=1224、ZI=44992B；完整识卡镜像CM7 Code=107514、RO=2802、RW=1228、ZI=45236B。
CM4仍为Code=26302、RO=1438、RW=936、ZI=3576B，与基线相同。CM7启动栈仍为0x1000（4096B），不新增任务栈；真实最坏嵌套余量仍属实板验证。
测试开启镜像的g_sdmmc_test地址为0x200004A0、大小152B，仅适用于对应AXF。默认测试关闭镜像也保留enabled=0的Watch标记。

## 检测脚配置修订（2026-11-08）

- 用户确认将拆除R49外部上拉；PH14改为内部下拉，高电平表示插卡。只修改检测输入，不修改CMD/DAT上拉或SDMMC时序。
- 400/400专项模拟执行通过：默认高有效、反向极性覆盖及GPIO_PULLDOWN配置断言。
- 本轮重编使用用户当前Keil工程设置，保留其检测探针宏及调试修改，不宣称构建的是默认关闭探针固件。构建结果见build_results.json。
- 实板待验收：拆除上拉后，无卡detect_raw=0/card_present=0，插卡detect_raw=1/card_present=1。g_sdmmc_test是启动时快照，实时观察使用(GPIOH->IDR >> 14) & 1，重新运行探针需复位。
- 本轮不重复执行历史IPC/OLED回归；历史记录不作为本轮实板识卡证据。

本轮当前工程全量重编：CM7 0 Error / 0 Warning，CM4 0 Error / 0 Warning；用户Keil项目文件未纳入本次提交。
