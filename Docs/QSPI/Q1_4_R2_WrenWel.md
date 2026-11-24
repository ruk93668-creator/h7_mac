# Q1.4-R2 跨页 WREN/WEL 故障锁存

审核输入为 `b75731da2c5008396dcde7167777b4d759017777`。生产改动仅在 `Qspi_WriteEnable()` 的有效 WEL=0 分支。有修改命令尝试历史时设置本次事务的故障标志，由现有 `Qspi_WriteFinish()` 锁存 FAULT 并清理 MCU 侧资源。返回值仍为 `SYS_ERR_PROTOCOL`。已验读前缀由原路径保留，完成未知标志继续由 `pending` 决定。公开接口、诊断 ABI 和 Recover 实现沿用原代码。见 [生产差异位置](../../CM7/BSP/bsp_qspi.c#L1227)。

|场景|R1|R2|
|---|---|---|
|当前调用首次 WREN 成功，有效 SR1 的 BUSY=0、WEL=0|READY，`SYS_ERR_PROTOCOL`|相同，未发修改命令，后续只读成功|
|同一跨页调用的第二次 WREN 成功，有效 SR1 的 BUSY=0、WEL=0|READY，R1-B01 FAIL|FAULT，R1-B01 PASS。两次 WREN、一次 0x02，`completed=1`，`flash_completion_unknown=0`|
|跨页 FAULT 后调用 Recover|R1-B01 在状态断言处失败|返回 `SYS_ERR_NOT_SUPPORTED`，指令计数无增加，保留首错和已确认前缀|
|独立上一笔写入成功，下一笔首次 WREN 有效读回 WEL=0|未专项覆盖|R2-01 PASS。当前请求 `completed=0`、`mutation_possible=0`，保持 READY，前次已确认范围仍可查询|

ARMCC5 与 Unicorn 专项 **11/11 通过，1,349 条断言**，其中 R1-B01 执行 186 条断言，R2-01 执行 215 条断言。R1-B01 的原断言保留，修复后增加对块边界、数据和 Recover 的检查。运行 `python Tests/QspiFlashQ1/run_q14_r1_tests.py --r2` 可复现。见 [R2 专项原始输出](../../Tests/QspiFlashQ1/q14_r2/evidence/run.log)及 [R1 原始 FAIL](../../Tests/QspiFlashQ1/q14_r1/evidence/boundary/run.log)。

Q1.1、Q1.2、Q1.3、Q1.4、SFDP、两个 Quad ID 频率和三个 H1 固件测试配置已重跑，逐例结果与 R1 一致。H1 PC 工具 10/10，真实 ST HAL 链接仿真和默认写门禁通过。历史 SDRAM、DCMI 和 USB 的既有 FAIL/ERROR 沿用 Q1.4 归档，本轮未重跑。见 [回归统计](../../Tests/QspiFlashQ1/q14_r2/evidence/legacy_regression.json)。

使用 `python Tests/QspiFlashQ1/run_q14_build.py current --r2` 在隔离的 R1 工程快照中应用本地 BSP 修订，执行 CM7 和 CM4 ARMCC5 全量 Rebuild。所用规范化 Keil 工程 SHA-256 与 R1 一致。

|目标|错误 / 警告|ROM|RAM|最大静态栈|
|---|---:|---:|---:|---:|
|CM7|0 / 0|149,644 B|114,856 B|1,344 B|
|CM4|0 / 0|28,676 B|4,512 B|432 B|

两个目标的 ROM、RAM 和最大静态栈增量均为 0 B。默认镜像的 Recover 入口继续被链接器剔除，独立的 HAL 链接仿真包含 Recover。见 [构建统计](../../Tests/QspiFlashQ1/q14_r2/evidence/current_build.json)。

本轮实板测试为 NOT_RUN。Q1.4 整体验收留待 GPT 独立审核，Q1.5 仍冻结。
