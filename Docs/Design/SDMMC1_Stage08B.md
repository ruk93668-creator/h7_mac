> 后续维护：用户已批准删除不兼容测试，见 Tests/TestMaintenance/incompatible_tests_removed.json。本文原回归数量是提交13364dc时的历史结果；当前集合另见active_regression_results.json，删除项不计为PASS。

# 阶段08B：SDMMC1 IDMA块设备设计与验收

日期：2026-11-08。基线：codex/stage08a3-sdmmc1@d0305de（包含1f98b9c及已实板验证的内部下拉、高有效插卡修复）。开发分支：codex/stage08b-sdmmc-idma。未合并main。

## 实现范围

- 识卡保持400kHz、1-bit；识卡成功后在低速下切换4-bit及High Speed，最后设置40MHz。卡只宣告普通速度时回退24MHz，切换失败进入故障，不能假报高速成功。
- PLL1Q仍为240MHz，ClockDiv分别为300、3、5；不修改PLL1/PLL2、相机XCLK或SDRAM。
- SDMMC1内部IDMA使用单缓冲模式，不申请DMA1/2 Stream。
- 保留08A的BSP_SDMMC_Init低速识卡接口。SD_Block_Init负责随后显式切换正式传输模式。
- 不接FatFS、MSC、JPEG保存；不改Camera/USB/IPC/OLED业务代码、现有任务和帧租借。

## AXI内存与Cache

| 范围 | 用途 | 属性 |
|---|---|---|
| 0x20000000～0x2001FFFF | 原CM7普通RW/ZI、任务及中断栈 | DTCM，IDMA不能直接访问 |
| 0x24000000～0x24007FFF | SDMMC专用32KiB中转区 | MPU Region3，Normal、Non-cacheable、Shareable、读写、XN |
| 0x38000000～0x38000FFF | 原双核IPC窗口 | Region1不变 |
| 0xD0000000～0xD03FFFFF | 原SDRAM、图像五缓冲及MJPEG FIFO | Region2不变 |

专用段`.sdmmc_dma`在Scatter独立分配，UNINIT、zero_init段类型、至少32B对齐，实际保留32KiB。UNINIT表示启动不清空，不表示可发送旧数据：写入前逐批复制完整数据；读取仅在完整成功后复制。
运行期核对链接地址、容量、MPU Region3实际RBAR/RASR及使能状态。编译期检查区域大小、基地址对齐和512B整数倍。当前源码审计Region0/1/2/3不重叠；未实现任意第三方模块运行期重配MPU后的自动修复。
不更改全局Cache开关。Non-cacheable区不执行Clean/Invalidate；交接使用DSB。调用者的缓存区只由CPU复制，不被IDMA直接访问。因此调用者可使用合法DTCM或未按32B对齐的CPU缓冲区。
地址范围校验排除外设、NULL、指针回绕、未知内存和专用DMA区。读目的必须为已允许RAM范围；写源额外允许Flash。测试不模拟真实Cortex-M7 Cache行驱逐；实板Cache/总线行为仍须验收。

## 同步、IRQ与所有权

```text
任务调用SD_Block_Read/Write
  → BSP取得唯一操作权
  → 每批最多64扇区，必要时CPU复制到AXI
  → HAL_SD_ReadBlocks_DMA/WriteBlocks_DMA
  → SDMMC1_IRQHandler锁存事件、屏蔽该外设事件源、通知静态二值信号量
  → 任务醒来，由BSP调用实际HAL_SD_IRQHandler
  → HAL完成/错误回调更新当前批次
  → 核对IDMA关闭、DPSM/CPSM停止、DCOUNT=0、HAL READY、卡TRANSFER
  → 读操作复制给调用者；写操作等待卡内部编程结束
```

IRQ优先级7，低于现有DCMI/DMA的5、USB的6，满足FreeRTOS最大系统调用IRQ阈值5；不改原优先级。
HAL当前多块完成和错误分支会发送同步CMD12，故不在真实ISR里直接运行这部分HAL代码。HAL的Rx/Tx/Error/Abort回调在本设计中由任务侧HAL处理路径执行。ISR只通知，不判定最终成功，不复制数据。
静态二值信号量合并唤醒，唤醒本身不是数据完成证明。没有StorageTask，默认不新增任务。每次最多阻塞5ms后复核状态，可发现IRQ丢失。保留SDMMC Sleep时钟，使任务阻塞后CPU进入WFI时传输不被意外门控。

