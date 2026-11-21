# Q1.1 实板只读识别候选准备报告

日期：2026-11-21；已接受的软件基线：`d35c6464a9392d97f04f103bc828f59800d2c751`。
Q0 设计审计与 Q1.1 纯软件 PASS；本轮只准备独立固件和下载范围，实板执行仍待单独批准，未进入 Q1.2。

## 交付范围

所有新增入口、测试 Scatter、构建与产物审计脚本在 `Tests/QspiFlashQ1/hardware/`。生产 main/Application/Platform/Framework/CM4/工程/Scatter/HAL 及已验收 bsp_qspi.c/.h 全部不变。隔离快照中的上述四份 Q1.1 生产文件已逐字与 d35c6464 核对。

CM7 不运行生产 Application 或 Initcall，仅使用基线启动、HAL、TIM6、RTOS port 和静态测试任务。CM4 用测试入口完成 HSEM/STOP 握手后停驻，不运行业务或 IPC。时钟函数从基线原样提取，哈希记录在证据中；测试专用 Scatter 只映射内部 Flash/RAM，不引入外部 QSPI、SDRAM、共享业务段或下载 loader。

uVision 多 target 文件树合并曾导致两核启动文件同时参与链接；现生成两个独立的单 target 工程，消除该问题。原生产链接脚本的无用 IPC/Initcall/SDMMC 段曾产生警告；改为测试专用脚本及去除对应 keep 后已解决，不压制警告。前三次候选日志留在独立 output，最终审核对象是 `output/QspiFlashQ1/hardware_candidate_v4/`。

## 构建及静态产物结果

| 候选 | 编译 | Code/RO/RW/ZI（bytes） | HEX 数据范围（含首尾） | HEX 数据字节 |
| --- | --- | --- | --- | --- |
| CM7 | 0 错误、0 警告 | 14934 / 794 / 112 / 9552 | 0x08000000–0x08003DDF | 15840 |
| CM4 | 0 错误、0 警告 | 944 / 696 / 12 / 1636 | 0x08100000–0x08100673 | 1652 |

逐条检查 Intel HEX 校验和与记录地址，两份数据均仅在 MCU 内部 Flash 范围，分别落在各自首个128KiB内。CM7 基线未启用自动生成 HEX，因此另外以 fromelf --i32combined 从已构建 AXF 导出，未接触探针。原始日志、完整 SHA256、实际候选路径及 ELF 符号地址冻结于本轮 hardware/evidence/。

产物审计确认每个工程只含自身启动文件，无生产 main/Application/Framework/Service 源。CM7 ELF 确实保留 BSP Init/GetId/GetDiagnostics、强 MSP 和真实 HAL Init/Command/Receive/Abort，不是上一轮未引用驱动的正式镜像；无保留的 HAL_QSPI_Transmit/MemoryMapped/Transmit_DMA/Receive_DMA。这仍不证明真实 HAL 总线事务已经执行。

## 只读门闩及证据采集

默认 `g_q1_hw_arm=0`，测试任务 phase=2 等待，尚不调用 QSPI。只有后续获准执行，确认供电、板号、两个候选固件和运行状态后，才允许调试器将此 RAM 变量置为 `0x51315244`。每次复位仅放行一次 BSP Init→GetId→GetDiagnostics；完成后不循环识别、不自动重试或复位。只使用已审9F路径，无写使能、擦写、解保护、Flash软件复位或 Quad 读取。

`g_q1_hw_report` 包含：version/phase/complete、启动与各 API 返回、ID、起止 Tick、BSP 全诊断、HCLK/RCC、GPIOF/G 配置、TIM6 状态/优先级及 PRIMASK/BASEPRI。complete=1 才代表该次快照完整；初始为0的字段不能直接当作成功结果。phase=FFFFFFFF 为启动失败。原始 AXF 中 RAM 符号地址见 artifact_audit.json，可在获准后用于观察。

未来通过要求：实际 EF4018、Init/GetId/Diagnostics 均成功、diagnostics.ready=1、fault=0、MSP本轮成功、Rx计数3、实际时钟/引脚配置一致，并保留供电/波形与板卡身份记录。失败时先保存诊断，不自动追加“恢复”命令。TIM6必须在 HAL等待期间持续推进，不能用调试器暂停造成的墙钟延时证明或否定软件预算；启动时钟函数原有等待也不在已审 BSP 事务时限保证内。

## 拟申请的内部下载范围（尚未授权）

| 镜像 | 拟用内部扇区范围（半开区间） |
| --- | --- |
| CM7 | [0x08000000, 0x08020000) |
| CM4 | [0x08100000, 0x08120000) |

范围是按当前小镜像和标准128KiB扇区提出的审核方案，必须在实板上先核实实际器件、bank/选项字节映射及内部 Flash 算法。HEX 数据地址检查不能约束下载工具扩大擦除范围；后续不得全片擦除、使用外部 QSPI loader、自动执行外部初始化或改写选项字节。

生成工程已关闭 UpdateFlashBeforeDebugging/LoadApplicationAtStartup/RunToMain 的已有选项；本轮实际只调用 UV4 rebuild 和 fromelf，没有发出下载命令。后续审批还需独立核查探针/调试设置、镜像哈希、准确扇区与双核匹配情况。

测试会临时替换对应内部 Flash 中原有 CM7/CM4 固件，必须先明确原固件备份与恢复版本；“不改生产 Application 源码”不等于板上原固件不受内部下载影响。W25Q128内容不需要提前分配擦写扇区，始终禁止擦写。

## 尚未验证

板卡/PCB版本对应、实装料号/焊接、供电/CS及通断、双核启动和测试调度、TIM6实板运行、真实 HAL FIFO/Command/Receive、EF4018、失败安全退出和信号完整性全部仍为 NOT_VERIFIED。当前交付只是可审核的候选，等待技术审查及单独实板执行授权，不自动运行、不进入 Q1.2。
