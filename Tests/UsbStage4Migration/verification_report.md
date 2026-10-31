# USB Stage 4 正式main完整补丁迁移验收

**结果：204/204 PASS；EP0 IRQ 4/4 PASS；CM7、CM4完整重编均0 Error、0 Warning。仅软件验收，未进行实板USB验证。**

## 基线与交付范围

- 时间：2026-10-31（Asia/Shanghai）。
- 已执行 `git fetch origin main`，正式main与origin/main均为 `98a9f89637b39e3739414c0405ad5f508b0fbda1`。
- 新建detached隔离工作树：`C:/Users/29062/.codex/worktrees/usb-stage4-main/h7_mac`，HEAD保持上述SHA。
- 原验收分支：`0395fa8785830bf0d9571ea9da0cea61f91ad027`；共同祖先：`949180ce1dd48d208e9d29289ac37e7c07e556e2`。
- 完整累计补丁：`C:/Users/29062/.codex/worktrees/usb-stage4-main/usb_stage4_complete.patch`，相对于上述正式main，包含全部本轮生产修改、测试及报告，不仅是ep0_irq_fix.patch。
- 未提交、未推送、未合并；未切换或覆盖正式main。

## 依赖顺序及冲突处理

main本身已经包含USB Device Core解耦（7fed77d/ec22e00）和Stage2端点/生命周期整改（949180c）。这些能力完整保留在新工作树，不能把main已存在的代码重复当作新增补丁。

按以下顺序，对六个相关生产文件进行三方合并（共同基线、main当前版本、待迁移版本）：

|顺序|来源层|迁移内容|
|---|---|---|
|1|949180c → 0395fa8|Stage3 PIO、Abort、错误分域、配置声明检查、pUserData隔离、默认Class注册|
|2|0395fa8 → P0修复后快照|EP0管理边界、Core取消旧事务；对应363/364等测试完整迁移|
|3|P0快照 → 原19项整改后|新SETUP事务协调、重复EP0 IN不能清除busy；A至F六组夹具和断言修正|
|4|上述结果 → IRQ审查后|EP0旧XFRC/EPDISD竞态修复，Abort失败保留所有权|
|5|最终测试集合|迁移原204项全部测试及独立4项IRQ测试；适配Service路径后重新执行|

**发现一处结构差异，零文本合并冲突：** main的98a9f89已将usb_mode移到Service；旧验收分支仍在Platform。保留main的 `CM7/Service/usb_mode.c/.h` 和Keil Group/Include配置，将修复应用到Service文件；更新两个回归执行器及core_test引用。没有把旧目录或旧uvprojx覆盖回来。

逐层差异、来源和最终SHA见 `migration_manifest.json` 及四个分层patch。分层patch是规范化文本审查记录；可直接应用的交付物是工作树外的完整累计patch。

## 架构与资源边界

- Platform不依赖UVC/MSC具体Class；外部Class提供描述符和Class操作表，官方Core处理标准请求与EP0阶段。
- BSP负责PCD、硬件FIFO、中断及PIO暂存；Class选择仍位于Service。
- EP0不参加Class数据Endpoint匹配；仍经过Control/64字节及BSP能力验证；Class公开接口不能管理EP0生命周期。
- EP1至EP8继续精确匹配注册描述符的地址、类型与最大包长；363验证未声明端点不会调用底层Open。
- Abort失败不能释放旧用户缓冲区/PIO状态；364验证Close失败保留资源及重试。
- 新SETUP取消EP0时：只屏蔽对应方向 → Abort成功 → IN仅Flush TX0 → 定向W1C清除旧XFRC/EPDISD等 → Close成功清理PIO → Open恢复该方向中断 → 新传输。
- Abort失败保留原PIO/所有权并禁止新事务，等待Stop恢复。OUT取消不Flush共享RX；不清其他端点中断。取消过程中只执行一次必要Abort。
- 原19项失败涉及的204用例没有删除、跳过或降低正确的安全约束；正常控制事务、多包完成事件、初始化及描述符声明前置条件按实际架构修正。

## 实际测试与编译

环境：ARMCC 5.06 update7 build960；Unicorn Cortex-M；ST官方USB Device Core；模拟HAL端点操作。执行：

```powershell
$env:PYTHONIOENCODING='utf-8'
$env:PYTHONPATH='D:\32\STM32H7\h7_mac\Tests\OledAsyncAcceptance\python_deps'
python Tests/UsbCoreDecoupling/run_tests.py
python Tests/Ep0IrqRace/run_race.py
python Tests/UsbStage4Migration/audit.py
```

本机Python依赖通过PYTHONPATH引用，未复制进交付或累计diff；可在其他机器安装unicorn和pyelftools后使用。实际ARMCC命令与返回码分别在两个测试目录的commands.json。