| 阶段 | AXI所有者 | 允许操作 |
|---|---|---|
| 空闲/准备 | CPU | 写请求先复制数据 |
| IDMA在途 | 硬件 | CPU不得改写 |
| 数据通路完成 | CPU等待卡状态 | 不能只凭回调复用 |
| 整批成功 | CPU | 读取复制、推进已完成块数 |
| Abort失败 | 驱动锁存FAULT | 保留active，禁止再次请求 |
| 显式Recover验证停止成功 | CPU | 撤销旧卡信息，必须重新Init |

错误优先：先锁存原始STA错误，再调用HAL，防止当前HAL先处理DATAEND并清除同时出现的错误，或ErrorCallback之后又调用完成回调。
同一请求只取一次操作权，其他任务重复提交返回BUSY；缓冲区不对外出借。操作只允许Scheduler运行、非ISR、PRIMASK/BASEPRI均未阻塞等待的上下文。

## 超时与恢复契约

- timeout_ms是整次请求统一截止时间，分批不续期，采用无符号Tick差值处理回绕；最大接受INT32_MAX。
- 该截止时间不是硬实时返回保证：厂商HAL内部同步命令及Abort有自己的等待上限，返回后再复核截止时间，清理也可能增加时间。任务仍可被Camera/USB等高优先级任务抢占，不在关中断区运行这些等待。
- 每次唤醒检查卡检测、硬件错误和时间；传输失败不交付本批读数据。大请求前面成功批次可能已复制，整体失败时调用者必须丢弃整次读取结果。
- 写入可能部分成功，禁止自动重试，不承诺多扇区原子性。
- 原始result/hal_error与recovery_result分开保存。HAL Abort返回成功后仍核对硬件静止；失败保留active和FAULT。
- Recover只在没有并发调用时重置本SDMMC1外设，核对停止并DeInit，撤销旧卡信息；成功后重新Init，不直接把HAL State强改READY。
- 观察到拔卡会使旧会话失效。没有热插拔后台任务，未观察到的快速换卡不构成可证明的会话识别，本轮不支持并发热换卡。

## 公共接口

```c
SysResult_t SD_Block_Init(void);
SysResult_t SD_Block_Read(uint32_t lba, uint32_t count, void *buffer, uint32_t timeout_ms);
SysResult_t SD_Block_Write(uint32_t lba, uint32_t count, const void *buffer, uint32_t timeout_ms);
SysResult_t SD_Block_GetInfo(BspSdmmcCardInfo_t *info);
SysResult_t SD_Block_Recover(void);
```

Read/Write同步等待当前调用任务，调用返回前保持CPU缓冲区有效，禁止其他上下文同时改写。成功表示整个请求完成；失败请查看BSP_SDMMC_GetIoDiagnostics，不根据完成回调猜测成功。
通用Write是未来文件系统的块设备能力，不内置分区解析或写授权。当前实板写测试另设默认关闭的严格授权入口，应用不得绕过该入口进行试写。

## 软件验收

- ARMCC5 V5.06 update7 build960，ARM机器码在Unicorn执行。
- 08B真实BSP＋真实HAL Read/Write DMA、IRQ、Abort路径：44场景×O0/O2＝88次PASS。卡命令响应/LL及MMIO由桩提供，不等于电气或DMA总线实测。
- 真实Platform等待适配和真实写测试策略：16场景×O0/O2＝32次PASS。使用真实FreeRTOS头文件，但队列/任务调度接口为可控桩；不宣称这是实际调度器压力验收。
- 08A原50个场景及断言不变，4种配置×O0/O2＝400次PASS，原测试源码未修改。
- 合计520次可执行测试PASS；7项写开关/身份/范围编译保护检查PASS。
- 其余历史回归见下表。失败专项已用d0305de源码快照实际重跑，13组的统计与失败集合相同，无08B新增退化；不能宣称全部历史测试通过。
- 真实40MHz读写、Cache/总线行为、视频并行性能、真实扇区内容、真实写入及恢复：均未验证。

