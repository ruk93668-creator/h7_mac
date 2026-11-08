# 08B 五分钟只读压力测试

## 历史直接压力烧录方式（已关闭）

按用户要求，当前08B分支的h7_mac_CM7 Target已直接启用只读压力宏：APP_SD_BLOCK_TEST_ENABLED=1、APP_SD_BLOCK_STRESS_TEST_ENABLED=1、APP_SD_BLOCK_WRITE_TEST_ENABLED=0；初始识卡测试关闭。头文件默认值仍为0。打开MDK-ARM/h7_mac.uvprojx，选择h7_mac_CM7，编译后点击Download即可；使用正式输出MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf，不再用Command手动LOAD其他镜像。烧录后仍需手动request=5，不会自动启动压力。

额外生成的output/SdmmcStage08B/firmware下normal、read_only、write_compile_only、read_stress、write_ready五个目录已清理。下文多profile路径、内存结果与项目恢复哈希是上一轮构建的历史证据，原镜像路径当前已不存在；只有主动重跑run_profiles.py才会重建。用户其他工程设置保留。验收完需关闭两个APP_SD_BLOCK测试宏并重新编译。

## 修改范围

只修改 Tests/SdmmcStage08B 的独立测试入口、测试桩、构建脚本和记录。复用原 SdTest 静态任务、BOOT 放行和优先级，不新增任务，不改 BSP、Platform 或 Camera/USB/IPC/OLED 业务代码。不合并 main。

## 运行机制

手动 request=5 启动。每轮通过真实 SD_Block_Read 读取 LBA0～64，共65个512B扇区（33280B），内部按32KiB AXI IDMA缓冲分为64+1。CPU目标是测试专用静态缓冲，带首尾哨兵；不是新增IDMA区域。首轮CRC32建立本轮基准，此后每轮比较。CRC重复一致只能证明重复读取一致，不能证明文件系统语义或卡内容正确。

每轮结束阻塞等待20ms，让业务任务运行；最长300000ms、最多15000轮。到时不再发起新读，正在执行的读必须先安全结束或超时。单次调用超时参数2000ms，但已有HAL同步命令和停止清理可能延长退出时间，因此不是严格实时的300000ms硬截止保证。

首次I/O错误、CRC不一致或哨兵破坏立即结束，不自动重试或恢复，不掩盖BSP故障。显式request=4仍由用户手动触发。期间排队的request在压力退出时清除，避免自动启动恢复或下一轮。压力模式与写测试编译互斥，不能写卡。

## 默认关闭与编译

正常固件不定义以下开关，默认均为0；不创建SdTest任务，不分配33280B压力缓冲。用户本地原写测试配置保留，不自动改为压力模式。

压力验收配置（替换同名宏，不要重复定义）：

```text
APP_SDMMC_TEST_ENABLED=0
APP_SD_BLOCK_TEST_ENABLED=1
APP_SD_BLOCK_WRITE_TEST_ENABLED=0
APP_SD_BLOCK_STRESS_TEST_ENABLED=1
```

无需删除原SD_TEST_CID等授权常量；写开关为0时不编译写路径。使用现有h7_mac.uvprojx，不新建临时工程。

`python Tests/SdmmcStage08B/run_profiles.py` 临时构建normal、read_only、write_compile_only、read_stress并逐字节恢复用户项目。write_compile_only使用合成CID，仅验证编译，不能用于实际写卡。压力AXF保存到 output/SdmmcStage08B/firmware/read_stress/h7_mac_CM7.axf。

## 实板步骤

1. 使用压力配置完整编译CM7，配套原CM4固件；Camera、USB、IPC、OLED正常运行并打开视频。
2. 先记录一分钟基线：实际Camera/USB FPS、采集/完成帧数、丢帧数、任务历史最小栈余量、IPC对端状态。
3. Watch确认 enabled=1、stress.enabled=1、restore_required=0；设置 g_sd_block_test.request=5，恢复运行，压力不会自动开始。
4. 保持视频五分钟，避免暂停CPU影响时序。完成后记录下面的结果，再观察一分钟业务是否正常。
5. 中途停止：仅在stress.active=1时设置stress.stop_request=1，等待当前读取安全结束。手动停止不是完整五分钟验收。
6. 结束后将测试开关恢复0并重新构建正常固件。