|验收|执行|PASS|FAIL|跳过|
|---|---:|---:|---:|---:|
|原完整USB集合|204|204|0|0|
|新增EP0 IRQ集合|4|4|0|0|

原204项断言2984条，与上一轮已验收集合的用例ID逐项一致；测试执行期间生产源码未变化。

IRQ四项共110条C断言，另含Python事件顺序/寄存器模型检查：

|场景|结果|重点|
|---|---|---|
|A 旧IN XFRC已挂起，新SETUP到来|PASS|先清旧标志再Open/Transmit；旧事件不消费新PIO；新真实XFRC可推进事务|
|B 无旧XFRC，新SETUP到来|PASS|正常取消和新响应；旧EPDISD不再迟到Flush新FIFO|
|C Abort失败|PASS|旧PIO内容/active/busy保留、方向屏蔽；不启动新响应，不交付假完成|
|D STATUS OUT取消|PASS|仅清OUT0旧标志；不清共享RX；新响应及其真实完成正常|

四项均验证EP1真实完成仍交付、其他OUT接收保持、每次仅一次Abort；无迟到TX Flush、无共享RX Flush。

IRQ测试直接编译未改动的HAL_PCD_IRQHandler函数体及LL，C调用IRQ分发函数；端点Open/Close/Abort/Transmit仍为桩，W1C与寄存器副作用由模型驱动，使用310A Core ID分支。因此不能宣称已验证真实NVIC异常入口、芯片时序、PHY或Windows枚举。

完整构建使用Keil `-r`，不是增量编译；串行构建两个正式Target：

```text
D:/32/UV4/UV4.exe -r <隔离副本>/MDK-ARM/h7_mac.uvprojx -t h7_mac_CM7 -o <隔离副本>/Tests/UsbStage4Migration/build_h7_mac_CM7.log
D:/32/UV4/UV4.exe -r <隔离副本>/MDK-ARM/h7_mac.uvprojx -t h7_mac_CM4 -o <隔离副本>/Tests/UsbStage4Migration/build_h7_mac_CM4.log
```

|Target|Errors|Warnings|Code|RO-data|RW-data|ZI-data|
|---|---:|---:|---:|---:|---:|---:|
|CM7|0|0|59518|2522|332|16340|
|CM4|0|0|20334|914|636|2580|

## 累计文件清单与原因

仅六个生产文件发生修改；Keil工程不需要修改，因为main已有Service注册。以下列出全部补丁文件（不含构建产物、本机Python库）：