| 历史专项 | 当前结果 |
|---|---|
| IpcV2_run_tests | {'total': 512, 'passed': 512, 'failed': 0} |
| IpcRuntime_run_tests | {'total': 260, 'passed': 260, 'failed': 0} |
| IpcRuntime_run_task_tests | {'total': 20, 'passed': 20, 'failed': 0} |
| IpcStatus_run_tests | {'total': 448, 'passed': 448, 'failed': 0} |
| IpcStatus_run_kernel_tests | {'total': 41, 'passed': 41, 'failed': 0} |
| OledAsyncAcceptance_run_tests | {'total': 50, 'pass': 50, 'fail': 0, 'not_run': 0, 'assertions': 11156, 'compiler': 'Product: MDK Plus 5.32\r\nComponent: ARM Compiler 5.06 update 7 (build 960)\r\nTool: armcc [4d365d]\r\nFor support see http://www.arm.com/support\r\nSoftware supplied by: ARM Limited\r\n', 'unicorn': '2.1.4'} |
| OledMonitor_run_tests | {'total': 122, 'passed': 122, 'failed': 0, 'assertions': 9076} |
| UsbCoreDecoupling_run_tests | {'total': 204, 'passed': 201, 'failed': 3, 'assertions': 2947} |
| Ep0IrqRace_run_race | {'total': 4, 'passed': 4, 'failed': 0} |
| UvcPayload_run_tests | {'unique_cases': 62, 'executions': 124, 'passed': 122, 'failed': 2} |
| UvcProbeCommit_run_tests | {'total': 89, 'passed': 0, 'failed': 89} |
| UvcProbeCommit_run_cancellation_tests | {'total': 39, 'passed': 0, 'failed': 39} |
| UvcEp1Lifecycle_run_tests | {'total': 45, 'passed': 0, 'failed': 45} |
| UvcStreaming_run_tests | {'total': 36, 'passed': 1, 'failed': 35} |
| CameraMjpegStage03B_run_media | 未执行到断言：原测试编译/链接失败 |
| CameraMjpegStage03B_run_native | 未执行到断言：原测试编译/链接失败 |
| CameraMjpegStage03B_run_usb | {'total': 13, 'passed': 13, 'failed': 0} |
| CameraStreamState_run_tests | 未执行到断言：原测试编译/链接失败 |
| CameraSingleFrame_Camera_run_tests | 未执行到断言：原测试编译/链接失败 |
| CameraFormats_Camera_run_tests | 未执行到断言：原测试编译/链接失败 |
| NativeRuntimeStage02_run_native | 未执行到断言：原测试编译/链接失败 |
| NativeRuntimeStage02_run_usb | {'total': 11, 'passed': 11, 'failed': 0} |
| NativeRuntimeStage02_run_bsp | {'total': 7, 'passed': 7, 'failed': 0} |
| DcmiReplaceTarget_run_tests | {'total': 133, 'passed': 88, 'failed': 45, 'assertions': 5475} |

已实际执行的历史测试合计2158次：1900 PASS、258 FAIL；另6组无法编译/链接。IPC/OLED原1453次全部PASS。
主要历史问题：旧UVC夹具未调用当前必需的USB_UVC_Configure；Payload仍把513B视为非法而当前支持1023B；旧Initcall期望已过时；Camera旧夹具缺失CameraMjpegFifo/App_StatusPublish依赖或usb_mode头文件路径；DCMI原45项失败保持。未修改这些历史断言或无关生产代码。
详见Tests/SdmmcStage08B/baseline_comparison.json与每组原始日志。

## 修改文件与原因

| 文件 | 原因 |
|---|---|
| CM7/BSP/bsp_sdmmc.c/.h | 正式4-bit速度、IDMA事务、IRQ、原始错误、卡身份与恢复；保留低速识卡和高有效检测 |
| CM7/BSP/bsp_sdmmc_memory.c | AXI段、Region3配置、属性校验和启动期非致命准备 |
| CM7/Platform/sd_block.c/.h | 通用块接口、静态二值信号量及RTOS等待适配 |
| MDK-ARM/h7_mac_CM7_custom.sct | 专用32KiB AXI执行区，与普通DTCM分离 |
| MDK-ARM/h7_mac.uvprojx | 仅叠加新增文件；CM4明确排除，不提交用户已有项目改动 |
| Tests/SdmmcStage08B/ | 新专项、原测试回归执行器、基线复现、默认关闭实板入口、构建和验收记录 |
| Docs/Design/SDMMC1_Stage08B.md | 本设计、调用约定、实板步骤和风险 |

HAL/LL、Camera、USB、IPC、CM4业务代码、RTOS配置与任务行为均未修改。默认测试关闭时不创建验收任务。用户本机uvoptx、已有uvprojx修改、输出和抓包保留。

## 实板只读验收

