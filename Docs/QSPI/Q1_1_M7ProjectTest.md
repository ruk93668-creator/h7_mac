# 原工程中的 M7 QSPI 只读测试

日期：2026-11-22。用户指定唯一工程 `D:\32\STM32H7\h7_mac_qflash\MDK-ARM\h7_mac.uvprojx`，QSPI 只在 M7 使用，烧录与调试由用户手动执行。

本次在该工程 M7 的 BSP 分组仅增加 `Tests/QspiFlashQ1/hardware/m7_project_test.c`。源文件受 CORE_CM7 条件保护，通过已有 APPLICATION Initcall 只创建一个静态测试任务；调度启动后等待 RAM 门闩。不替换 main，不改既有 BSP、生产 Application、Platform、Framework 或 CM4 源码。此前独立双核候选不再作为本次操作入口，不需要改刷其 CM4 停驻固件。

实际工作区已有用户修改的 uvprojx/uvoptx；编辑前已备份到 output/QspiFlashQ1/m7_project，保留原有设置。Git 提交只包含本次 M7 文件条目，不夹带已有工程/调试配置改动。构建使用用户当前工作区的原工程，仅运行 UV4 rebuild，没有连接探针、下载或操作实板。M4 target 对编辑前用户快照保持不变；构建 M4 仅验证其未受影响，不代表需要下载 M4。

## 手动步骤

1. 重新加载上述唯一工程，选择 **h7_mac_CM7**，Rebuild。M4 保留现有兼容的正常固件，不下载之前独立候选的 CM4 文件。
2. 采用用户现有 CMSIS-DAP/SWD 调试配置，仅下载 **M7 内部 Flash**，使用按扇区擦除，不用全片擦除、外部 QSPI loader 或选项字节写入。当前是完整生产应用加测试任务，镜像大小/范围与旧独立小候选不同，不能沿用旧候选的128KiB单扇区假定。
3. 进入 M7 调试，观察 `g_q1_hw_arm`、`g_q1_hw_report`，复位并 F5。任务正常启动后 phase=2、complete=0，尚未调用 QSPI。
4. 保持 CPU 运行，将 `g_q1_hw_arm` 写为 `0x51315244`。它立即清零，只执行一次 Init→GetId→GetDiagnostics，完成后不重试。
5. complete=1、phase=4 后检查三个 API 返回0、id=0xEF4018、diagnostics.ready=1、fault=0、received_bytes=3。先保存失败快照，不追加擦除、解保护或复位 Flash 命令。

若未到 phase=2，先检查原工程现有 `init_error` 和启动/Initcall/任务调度，不把尚未运行测试解释为 QSPI 失败。不得在 HAL 等待过程中暂停 CPU 或屏蔽 TIM6。使用当前 AXF 的变量符号，不沿用旧独立候选的 RAM 地址。

该方案保留完整应用及 M4 原有运行逻辑；其他业务的既有行为不由本测试任务停用，不能把它描述为硬件隔离测试。测试模块本身只使用已验收 BSP 的9F单线识别，不提供写使能、擦写、解保护、Flash复位、SR1/2/3 或 Quad 读取。

## 既有自动尝试的状态

用户转为手动调试前，探针识别到双核、2MiB内部Flash、RDP Level0、bank未交换。内部Flash原固件双读备份一致。首次自动下载在 CM7 loader 进入 HardFault 后停止，未放行 JEDEC 测试；随后原内部固件已恢复，并全2MiB逐字核对一致，SHA256 为 `3d5783fedbb46898a72f025adb5aedb955f73e267ebf329ee07b1dbb693f3b21`。原备份仅在本地 output，不提交原始固件。后续自动烧录停止。

实板 JEDEC 仍为 NOT_VERIFIED，等待用户按当前原工程 M7 入口操作并提供报告。本次接入与编译结果见 `Tests/QspiFlashQ1/hardware/evidence/m7_project/`。