| Watch | 预期或解释 |
| --- | --- |
| g_sd_block_test.state | 1运行，2成功结束，3失败 |
| g_sd_block_test.stress.active | 运行1，结束0 |
| stress.stop_reason | 1到时，2轮数上限，3手动停止，4I/O，5CRC，6初始化，7缓冲边界 |
| stress.result / g_sd_block_test.result | 成功0，失败保留负数 |
| stress.elapsed_ms | 完整时长验收应达到300000ms；不能只看state=2 |
| stress.completed_rounds / bytes_read | 成功且CRC通过的轮数/字节数 |
| stress.io_errors / crc_errors / buffer_errors | 全部0 |
| stress.reference_crc / last_crc / crc_ready | 基准与末轮一致，ready=1 |
| stress.max_read_ms / average_read_us | 最大单轮耗时/结束时平均耗时 |
| stress.bytes_per_second | 结束时计算，含CRC与等待，不是卡最大吞吐 |
| g_sd_block_test.stack_free_words | 任务历史最小剩余栈，单位Word，乘4为字节 |

压力前、中、后使用同一视频配置比较真实FPS与计数增量；DROP可能因消费者速率而正常增长，应比较单位时间增量，不能仅凭非零判失败。观察已有USB SOF间隔诊断、IPC ONLINE/新鲜度和OLED刷新。现有IPC诊断不等同完整最坏调度延迟测量，未新增业务侧性能探针。不得在SdTest中越权调用仅IpcTask可用的应用快照接口。

## 验收边界

用户此前报告实板只读与写入/回读/恢复通过：state=2、result=0、restore_result=0、restore_required=0。这是用户反馈，非本轮自动化工具实测。五分钟Camera/USB/IPC并发压力尚未在硬件执行；软件模拟不能替代该项。

自动化结果、完整编译和内存对照见本报告后续核验结果及对应JSON/日志。

## 软件核验结果（2026-11-08）

- ARMCC 5.06 update7 build960，Unicorn执行实际BSP/HAL及测试入口：556/556 PASS。其中原520次保留，新增17种压力场景×O0/O2以及关闭入口×O0/O2共36次。
- 编译保护：15/15 PASS（原7项保留，新8项覆盖默认、互斥和边界）。
- 原保留USB/UVC、Camera、IPC、OLED回归：1899/1899 PASS。此前用户要求移除的过时测试不计入通过数量，历史清理证据仍保留。
- 实际AXF内存检查：17/17 PASS，可用 `python Tests/SdmmcStage08B/run_memory_audit.py` 重跑（须先构建四个profile）。
- CM7 normal、read_only、write_compile_only、read_stress：完整重编均0 Error、0 Warning，日志与 profile_build_results.json 对应。
- normal ZI=78164B；read_only ZI=86740B；read_stress ZI=120052B。压力比只读增加33312B，其中33288B为带哨兵缓冲，其余24B来自链接后资源；总ZI包含独立AXI区域，不能全部算成DTCM。
- read_stress缓冲地址0x200026E8，长度33288B；DTCM已分配末端0x20015898，小于0x20020000。IDMA区始终是0x24000000～0x24007FFF；测试CPU缓冲不直接传给IDMA。
- normal实际映像无s_test_stack、s_stress_buffer，未增加运行期任务或33KB静态占用。

四种profile均使用相同生产源码；只有测试宏不同。构建后用户项目逐字节恢复，证据 project_restoration.json。压力镜像SHA256见 profile_build_results.json；输出目录不提交Git。

## 本轮文件职责

- sd_block_probe.c/.h：复用原SdTest任务，新增只读request=5、编译开关、统计、停止和CRC/哨兵检查。
- stress_test.c：压力场景与故障注入；adapter_test.c只增加受宏控制的等待/延迟测试钩子。
- run_tests.py：保留原测试并增加压力场景；链接真实ARM运行库的64位除法，验证统计计算。
- run_policy_checks.py：保留写授权保护，追加压力互斥/时长/轮数保护。
- run_profiles.py：追加压力构建，保留原项目恢复机制，不复制可能过期的HEX。
- run_memory_audit.py：检查实际AXF符号与内存边界。
- README、各JSON及重编日志：运行说明与此次自动化证据；不改写历史硬件验收为自动化结果。

恢复用户本地配置后的CM7与CM4也已完整重编，均0 Error、0 Warning，见 build_results.json。源码指纹构建前后一致；Keil项目SHA256恢复为 e3790ac3b7932722120fa82cda24ebd433306d4b933d969fb629041aa8750767。用户原写测试开关和授权值保持不变；要运行压力验收，请显式切换为本报告的只读压力宏配置。

