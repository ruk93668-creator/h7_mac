# Q1.1 实板只读候选固件（仅准备，未获执行授权）

基线 `d35c6464a9392d97f04f103bc828f59800d2c751` 已通过纯软件审核。本目录仅为后续单独审批准备双核测试入口；不修改已验收 BSP 或任何生产 Application/main/工程配置，不做实板下载。

## 构建

`python Tests/QspiFlashQ1/hardware/build_readonly.py <新的输出名称>`

生成到 `output/QspiFlashQ1/<输出名称>/source/MDK-ARM/` 的 `qspi_readonly_CM7.uvprojx`、`qspi_readonly_CM4.uvprojx` 每个只有一个 target，避免 uVision 多 target 文件树合并引入另一内核启动文件。构建脚本仅调用 UV4 `-r`，没有下载/擦除命令。输出已存在则拒绝覆盖，旧失败日志保留。

快照由固定验收提交归档，加本目录两个 main 生成；原生产文件均来自该验收提交。CM7 SystemClock_Config 函数从验收基线逐字提取，哈希写入 build_results.json；保留原时钟配方，不引用业务初始化。保留基线 startup、SystemInit、HAL、TIM6、FreeRTOS port 和平台 RTOS hooks，不运行 Framework Initcall、Application、IPC、USB、SD 卡、SDRAM 或相机业务。测试使用本目录专用 Scatter，沿用内部 Flash/RAM 地址，去掉未使用的 Initcall/IPC/SDMMC 段和 keep 参数；不修改生产 Scatter，不通过压制警告掩盖无效段。

CM4 测试入口只作 HSEM/STOP 启动握手，随后 WFI 停驻；CM7 创建一个静态测试任务。启动握手、调度器、TIM6、实板电气及真实 HAL 通信尚未验证，编译成功不代表可在板上正常启动。基线时钟初始化的启动等待与已审 BSP 的事务超时属于不同层次，不能宣称整机启动具有同样的有界保证。

## 后续获准执行时的观察点

该节仅说明候选行为，不授予实板操作权限。

- `g_q1_hw_report.version=1`；phase=1 启动、2 等待门闩、3 正在识别、4 完成、FFFFFFFF 启动失败。只有 complete=1 时，读取本轮完整结果。
- `g_q1_hw_arm` 默认0。后续获准下载/运行并核对供电与 phase=2 后，才可由调试器写入 RAM 常数 `0x51315244`，放行一次识别；门闩立刻清零。
- 每次复位最多调用一次 BSP_QSPI_Init、GetId、GetDiagnostics。只通过验收驱动发送一次9F；无擦写、解保护、Flash 复位或 Quad 读取。GetId 返回缓存值。
- 报告保存 API 返回、24-bit ID、BSP 全诊断、起止 HAL Tick、实际 RCC/HCLK 和 GPIO/TIM6 配置；complete 后不自动重试或 DeInit，以便保留成功时控制器配置。失败按已验收 BSP 策略锁存，不提供自动恢复通道。
- 观察窗口应保持 CPU 正常运行和 TIM6 可推进，不在 HAL 等待中暂停内核或屏蔽 Tick。不能把调试器暂停导致的墙钟延时当作驱动时限证据。

## 下载范围审核要求

脚本逐条验证两个 HEX 的 Intel HEX 校验和和数据地址：CM7 必须完全位于内部 Flash `0x08000000..0x080FFFFF`，CM4 位于 `0x08100000..0x081FFFFF`；并检查当前小镜像均只占各自首个128KiB范围。实际 HEX 起止、字节数和 SHA256 记录于 download_scope.json，execution_authorized 永远为 false。

这仅证明候选文件的数据范围，不保证调试器所选擦除策略。后续审核必须限定为匹配器件/选项字节实际映射的内部 Flash 算法、仅所需扇区，禁用全片擦除、外部 QSPI loader、自动外部初始化、选项字节写入及调试自动下载。不能只凭 HEX 范围认定工具不会扩大擦除。

候选固件会临时替换板上对应内部 Flash 中的原 CM7/CM4 固件；获准执行前需确认原固件备份和恢复版本，并将双核镜像及明确扇区范围一并审批。源码中的生产 Application 保持不变，不代表板上旧固件仍同时运行。

仍需审核板卡版本与 PCB、U5 实装/焊接、VCC/GND/CS 与通断，确认探针/调试配置和双核镜像匹配。不得提前假定 JEDEC=EF4018。识别失败时先保存快照，禁止追加写使能/擦除/解保护/复位“尝试修复”。下一步仍需独立实板授权，不进入 Q1.2。