|状态|文件|原因|
|---|---|---|
|M|`CM7/BSP/bsp_usb.c`|PIO按包暂存和尾部补齐；多包续传；Abort/Close所有权；共享RX保护；EP0定向屏蔽与旧XFRC/EPDISD清除。|
|M|`CM7/BSP/bsp_usb.h`|导出Abort与PIO/端点通用接口，保留硬件能力约束。|
|M|`CM7/Platform/usb_device.c`|严格配置/Endpoint声明检查；Core管理EP0；新SETUP取消旧事务；数据端点所有权；Class私有数据隔离；设备错误与操作错误分域。|
|M|`CM7/Platform/usb_device.h`|补齐通用Abort、最近操作结果等公开声明和语义说明。|
|M|`CM7/Service/usb_mode.c`|集中选择默认TEST；不覆盖显式Class；仅设备级故障触发恢复判断；保留main的Service目录。|
|M|`CM7/Service/usb_test_device.c`|移除Class自行SERVICE自动注册，由Mode Manager统一选择；TEST描述符和Class留在Service。|
|A|`Tests/Ep0IrqRace/commands.json`|四项IRQ测试实际编译命令。|
|A|`Tests/Ep0IrqRace/race_test.c`|新增A/B/C/D四项真实HAL IRQ调度回归，覆盖新真实完成、Abort失败所有权及其他端点保护。|
|A|`Tests/Ep0IrqRace/results.json`|四项IRQ测试逐项事件顺序、观察值、断言结果。|
|A|`Tests/Ep0IrqRace/run_race.py`|编译未改动HAL IRQ/LL并运行W1C寄存器模型，适配Service路径。|
|M|`Tests/UsbCoreDecoupling/commands.json`|本轮实际ARMCC5编译/链接命令、输出与退出码。|
|M|`Tests/UsbCoreDecoupling/core_test.c`|迁移完整204项驱动及正常/取消控制事务夹具；适配Service路径和默认Class初始化前置条件。|
|M|`Tests/UsbCoreDecoupling/endpoint_lifecycle_test.c`|保留363/364实际测试体、365至371 EP0边界等断言，迁移全部19项相关测试修正。|
|M|`Tests/UsbCoreDecoupling/legacy_test.c`|适配真实Class注册/控制事务及端点声明前置条件。|
|M|`Tests/UsbCoreDecoupling/results.json`|本轮204项逐项输入、预期、实际、PASS/FAIL与源文件哈希。|
|M|`Tests/UsbCoreDecoupling/results.md`|204项可阅读结果清单。|
|M|`Tests/UsbCoreDecoupling/run_tests.py`|保留204项执行集合与编号完整性检查；Service引用路径和include path适配。|
|A|`Tests/UsbCoreDecoupling/stage3_acceptance.md`|保留Stage3设计及最初未验收说明，属于历史文档；本轮结果以本报告为准。|
|A|`Tests/UsbStage4Migration/audit.json`|迁移过程、分层差异、SHA审计、构建或回归证据；不加入Keil固件编译。|
|A|`Tests/UsbStage4Migration/audit.py`|可重复执行范围、受保护SHA、正式工作区保护、204/4结果及构建日志审计。|
|A|`Tests/UsbStage4Migration/build_h7_mac_CM4.log`|迁移过程、分层差异、SHA审计、构建或回归证据；不加入Keil固件编译。|
|A|`Tests/UsbStage4Migration/build_h7_mac_CM7.log`|迁移过程、分层差异、SHA审计、构建或回归证据；不加入Keil固件编译。|
|A|`Tests/UsbStage4Migration/ep0_P0_and_missing_tests.patch`|单层规范化文本差异，用于依赖审查；实际应用请使用工作树外的完整累计补丁。|
|A|`Tests/UsbStage4Migration/ep0_old_XFRC.patch`|单层规范化文本差异，用于依赖审查；实际应用请使用工作树外的完整累计补丁。|
|A|`Tests/UsbStage4Migration/file_manifest.json`|完整迁移文件清单与逐文件原因。|
|A|`Tests/UsbStage4Migration/formal_before.json`|迁移过程、分层差异、SHA审计、构建或回归证据；不加入Keil固件编译。|
|A|`Tests/UsbStage4Migration/make_report.py`|迁移过程、分层差异、SHA审计、构建或回归证据；不加入Keil固件编译。|
|A|`Tests/UsbStage4Migration/migrate.py`|本机本轮迁移记录脚本：从公共祖先分四层三方合并；依赖旧隔离副本，不应在已迁移工作树再次执行。|
|A|`Tests/UsbStage4Migration/migration_manifest.json`|迁移过程、分层差异、SHA审计、构建或回归证据；不加入Keil固件编译。|
|A|`Tests/UsbStage4Migration/protected_sha256.json`|迁移过程、分层差异、SHA审计、构建或回归证据；不加入Keil固件编译。|
|A|`Tests/UsbStage4Migration/regression_204.log`|迁移过程、分层差异、SHA审计、构建或回归证据；不加入Keil固件编译。|
|A|`Tests/UsbStage4Migration/regression_irq.log`|迁移过程、分层差异、SHA审计、构建或回归证据；不加入Keil固件编译。|
|A|`Tests/UsbStage4Migration/remaining19.patch`|单层规范化文本差异，用于依赖审查；实际应用请使用工作树外的完整累计补丁。|
|A|`Tests/UsbStage4Migration/stage3_architecture.patch`|单层规范化文本差异，用于依赖审查；实际应用请使用工作树外的完整累计补丁。|
|A|`Tests/UsbStage4Migration/verification_report.md`|本轮迁移基线、文件原因、验收、冲突及限制。|
|A|`Tests/UsbStage4Migration/whitespace_check.log`|迁移过程、分层差异、SHA审计、构建或回归证据；不加入Keil固件编译。|

## SHA与正式main保护

- 1593个受保护非测试文件与main基线blob逐字节一致，明细见protected_sha256.json。
- 其中STM32 HAL Driver目录262个文件、官方USB Core 9个文件SHA一致；CMSIS/LL、UVC Class/Descriptor、CM4、Camera/Media/SDRAM/DCMI等均未修改。
- 六个迁移生产文件与原已验收实现规范化换行后完全一致；保留main的CRLF换行，改动文件为UTF-8，差异已由重新编译验证。
- 正式工作区HEAD、Git index、所有已跟踪文件内容与删除状态均与迁移前快照一致。
- 正式用户的BSP空行、uvoptx/uvprojx修改、framework目录删除均未覆盖或带入本隔离副本；本副本来自准确提交基线。
- `git diff --check`按CR-at-EOL规则执行通过；累计补丁另行通过临时index的cached apply检查，校验结果保存在工作树外的usb_stage4_patch_check.json。

## 尚待验证及审核

软件测试、完整编译与迁移审计全部通过，未发现未解决的软件回归失败。仍需在STM32H747实板验证枚举、新SETUP与旧XFRC硬件时序、Abort失败恢复、并发OUT和PIO/FIFO行为。本轮不宣称实板验收、不实现UVC协议。

完整累计patch相对98a9f89可独立应用，包含本轮全量改动和所有新文件；不需要先应用ep0_irq_fix.patch，也不要叠加重复应用旧补丁。交付后等待人工审核，不提交、不推送、不合并。