1. 正常固件APP_SD_BLOCK_TEST_ENABLED=0，不自动识卡或访问扇区。可选使用已构建的只读镜像：output/SdmmcStage08B/firmware/read_only/h7_mac_CM7.axf。不要使用write_compile_only镜像做实板写测试。
2. 或在当前CM7 Define设置APP_SDMMC_TEST_ENABLED=0、APP_SD_BLOCK_TEST_ENABLED=1、APP_SD_BLOCK_WRITE_TEST_ENABLED=0，使用同一个正式工程编译。
3. 配套CM4沿用当前固件。确认R49外部上拉已拆除，PH14内部下拉，空卡0/插卡1。测试卡接PC8～PC11、PC12、PD2，3.3V和CMD/DAT外部上拉保持原样。
4. 烧录、完整复位，Camera/USB/IPC/OLED正常后，在Watch将g_sd_block_test.request设为1。没有请求时不读卡。
5. 等待state从1变2；result=0，init.ready=1，init.bus_width=4，支持High Speed时init.sd_clock_hz=40000000、io.high_speed=1；普通卡回退24000000、high_speed=0。
6. card.logical_block_size=512、capacity_bytes=7795113984；read_crc与repeat_crc一致。io.result=0、active=0、fault=0，io.dma_address=0x24000000、dma_bytes=32768。
7. 分别无卡启动、插卡后请求、重复只读、读期间拔卡，记录结果及io.recovery_result。故障后request=4执行Recover＋重新识卡，再request=1。恢复失败不得继续读写。
8. 确认正常任务仍运行，观察Camera/USB实际FPS、IPC心跳延迟、任务栈余量和OLED；连续运行至少5分钟。记录实际吞吐，不把40MHz或20MB/s理论值当作写速。
9. 测试完成后关闭APP_SD_BLOCK_TEST_ENABLED。Keil停机查看请放在请求结束后，传输中单步/暂停会扰动时序。

## 实板写入验收（已配置授权参数，执行前完成PC备份）

用户已提供CID，并明确授权该卡现有数据可覆盖。依据导出的MBR，两个分区分别为LBA1024～17407、17408～140287；选定授权测试窗口LBA1048576～1048583，共8扇区4096B，位于512MiB偏移，不覆盖已声明分区或LBA0。PC备份尚未确认，当前不触发真实写入。
用户须先在PC备份测试卡数据，并明确保留、未被分区/文件系统使用的原始区域；不能把文件系统中的空闲簇当作安全原始区域，不使用LBA0或未知元数据。
随后另行配置APP_SD_BLOCK_TEST_ENABLED=1、APP_SD_BLOCK_WRITE_TEST_ENABLED=1、SD_TEST_CID0～3、SD_TEST_CAPACITY_BYTES、SD_TEST_WRITE_FIRST_LBA、SD_TEST_WRITE_BLOCKS。缺少参数或LBA0会编译拒绝。
每次写前核对CID、容量和范围；入口只使用授权区前8扇区。先读备份到独立RAM，request=2执行写模式、逐字节回读、恢复原扇区、再次逐字节核对。
恢复结果必须restore_result=0、restore_required=0。任何失败保留原错误及备份；request=4只恢复外设和重新识卡，之后request=3再尝试恢复原数据。身份不匹配拒绝写入。
恢复前不要复位或断电，否则RAM备份会丢失；PC外部备份仍是必要前提。测试过程中不允许换卡。

## 剩余风险与状态

- 40MHz、4-bit引脚信号完整性、SD卡高速度协商和持续读写未实板验收。
- SD卡内部写停顿与最坏延迟不可由理论带宽保证；HAL内部命令等待可能超过调用者软件截止时间。
- 当前单缓冲方案包含CPU复制，未实现双缓冲、存储队列或持续录像缓存。
- 六组旧测试无法运行、258项历史失败在基线已存在；本次仅证明没有新增退化，未完成全仓历史测试修复。
- 软件专项通过不等于实板只读通过，更不等于真实写入或原数据恢复通过。

## 本卡写测试配置补记（2026-11-08）

用户已确认4-bit、40MHz IDMA只读通过，读取4096B的CRC为0xA55822CC，导出的sd_layout.hex CRC一致。容量7795113984B，逻辑块512B。

```text
SD_TEST_CID0=0x9F544930U
SD_TEST_CID1=0x30303030U
SD_TEST_CID2=0x00000189U
SD_TEST_CID3=0xBD013CBAU
SD_TEST_CAPACITY_BYTES=7795113984ULL
SD_TEST_WRITE_FIRST_LBA=1048576U
SD_TEST_WRITE_BLOCKS=8U
```

本机保留用户已开启的APP_SD_BLOCK_TEST_ENABLED=1和APP_SD_BLOCK_WRITE_TEST_ENABLED=1，仅补全授权参数；未烧录、未执行request=2。
Git正式项目仅备份上述七项授权参数，不默认开启测试；本机其他Keil设置仍不提交。
真实写入前先完成PC备份，烧录本机新编译的CM7镜像后手工request=2。成功必须state=2、result=0、restore_result=0、restore_required=0；恢复未完成不要断电或复位。
单次只读通过不代表持续并行压力、真实写入或原数据恢复通过。
