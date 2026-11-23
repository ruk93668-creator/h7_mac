# Q1.2 只读数据与 Quad Output 软件交付

交付日期：2026-11-23。冻结输入：`457bd068dc53cfb61347aff090670dc98064c557`。
工作分支：`codex/qspi-flash`。软件检查通过，等待 GPT 技术审核；实板 Quad 数据正确性、波形和稳定性均为 **NOT_VERIFIED**。

## 变更范围与提交

- Q1.2A：`46c55deffd09c32285ccc8a03da2093e3798a218`，状态和单线读取（提交时标记验证进行中）。
- Q1.2B：`223f86c238deefe8ce3265c235e451b7bffd5fad`，Quad 前置条件、边界和故障诊断。
- Q1.2C：本报告所属 Git 提交，最终答复提供完整交付 SHA；包含测试、构建证据和本报告。

生产差异仅 `CM7/BSP/bsp_qspi.c/.h`；C 文件原有 Q1.1 实现逐字保留，只在末尾扩展。HAL 句柄继续私有，复用原子操作权，无动态内存、新任务或 Mutex。未修改 Application、Platform、Framework、CM4、PLL、FreeRTOS 或 HAL 源码。

用户已有 `MDK-ARM/h7_mac.uvprojx/.uvoptx` 修改保留且不提交。正式 Rebuild 使用同一份本地原工程配置的隔离副本，基线和当前工程 SHA256 均为 `6fd94928da5f3fa9839b6ad5d097fc973de1830c955e133afcba2800a2f0c06e`；未建立另一套生产工程配置。QSPI 测试继续使用原工程已有的 M7 测试任务。

## API 与状态契约（已核验源码事实）

定义见 `CM7/BSP/bsp_qspi.h:11`、`:94`、`:100`，实现见 `bsp_qspi.c:573`、`:596`。

```c
SysResult_t BSP_QSPI_ReadStatus(BspQspiStatus_t *status, uint32_t timeout_ms);
SysResult_t BSP_QSPI_Read(BspQspiReadMode_t mode, uint32_t address,
    void *buffer, uint32_t size, uint32_t *completed, uint32_t timeout_ms);
```

ReadStatus 返回 SR1/SR2/SR3 和 BUSY/WEL/QE/SUS；失败不修改调用者 status，诊断 `status_valid_mask` 标出已取得的状态。Read 使用 STANDARD、FAST、QUAD_OUTPUT 枚举；合法参数后 completed 置零。调用者须提供独占、有效的可写内存，buffer 和 completed 不重叠；completed 要求 4 字节对齐。不尝试探测任意地址是否确实可写。

参数在外设访问前检查：NULL、零长度、模式、timeout 1..60000ms、24-bit/16MiB 范围、减法长度判断、指针加法溢出和输出重叠。非法参数不锁存 FAULT。随后检查普通线程/TIM6/中断环境、操作权和 READY/FAULT。

每次数据读取先读取三组状态。BUSY 拒绝数据读取并返回 BUSY，SUS 返回 INVALID_STATE；QE=0 仅拒绝 Quad，保留异常状态，不写入修复。这些前置条件失败不锁存硬件 FAULT。WEL 仅记录，不修改。

`bsp_qspi.c:413` 起使用私有 256 字节暂存区。HAL 完成、RxXferSize/RxXferCount、DLR、上下文、配置和时间检查全部通过后，才复制本段并增加 completed。失败只保证 `buffer[0..completed)` 有效；其后保持调用前内容。不会将部分读取报告为整次成功。

所有分段共享 started 和 timeout；每个 HAL 调用使用 min(剩余预算,100ms)，不会每段重新获得完整预算。HAL Tick 必须在整个等待过程中持续运行；其他代码中途停止 Tick 时，不能承诺墙上时间上限。故障清理另计时，不包含在读取工作预算中。

真实 HAL 错误、超时、短接收或硬件配置异常先保存 read_result、HAL 错误/状态及寄存器快照，再锁存 FAULT、清除 READY 并尝试原有有界清理。上下文失效或意外 DMA 时不调用不安全 Abort，保留资源与 FAULT。Abort 失败独立记录，不能覆盖首错；后续 Read/Init 不能自动恢复，需系统复位后重新识别。原有四 API 的行为契约保持不变。

新增诊断在旧结构尾部追加，旧初始化字段含义不变；包含读取模式、地址、请求/确认长度、阶段、opcode、工作/清理耗时、HAL 错误与快照。观察时使用符号，不沿用旧结构长度或固定 RAM 地址。

## 命令字段（软件已核验，实板未验证）

|模式/状态|指令|Instruction|Address|Data|地址位数|Dummy|
|---|---|---|---|---|---|---|
|SR1/SR2/SR3|05/35/15|1 line|none|1 line|无地址|0|
|STANDARD|03|1 line|1 line|1 line|24|0|
|FAST|0B|1 line|1 line|1 line|24|8|
|QUAD_OUTPUT|6B|1 line|1 line|4 lines|24|8|

均关闭 DDR、AlternateBytes，每条命令发 Instruction。不是 QPI/EB。固定 120MHz 下 STANDARD 在发送任何命令前返回 NOT_SUPPORTED；不动态改频。48MHz 模拟配置验证了 03 的可用路径，未在硬件运行该配置。

保留 D1HCLK、向上取整分频、Half-cycle Sample Shifting 配方（`:103`、`:118`、`:315`、`:318`）。事务前后复核 HCLK、CR/DCR、GPIO、HAL READY，拒绝 DMA/IRQ 和自动轮询/Memory Mapped。240MHz HCLK / 2 是软件配置计算值；本轮没有测量实际 CLK。

