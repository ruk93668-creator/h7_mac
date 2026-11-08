> 后续维护：用户已批准删除不兼容测试，见 Tests/TestMaintenance/incompatible_tests_removed.json。本文原回归数量是提交13364dc时的历史结果；当前集合另见active_regression_results.json，删除项不计为PASS。

# 阶段08B 验收报告

结论：08B专项软件检查通过；历史专项没有新增退化，但全仓历史回归并非全绿。40MHz实板只读、真实写入及并行性能尚未验证。

## 结果

| 检查 | 结果 |
|---|---|
| 真实BSP/HAL IDMA、IRQ、Abort及适配、08A原断言 | 520/520 PASS |
| 写入编译保护 | 7/7 PASS |
| IPC/OLED历史执行 | 1453/1453 PASS |
| 全部已运行历史断言 | 1900 PASS / 258 FAIL，合计2158次 |
| 未能编译/链接的历史专项 | 6组，未执行断言 |
| 失败/不兼容专项对照d0305de | 13组实际复跑，统计与失败集合一致 |
| Camera/USB/IPC/CM4/HAL等受保护源码 | 1208个C/H SHA一致，未改动 |
| AXI链接布局 | 三种配置均为0x24000000、32768B、UNINIT |

## 构建

编译器：ARMCC5 V5.06 update7 build960，Keil UV4。使用同一正式uvprojx，未新建临时工程。

| 构建 | Error | Warning | 结果 |
|---|---:|---:|---|
| CM7 normal | 0 | 0 | PASS |
| CM7 read_only | 0 | 0 | PASS |
| CM7 write_compile_only | 0 | 0 | PASS |
| 当前本机 h7_mac_CM7完整重编 | 0 | 0 | PASS |
| 当前本机 h7_mac_CM4完整重编 | 0 | 0 | PASS |

三种profile只临时切换测试宏，最后恢复用户原工程字节；最终再次完整构建用户本机CM7/CM4配置。用户原08A测试宏保留在本地，未纳入提交。提交的uvprojx只含08B新增文件条目，测试开关仍由头文件默认关闭。

## 关键实现

- 400kHz/1-bit识卡，低速协商4-bit与High Speed后40MHz；普通卡24MHz回退。未修改PLL。
- 独占32KiB AXI中转区，Region3 Non-cacheable，CPU缓冲区可以是合法DTCM，不直接交给IDMA。
- 单批64扇区，统一截止时间，任务阻塞等待；实际HAL的同步命令/Abort仍可能增加返回时间。
- SDMMC IRQ只锁存终止事件并唤醒，任务侧运行HAL终止处理，错误优先于DATAEND和完成回调。
- 完成必须同时满足硬件静止、DCOUNT=0、HAL READY、卡TRANSFER；失败不交付本批读取数据。
- Abort失败保留所有权；显式Recover复位并验证硬件，撤销旧卡会话后重新Init。
- 默认不开启实板探针。可选测试任务只接受Watch手工命令，不改变既有业务任务及BOOT计数。
- 写测试独立开关、CID/容量/原始区域授权、LBA0禁止、备份/逐字节回读/恢复验证。合成CID的编译镜像不能作为真实卡授权。

## 原失败说明

没有修改历史断言、相关业务模块或HAL库。258个历史失败及6组旧夹具编译/链接问题，在d0305de源码快照同样复现；不是本轮新增退化，也不把它们计为PASS。详细逐例结果保留在各组JSON和日志中。

## 文件、调用链、Watch及操作步骤

详见 [SDMMC1_Stage08B设计与实板说明](../../Docs/Design/SDMMC1_Stage08B.md)。完整累计差异由本分支相对d0305de的Git diff查看；目录内production.diff保留生产文件审阅差异。

主要Watch：g_sd_block_test.state、result、card、cid、init.sd_clock_hz、init.bus_width、io、read_crc、repeat_crc、restore_required、restore_result、stack_free_words。
只读验收镜像：output/SdmmcStage08B/firmware/read_only/h7_mac_CM7.axf；烧录后手工request=1。故障后request=4恢复并重新识卡。真实写测试未经用户指定原始区域和CID，不得执行。

## 剩余项目

- 40MHz/4-bit信号与真实卡读写稳定性。
- 实际吞吐、最坏写停顿、Camera/USB并行FPS、IPC延迟和实板任务栈余量。
- 当前Unicorn不模拟真实M7 Cache行驱逐/AXI仲裁，软件验证不能替代实板Cache与DMA一致性验收。
- 原历史测试需另立任务维护；本轮不修复其配置夹具或无关业务。
- 不支持并发热换卡、异步任务删除或跨任务强制取消；卡变更后需受控恢复和重新识卡。

Git：仅独立分支提交与推送，不合并main。实板结果必须由用户烧录后补充。
