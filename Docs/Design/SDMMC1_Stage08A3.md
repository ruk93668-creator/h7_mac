# SDMMC1初始化与实板识卡（08A-3）

## 默认状态

只接入BSP与可选测试入口，默认APP_SDMMC_TEST_ENABLED=0，不自动识卡，不新增任务。默认g_sdmmc_test.enabled/attempted/finished均为0，不能据此认为识卡成功。
现有Camera、USB、IPC、CM4 OLED及HAL/RTOS时基不修改。CM7专用预包含启用SD HAL，软件等待默认1000ms，不是整个识卡流程总超时。

## 接口

| API | 约定 |
|---|---|
| BSP_SDMMC_ReadDetect | 普通线程配置并读取PH14，返回原始电平及当前宏解释结果 |
| BSP_SDMMC_Init | 低速1-bit同步识卡，必须允许TIM6 Tick运行；单次操作互斥，失败保留诊断 |
| BSP_SDMMC_GetCardInfo | 只返回已验证卡信息，不发扇区读写；检测无卡拒绝旧副本 |
| BSP_SDMMC_GetDiagnostics | 只复制本地诊断，正在初始化返回BUSY |
| BSP_SDMMC_DeInit | 清理本外设；清理失败不能重新Init，允许再次DeInit |

不公开HAL句柄。没有热插拔自动重挂载；换卡需要DeInit→Init，不把旧Ready当作新卡身份。

## 第一步：核实PH14有效电平

在同一工程的h7_mac_CM7 → C/C++ → Define追加：

```text
APP_SDMMC_TEST_ENABLED=1,APP_SDMMC_DETECT_ONLY=1
```

全量重编后烧录CM7，CM4沿用配套IPC V3固件。观察g_sdmmc_test：enabled=1、detect_only=1、attempted=1、finished=1，result=0只表示检测输入读取成功，**不是识卡成功**。

无卡和插卡各冷启动一次，记录g_sdmmc_test.detect_raw。也可在运行时观察`GPIOH->IDR`的bit14；g_sdmmc_test是冷启动快照，不会随着插拔实时重采。
原理图R49为10k上拉，默认BSP_SDMMC_DETECT_ACTIVE_LOW=1是图纸推断。若实测有卡为1、无卡为0，在CM7 Define增加`BSP_SDMMC_DETECT_ACTIVE_LOW=0`后重编；若两种状态电平不变，先排查卡座开关和线路，不强行跳过检测。

## 第二步：完整低速识卡

确认检测电平后，将宏改为：

```text
APP_SDMMC_TEST_ENABLED=1,APP_SDMMC_DETECT_ONLY=0
```

不要同时保留两份不同值的宏。仍使用h7_mac_CM7正式Target，全量编译，烧录并完整断电启动。
测试只在APPLICATION准备阶段执行一次，不循环重试，失败不会作为Framework错误返回；同步HAL可能使CM7稍晚启动Scheduler，期间CM4可能短暂显示STARTUP TIMEOUT，之后观察真实心跳变化恢复，不能为此伪造Ready。

## Keil Watch

展开`g_sdmmc_test`：

| 字段 | 成功时检查 |
|---|---|
| enabled / detect_only | 1 / 0，确保运行完整识卡镜像 |
| attempted / finished | 1 / 1 |
| result | SYS_OK=0 |
| diagnostics_result | SYS_OK=0 |
| diagnostics.stage | BSP_SDMMC_STAGE_READY=6 |
| diagnostics.ready | 1 |
| diagnostics.detect_raw / card_present | 与实测电平一致 / 1 |
| diagnostics.kernel_clock_hz | 240000000 |
| diagnostics.sd_clock_hz / clock_div | 400000 / 300 |
| diagnostics.bus_width | 1 |
| diagnostics.hal_status / hal_error | HAL_OK=0 / 0 |
| diagnostics.rcc_status | HAL_OK=0 |
| card.logical_block_size | 512 |
| card.logical_block_count | 非零，按卡实际容量 |
| card.capacity_bytes | uint64，等于logical_block_count×512 |
| card.card_type | HAL编号0=SDSC，1=SDHC/SDXC合并类型 |
| card.card_state | TRANSFER=4 |

无卡时完整探针result预期NOT_FOUND=-2（0xFFFFFFFE），stage=DETECT=1、ready=0，Camera/USB/IPC仍应继续启动。CLOCK=2、INIT=3、INFO=4、STATE=5分别指示时钟、HAL识卡、缓存信息和实时卡状态阶段。若stage=0且result=-4，说明尚未进入硬件步骤，先核对TIM6 CEN/UIE、NVIC使能以及PRIMASK/BASEPRI。
错误时保存result、hal_status、hal_error、rcc_status、status_register、clock_register、response1、cleanup_result及elapsed_ms。BSP返回SYS_ERR_IO=-9并不总能指出卡内原因，不要把hal_error=0当作一定没有错误。

## 验收项目（必须实板执行）

1. 无卡启动：不阻止原功能，原视频、IPC及OLED正常。
2. 有卡识别：result=0、ready=1、容量和卡标称合理对应、卡处于TRANSFER。
3. 逻辑分析仪验证PC12初始化及结束后约400kHz，DAT0/CMD有协议活动，DAT1～DAT3未用于4-bit传输。
4. 无卡/检测与识卡失败后再插入卡并重新冷启动，能够重新识别；不宣称自动热插拔支持。
5. 恢复默认关闭测试后，Camera/USB/IPC/OLED启动和持续运行保持原行为。

## 测试镜像位置与清理

自动构建的两个镜像分别在：

- `output/SdmmcStage08A3/firmware/detect/h7_mac_CM7.axf`：仅检测。
- `output/SdmmcStage08A3/firmware/probe/h7_mac_CM7.axf`：完整识卡。

这两份镜像与根目录常规h7_mac_CM7.axf不同，必须使用对应AXF的符号调试。两份测试镜像中的g_sdmmc_test地址见probe_build_results.json，不建议手工写死地址。
自动脚本结束会还原工程宏并最终重编默认固件。手工实板验收后移除两个测试宏或设APP_SDMMC_TEST_ENABLED=0，再全量重编；不要只改宏而烧录旧AXF。

没有FatFS、DMA、扇区读写、卡速测试，也没有修改PLL1/PLL2或摄像头XCLK；真实卡兼容性与电气行为尚待用户验收。
