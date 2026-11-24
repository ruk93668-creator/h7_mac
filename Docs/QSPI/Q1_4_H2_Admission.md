# Q1.4-H2 非破坏性 Recover 准入

基线为 `a897daaca069e1bccad6f1b91aabf09b35a4eb65`。H2.0–H2.3 仅交付软件准备和候选镜像。实板执行为 **NOT_RUN**，下载、故障注入、显式 Recover 分别等待授权。

## H2.0 基线审计

|核对项|R2 行为与 H2 验证|
|---|---|
|普通入口和 Recover 准入|Init 和 Read 不能清除锁存。Recover 校验线程上下文、操作权和写风险；已 READY 时返回 SYS_ERR_ALREADY_EXISTS，未初始化且无锁存时返回 SYS_ERR_INVALID_STATE。H2 在 FAULT 后核对两个普通入口拒绝，二次授权后才测试实际恢复。见 [BSP](../../CM7/BSP/bsp_qspi.c#L479)。|
|读取首错|ReadFinish 先记录原始错误，清 READY、置 FAULT，再进入清理。纯读取故障的快照须为 READ、RECEIVE、SYS_ERR_IO、0x0B，WREN、修改及完成未知均为零。注入读取的已确认长度为零，目标缓冲仍保留哨兵字节。见 [BSP](../../CM7/BSP/bsp_qspi.c#L743)。|
|清理与资源|Abort 或 DeInit 未证明控制器停止时即返回。引脚和 mux 读回、门关闭均完成后才释放所有权。H2 对照 MCU 实际寄存器和所有权；清理失败终止并保留现场。见 [BSP](../../CM7/BSP/bsp_qspi.c#L272)。|
|恢复握手|Qspi_InitLocked 重新配置控制器并取得 EF4018，随后依次读取 SR1/SR2/SR3。BUSY、SUS、WEL 为零且在时限内才发布 READY。快照保留原故障来源和结果。H2 核对 9F/05/35/15 的真实 HAL 成功接收计数各增加一次。见 [初始化](../../CM7/BSP/bsp_qspi.c#L396)及 [Recover](../../CM7/BSP/bsp_qspi.c#L504)。|
|时基|生产 Recover 使用 100,000 次固定探测循环观察下一次 HAL Tick。板上循环耗时依赖时钟、优化和中断相位，Mock 的 Tick 前进不能证明实板必通过。H2 记录进入 Recover 前的 HAL Tick/TIM6 CNT、返回快照、TIM6 CR1/DIER/SR、NVIC 启用及优先级和 CPU 屏蔽状态。保留超时和当时阶段，禁止重试或将断点导致的结果当作正常时序。见 [Tick 探测](../../CM7/BSP/bsp_qspi.c#L493)。|
|镜像来源|从锁定提交归档提取 BSP、ST HAL、CM4 启动入口、双核 Scatter 配方和时钟。H2 私有翻译单元以宏重定向三个调用，文件本身与基线相同。测试专用只读访问器记录 BSP 所有权。正式工程和 ST HAL 未修改。见 [R2 报告](Q1_4_R2_WrenWel.md)及 [H2 翻译单元](../../Tests/QspiFlashQ1/h2/bsp_qspi_h2.c)。|

## H2.1 测试契约

两核启动和 HSEM 握手沿用独立测试配方。CM7 建立一个 768 字静态测试任务，CM4 完成握手后停驻。下载配对镜像会暂时替换 MCU 上的 CM7 和 CM4 软件。生产 CM4 Application 在配对测试期间不会同时运行。详见 [下载范围](Q1_4_H2_DownloadScope.md)。

|输入与检查|预期结果|
|---|---|
|上电、复位或尚未授权|RAM 门闩默认 0，等待 FAULT 授权。无 QSPI 外设访问。|
|第一阶段获另行授权后向 `g_qspi_h2_arm` 写 `0x48324631`|清空门闩，Init → GetId → ReadStatus → 0x0B 短读和两扇区基线扫描。ID 必须为 EF4018，配置读回 120 MHz、READY=1 和 BUSY/SUS/WEL=0。失败停机。|
|单次软件注入|仅在专用注入阶段匹配 0x0B、地址 0x00FFE000、长度 16、同一 HAL 句柄。真实 HAL 必须返回 HAL_OK、ErrorCode=0、READY、计数完整，适配器才把这次返回改为 HAL_ERROR。保留实际和注入结果，不改数据、GPIO、时钟或器件状态。|
|FAULT 及待 Recover|清理阶段必须为 COMPLETE，真实 HAL DeInit 与 MCU CR/SR、RCC、BSP 所有权和六脚复用读回一致。若任何检查失败，保留报告并停止。Init 和 Read 在锁存期间各检查一次，禁止用 Init 恢复。|
|第二阶段获另行授权后写 `0x48325232`|仅在 `WAIT_RECOVER` 消费一次，调用显式 Recover，一次失败即停止。成功时记录 READY、FAULT=0、保留首错、恢复状态/寄存器及命令计数。|
|恢复后验读|两扇区分别走 0x0B 与 0x6B，两路均逐字节与恢复前 0x0B 比较，并分别记录 CRC32。随后验证一次已 READY 的 Recover 返回 ALREADY_EXISTS 且无 SPI 指令。|
|完成|报告 complete=1，任务仅延时，不再发送 Flash 指令。重复或跨阶段门闩不排队，每次复位每种有效门闩最多消费一次。|

允许送到真实 HAL 的操作码只有 9F、05、35、15、0B、6B。适配器保留命令及完整接收计数。GetId 返回缓存 ID，Recover 的 9F 证据来自计数与生产诊断。测试不执行 WREN、Program、Erase、状态寄存器写入、解保护或 Flash Reset，也不在实板制造 BUSY、断电、GPIO 或时钟故障。

## 风险与场地核验

|未验证项|准入条件与停止条件|
|---|---|
|真实 TIM6 和 Recover 路径|允许独立读出运行后的快照。观察 Tick 有无前进，记录实际 JEDEC 和状态事务。探测超时或其他恢复失败保留快照和 FAULT，结束本次执行。|
|板卡、电压和 120 MHz|确认同一板卡及 U5 供电符合高速既有批准条件。120 MHz 为控制器配置读回，实际 CLK 波形、时序裕量和长期稳定性待实板证据。|
|器件与阵列数据|外部偏移 `[0x00FFE000,0x01000000)` 仅供读取。H1 两份本地 4 KiB 备份可按保存的地址、长度和 SHA-256 离线验证。需由现场证据确认是同一板卡；仅字节相等不能识别板卡。若均为 FF，结论限于这两个范围的恢复前后数据一致，非均匀 Quad 的历史证据仍来自 H1。|
|下载和回退|两核实际固件、Bank Swap、启动地址、选项字节、内部 Flash 下载算法及各核回退镜像由实板人员人工核对。镜像只构建和审计，尚未授予下载权限。|

## H2.2 软件证据

H2 Mock 使用生产 BSP 路径，覆盖两门闩、真实结果失败或短接收时不注入、成功恢复、恢复 JEDEC 不匹配、固定 Tick 超时、DeInit 失败时保留所有权、恢复后 Quad 失配及禁止指令门禁。独立真实 ST HAL 测试在 Unicorn 合成 MMIO 下执行 Command 与 Receive，核对完整接收后才注入、后续读取转回原 HAL 结果、保护状态读保持真实返回。两套软件仿真没有物理 SPI 总线。见 [原始软件证据](../../Tests/QspiFlashQ1/h2/evidence/)。

|验证|结果|
|---|---|
|H2 Mock|8/8，2,661 条断言|
|H2 真实 ST HAL 仿真|28 条断言，全部通过|
|PC 现场报告工具|5/5，含通过样本、Recover 超时、错误 ABI、数据损坏和 Intel HEX 校验|
|Q1.1–Q1.4-R2|51/51、516/516、48/48、45/45、11/11，结果与 R2 逐例一致|
|SFDP、Quad ID、H1|SFDP 6/6，Quad ID 30/120 MHz 各 11/11，H1 固件 19/19，H1 PC 工具 10/10|
|真实 HAL 链接回归和生产写门禁|完整复现 R2 输出|

SDRAM、DCMI 和 USB 的历史 FAIL/ERROR 记录原样保留，本轮未重跑这三组。两个 H1 原始扇区档案均通过哈希检查，与 H1 擦后独立复读逐字节相同且为全 FF。见 [回归统计](../../Tests/QspiFlashQ1/h2/evidence/regression.json)和 [H1 参考证据](../../Tests/QspiFlashQ1/h2/evidence/h1_reference.json)。

现场在完整停止时导出 `g_qspi_h2_report`，成功时另导出 `g_qspi_h2_before` 与 `g_qspi_h2_after`。PC 工具读取与 CM7 AXF 匹配的下载范围清单，并校验完整 BIN 或 Keil Intel HEX RAM 导出。失败报告可只提供 report，工具输出快照及失败状态。命令示例和地址见 [下载范围](Q1_4_H2_DownloadScope.md)。

H2 软件准备完成后只提交独立分支，等待 GPT 审核。Q1.5 冻结。
