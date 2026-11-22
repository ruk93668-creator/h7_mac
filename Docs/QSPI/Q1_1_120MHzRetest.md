# 120 MHz JEDEC 只读复测配置

日期：2026-11-22。用户要求配置120MHz重新测试；烧录和实板调试仍由用户手动执行，唯一工程为 `MDK-ARM/h7_mac.uvprojx`，只使用 M7 的 QSPI。

## 本轮修改

- `CM7/BSP/bsp_qspi.c`：目标上限从30MHz改为120MHz，继续使用实际 D1HCLK 和向上取整分频，不修改 PLL 或其他模块。HCLK=240MHz 时 prescaler=1，计算串行时钟120MHz。
- 高速读取启用 HAL 的 `QSPI_SAMPLE_SHIFTING_HALFCYCLE`（SSHIFT=1），并在配置校验中要求该位匹配；不改变单线9F指令、三字节接收、CS high=8周期、mode0及故障契约。不自动降频掩盖失败。
- `Tests/QspiFlashQ1/behavior_test.c`：Mock 反映 HAL 的采样位配置，成功路径检查分频1、频率不超过120MHz和SSHIFT读回。

30MHz实板截图是先前的识别证据，不作为120MHz通过证据。本轮未连接探针、烧录或操作实板；GPIO波形、实际频率、采样裕量和120MHz真实通信仍待手动复测。

## 软件验证

51/51 Mock 用例、992条实际断言通过，真实 ST HAL 链接及默认初始化超时验证通过；原始输出另存于 `Tests/QspiFlashQ1/hardware/evidence/120mhz/`，保留先前30MHz验收证据不覆盖。原工程 CM7/CM4 重建结果以该目录 build_results.json 及原始日志为准。

用户原有 uvprojx/uvoptx 改动保留、不夹带提交。本轮没有修改 M4、生产 main、Application、Platform、Framework 或工程源文件条目。

## 手动重新测试

1. 在原工程选择 **h7_mac_CM7**，使用本轮最新构建手动下载 M7；M4 不需要改刷独立候选。
2. 复位并 F5，观察 `g_q1_hw_report` 和 `g_q1_hw_arm`。等待 phase=2、complete=0。
3. CPU保持运行，将 `g_q1_hw_arm` 写为 `0x51315244`，等待 complete=1。
4. 检查 phase=4，init_result/id_result/diagnostics_result 均0，id=0xEF4018，diagnostics.ready=1、fault=0、received_bytes=3。
5. 核对 `hclk_hz` 和 `diagnostics.kernel_clock_hz` 均为240000000、`diagnostics.prescaler=1`、`diagnostics.serial_clock_hz=120000000`、`diagnostics.cr` 的 SSHIFT 位为1。若实际HCLK不同，以真实读回和计算结果为准。

请保存展开字段的 Watch 截图及失败诊断。不得在 HAL 等待期间暂停内核或屏蔽TIM6。若120MHz失败，保留当前结果，不自动重试、复位器件或擦写。此测试仅验证高速单线JEDEC识别；不代表120MHz四线数据读取或长期稳定性通过，不进入Q1.2。