## 测试与原始证据

全部证据位于 `Tests/QspiFlashQ1/q12/evidence/`，manifest.json 记录文件 SHA256。

|检查|实际结果|证据|
|---|---|---|
|原 Q1.1 Mock|51/51，992 断言|q11/results.json、run.log、commands.json|
|新增 Q1.2 Mock|516/516，285725 原始断言（包含每例初始化夹具）|mock/results.json、run.log、commands.json|
|真实 ST HAL 链接|包含两个新 API，链接成功|hal_link/commands.json、link_map.log|
|真实 HAL 初始化超时仿真|HAL_ERROR/TIMEOUT，默认5000ms，5002 Tick 调用|hal_link/results.json|
|CM7/CM4 完整 Rebuild|两核均0错误、0警告|current_build.json、current/*.log|

新增矩阵为六种数据模型 × 七个地址 × 十种长度，共420例（含末端越界组合）；20例非法参数，48例事务故障，28例状态/上下文/占用/恢复边界。地址包括0、1、FF、100、FFF、1000、FFFFFF；额外非法参数覆盖1000000。长度为1/2/3/4/255/256/257/512/1024/4096。模型包含00、FF、55、AA、递增和伪随机；有效组合逐字节比对 FAST、Quad 和独立模型，检查缓冲区哨兵。

非法参数期间仿真监视全部外设地址访问，确认无 MMIO；HAL Command/Receive 的 BUSY/ERROR/TIMEOUT、DLR 异常、短接收、HAL_OK 带错误、超预算分别注入三条状态命令和首条数据命令。另覆盖后段失败有效前缀、Tick 回绕、收缩期限、QE/BUSY/SUS、屏蔽上下文、TIM6/NVIC、六 API 重入竞争、Abort 失败、DMA/映射/时钟漂移及重复读取。

Mock 不模拟真实电气、FIFO 时序或数据线置换；真实 HAL 完整 Command→Receive 数据路径尚未在本轮执行。链接和初始化超时不能替代实板事务验收。

复现入口：`run_tests.py`、`run_read_tests.py`、`run_q12_hal.py`、`run_q12_build.py baseline/current`、`run_q12_regression.py`、`collect_q12_evidence.py`，均在 `Tests/QspiFlashQ1/` 下。构建输出存在时拒绝覆盖，需保留旧证据后另行归档输出目录再运行。

## 双核资源与栈

|内核/版本|Code|RO|RW|ZI|ROM=Code+RO+RW|RAM=RW+ZI|
|---|---:|---:|---:|---:|---:|---:|
|CM7冻结基线|142302|3146|1360|104448|146808|105808|
|CM7当前|144334|3218|1368|113384|148920|114752|
|CM7增量|2032|72|8|8936|2112|8944|
|CM4两侧相同|26302|1438|936|3576|28676|4512|

对象表中 BSP 的 ZI 208→560、RW12→13；测试对象 ZI3328→11884、RW4→8，含两个4096字节缓冲区和扩展诊断；总量另含对齐差异。测试静态栈仍为768 words=3072字节，未新增分配。Q1_Task 静态调用链264→704字节，Read调用链264字节；全工程最大静态栈仍1344字节（CM4为432）。链接器标有未知调用/循环等限制，未做实板高水位测量。详见 resources.json。

公共 ReadStatus 在完整镜像中因无直接调用被链接器剔除，其真实 HAL 可链接性由独立链接测试覆盖；完整镜像通过 BSP Read 的内部状态路径读取三组状态。

## 历史回归与资源复核

冻结基线与当前均独立运行三组原始脚本：SDRAM 26例18通过8失败；DCMI133例88通过45失败、5475断言；USB摘要8例7通过1错误，缺失 camera_mjpeg_fifo.h 包含路径。没有修改原有脚本或相关生产模块。逐例比较一致；USB仅归一化临时路径和测试耗时，原始 FAIL/ERROR 日志完整保留，未标记为全通过。见 regression/comparison.json 和两侧日志/JSON。

沿用 Q0 引脚证据：PF6/7 AF9，PF8/9 AF10，PF10 AF9，PG6 AF10。当前 SDRAM 用PF0–5/11–15及其他PG脚；SDMMC PC8–12/PD2/PH14；DCMI PC6/7、PG10、PH12、PE4–6、PB6/7、PA4/6；USB PA11/12，无六脚直接重叠。源码依据见 `bsp_sdram.c:59`、`bsp_sdmmc.c:366`、`bsp_dcmi.c:241`、`bsp_usb.c:584` 及 Q0_HardwareAudit.md。共享 GPIO 端口不整端口复位或关闭；QSPI 不改 PLL1Q、PLL2R、HSI48。双核/外设同时运行的实板稳定性仍未验证，CM4 不得访问 QSPI。

## 实板准备与未关闭事项

已准备原 M7 测试任务中的独立 RAM 门闩扫描入口，见 `Q1_2_HardwareTestPlan.md`。未下载、未运行、未采集波形；未分配擦写扇区。Flash 命令仅9F/05/35/15/03/0B/6B，无写使能、写状态、QE修复、解保护、擦写或 Flash Reset；现有 RCC 控制器复位不属于 Flash Reset 指令。

建议经 GPT 软件审核和下载范围审批后，申请 Q1.2 实板只读验收。未关闭：实际供电与板卡版本复核、120MHz信号质量/采样裕量、真实HAL接收、IO0–3物理活动、非均匀内容一致性、长期稳定性、运行时栈余量。若只有全FF/大部分FF，不因FAST/Quad一致而放行数据完整性；保持NOT_VERIFIED，提交证据后另议方案。不得自动降频、擦写制作图案或进入Q1.3。