直接烧录配置已完成编译检查：CM7重新编译、CM4增量检查均0 Error、0 Warning（incremental_build_results.json）。正式CM7 AXF包含s_stress_buffer且不含写测试s_backup，确认当前输出为只读压力固件。

## 压力通过后的无SD测试对照（2026-11-08）

用户报告五分钟实板：elapsed_ms=300064、846轮、28154880B，I/O/CRC/边界错误均0，CRC一致、result=0，视频与OLED正常。Camera约30FPS、USB约16FPS；重新上电不启动压力仍约16FPS，尚不能据此确定USB限速根因。

按用户要求，当前h7_mac_CM7的APP_SDMMC_TEST_ENABLED、APP_SDMMC_DETECT_ONLY、APP_SD_BLOCK_TEST_ENABLED、APP_SD_BLOCK_WRITE_TEST_ENABLED、APP_SD_BLOCK_STRESS_TEST_ENABLED全部为0。测试代码保留但不创建SdTest任务，也不分配压力缓冲。视频配置保持不变，未调整Camera、USB、帧率或缓冲算法。打开原工程编译并Download，完整断电后使用相同播放器参数测量Camera/USB实际FPS，不再设置request=5。无SD测试固件的实板FPS尚待用户反馈。

无SD测试配置已编译通过：CM7重新编译与CM4增量检查均0 Error、0 Warning；正式AXF不含s_stress_buffer、s_test_stack、s_backup，确认未保留压力任务和测试缓冲。实际FPS仍待实板复测。

## C组压力缓冲迁移对照（2026-11-08）

用户反馈B组（仅任务）USB正常，C组（任务+压力代码/缓冲，未提交request）下降。此结果只将范围缩小到新增代码及布局，尚不能判定根因；未发起请求时没有SD IDMA读取、CPU复制或CRC工作。

本轮仅改变压力缓冲存放段，并增加对应链接映射：

| 对象 | 原位置 | 新位置 |
| --- | --- | --- |
| s_stress_buffer | DTCM 0x200026E8 | AXI 0x24010000，32B对齐 |
| before | 原结构头4B | 0x24010000，4B |
| data | 原结构头后4B | 0x24010004，33280B |
| after | 原结构尾4B | 0x24018204，4B |
| IDMA专用缓冲 | 0x24000000，32768B | 保持不变 |

RW_SD_STRESS_CPU独占0x24010000～0x2401FFFF的64KiB链接窗口，实际结构33288B、末端不包含为0x24018208，不与IDMA的0x24000000～0x24007FFF重叠。原32KiB IDMA MPU Region3不变；未扩大Non-cacheable区域，新CPU缓冲遵循原AXI默认内存属性。IDMA仍只访问原专用缓冲，CPU复制至新的压力缓冲，不需要为该CPU目的缓冲增加DMA Cache维护。

UNINIT不承诺初值；原压力入口已在每次运行前写入两个哨兵，并且只在完整读取成功后计算数据CRC，故无需新增初始化逻辑。未设置request时不读写该缓冲。空测试段使用有优先级的.ANY1命名匹配，关闭测试不会产生找不到段的链接警告，也不会将普通全局变量放入测试区。

C组宏保留：APP_SD_BLOCK_TEST_ENABLED=1、APP_SD_BLOCK_STRESS_TEST_ENABLED=1、APP_SD_BLOCK_WRITE_TEST_ENABLED=0、APP_SDMMC_TEST_ENABLED=0。任务优先级、20ms等待、读卡、CPU复制、CRC、Camera/USB/IPC和视频配置均不修改；未改MPU/Cache/BSP实现。

当前正式CM7重新编译/链接通过、CM4增量检查通过，均0 Error、0 Warning；SD实际BSP/HAL测试556/556 PASS（原断言保留）。`python Tests/SdmmcStage08B/run_memory_audit.py --current` 检查当前正式AXF，不需重新生成已删除的多profile镜像。

实板步骤：原h7_mac.uvprojx选择h7_mac_CM7，编译Download并完整断电重启；先不设置request，按原播放器参数观察一分钟Camera/USB FPS。只比较新的AXI C组与之前的DTCM C组结果。USB若恢复，则继续排查DTCM布局/潜在地址依赖；若未恢复，继续隔离新增代码布局。此实验同时改变链接布局和缓冲内存属性，单次恢复仍不能直接证明具体硬件机制。实际USB FPS待用户反馈，尚未确认根因，不合并main。
