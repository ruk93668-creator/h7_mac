# Q1.4-R1 WREN/WEL 契约与回归报告

审核输入为 `aa2248cd24d915dfb0054a33032067a62d28f956`。R1 指定专项通过，跨页严格契约复现仍为 **FAIL，待 GPT 审核**。矩阵和恢复准入见 [Q1.4 故障隔离与显式恢复](Q1_4_FaultRecovery.md)，待审边界见 [跨页技术说明](Q1_4_R1_MultiPageDecision.md)。

## WREN 专项

|编号|覆盖|结果|
|---|---|---|
|R1-01|Program 的 WREN 成功且有效 SR1 证明 BUSY=0、WEL=0；未尝试修改命令，保留 WREN 历史，无清理及 Recover，后续合法读成功|PASS|
|R1-02|Sector Erase 完整校验 4 KiB 备份，WREN 成功但 WEL=0，未发送 0x20，目标内容逐字节一致，保持 READY，后续合法读成功|PASS|
|R1-03|WREN 后 SR1 接收 HAL_ERROR、HAL_TIMEOUT、短接收、HAL_OK 带错误；均锁存 FAULT，禁止继续 Program/Erase，Recover 拒绝且不发送指令。超时用例另注入 Abort 失败，首错与清理结果分别保留|4/4|
|R1-04|WREN 命令自身返回 HAL_BUSY、HAL_ERROR、HAL_TIMEOUT；均锁存 FAULT，Recover 拒绝|3/3|
|R1-B01|跨页 Program 前缀已验读，随后有效 WEL=0；断言要求按整次调用锁存 FAULT，实际仍 READY|FAIL，175 条断言中第 280 行断言失败|

R1-01/02 核对写事务返回的诊断和 Mock 指令计数，并通过 `BSP_QSPI_GetFaultDiagnostics()` 确认 `wren_possible=1`、`fault_latched=0`。专项通过真实 BSP 路径注入 HAL 和模拟寄存器故障。指定组和追加 WREN 错误用例合计 **9/9、948 条断言**。运行 `python Tests/QspiFlashQ1/run_q14_r1_tests.py` 可重现专项，附加 `--boundary` 独立重现并保留非零退出码。

## 原有回归与链接

|套件|通过|断言|
|---|---:|---:|
|Q1.1|51/51|989|
|Q1.2|516/516|285,723|
|Q1.3|48/48|25,971|
|Q1.4|45/45|12,757|
|SFDP|6/6|600|
|Quad ID 30 / 120 MHz|11/11 各频率|1,466 各频率|
|H1 备份 / 120 MHz 备份 / 写入|2/2、4/4、13/13|1,152、1,643、11,015|
|H1 PC 工具，含既有实际 AXF 证据|10/10|unittest|

真实 ST HAL 链接仿真通过，包含 5000 ms 初始化超时、控制器持续 BUSY 时拒绝 Recover，以及默认写门禁拒绝 Program/Erase（0 次 Tick，未映射外设）。历史 SDRAM、DCMI、USB 的 Q1.4 原始 FAIL 和 ERROR 证据保留并沿用，本轮未重跑这三组。各次测试原始输出、逐例统计及编译命令见 [证据目录](../../Tests/QspiFlashQ1/q14_r1/evidence/)。

## 构建、资源与提交范围

使用 `python Tests/QspiFlashQ1/run_q14_r1_build.py` 在隔离目录从审核输入导出工程，以 ARMCC5 完整 Rebuild。对比 [Q1.4 审核基线构建](../../Tests/QspiFlashQ1/q14/evidence/build/current.json)，ROM、RAM、最大静态栈变化均为 0 B。

|目标|错误 / 警告|ROM|RAM|最大静态栈|
|---|---:|---:|---:|---:|
|CM7|0 / 0|149,644 B|114,856 B|1,344 B|
|CM4|0 / 0|28,676 B|4,512 B|432 B|

生产 BSP 在审核输入与本地逐字节相同，Git 差异为空。SHA-256 如下。

|文件|SHA-256|
|---|---|
|bsp_qspi.c|`50f80330758e12853831bb98f66e87364f3c2ef8f80da268dcd93630c3965f7d`|
|bsp_qspi.h|`ff49f205443a12dc604cf04b9da1588d8e68231de48cc887a39da1da731aac76`|

提交仅含故障矩阵、R1 报告与跨页说明、专项测试、两个运行脚本和原始软件证据。本地 Keil 配置、AXF、HEX 及现有 H1 实板归档继续保留。本轮实板测试为 NOT_RUN。
