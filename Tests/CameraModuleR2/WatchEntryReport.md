# R2 Keil Watch独立验收入口交付报告

> 2026-11-14 清理状态：用户报告本轮Watch正常操作通过后，已要求移除验收代码、Control调用点、生成脚本及临时工程/镜像。本文作为历史验收文档保留；下文Watch工程路径和脚本命令属于`0d88b1a`历史版本，当前不再存在，不能直接照旧执行。正式固件不包含`g_r2_hw`。
> 实板反馈仅记录用户确认的手动START/STOP、录像/抓拍/存储停止与恢复正常；没有新增故障注入现场、H/M/B/D逐项数据、长时间运行或栈余量测量。不能据此宣布全部R2异常路径通过。


日期：2026-11-14。基线：`fix/lifecycle-init-basepri@1c97d7cca0c19927f4405d2abd34e5cc918fcd51`。
分支：`test/r2-keil-watch-control`。包含已实板确认的BASEPRI修复，但不改变其分支和PR #7，不合并main。

## 1. 实现与修改范围

唯一生产文件差异是 `CM7/Application/Tasks/control_task.c`：默认APP_R2_HW_ACCEPTANCE_ENABLED=0；仅在验收构建包含HardwareControl.h，并在原ControlTask_Process和状态发布之后调用一次邮箱适配器。

`audit_watch_scope.py`删除这两个测试条件块后，与基线ControlTask文件正文逐字一致。原BOOT、100ms等待、任务优先级2、768Word栈、消息处理上限、ACK/FAULT、业务状态机、状态发布均未调整。宏引入可能移动断言源码行号，不作为业务行为变化。

新增文件都在 `Tests/CameraModuleR2/`：

| 文件 | 作用 |
| --- | --- |
| HardwareControl.c/.h | 24B Watch邮箱、私有序号和一次性投递，六个公共请求接口 |
| prepare_watch_project.py | 从原工程生成唯一隔离验收工程，不写原工程配置 |
| run_watch_build.py | 正式CM7/CM4完整编译、独立Watch完整编译和Map隔离检查 |
| hardware_control_test.c | 30个输入、去重、并发读取、权限、失败边界场景 |
| control_integration_test.c | 实际Control决策、实际FreeRTOS队列、原START/STOP轨迹4场景 |
| run_watch_tests.py | ARMCC5 O0/O2构建执行专项 |
| run_watch_protocol.py | 隔离副本执行原C1/C1.1/C2.1.1 |
| audit_watch_scope.py | 正式代码条件关闭等价性及受保护文件审计 |
| KeilWatchUsage.md | 独立镜像烧录与Watch六种操作说明 |
| 本报告、JSON和log | 实际软件结果与原始构建日志 |

HardwareAcceptance.md只更新独立入口可用性和镜像说明，不把未执行项目改成通过。

## 2. 邮箱契约

用户只写command和submit_seq，后者最后提交，每次严格递增1。ControlTask合法上下文才消费，私有s_processed_sequence去重；禁止通过写诊断输出重新执行旧请求。输入读取不稳定则等待下一轮，未知操作码/跳号均不执行业务。

先冻结输入和绑定序号，再调用现有公共请求接口，最后发布processed_command/result/processed_seq。队列满或原API失败也只投递一次，不自动重发；用户用新序号显式重试。同一序号或旧序号不再执行，编号不回绕。

command=1/2调用原ControlTask_RequestStart/Stop；3/4/5/6分别调用原Storage录像/抓拍/停止/恢复接口。没有直接执行FatFS、Media Release、Device Stop或BSP/HAL操作，没有新任务、队列、Timer和动态内存。

processed_seq代表一次输入处理及投递尝试结束，dispatch_result=0只证明请求入队，不是业务完成ACK。ready=1不代表Camera已运行。整个邮箱不承担生命周期Replay、消费者资格或帧租约功能。

## 3. 测试结果

| 检查 | 实际结果 |
| --- | --- |
| 邮箱30场景×O0/O2 | 60/60 PASS |
| 真实Control与FreeRTOS队列4场景×O0/O2 | 8/8 PASS |
| 原C1 | 419/419 PASS |
| 原C1.1新增 | 24/24 PASS |
| 原C2.1.1 | 648/648 PASS，40960次事务压力保留 |
| 原ABI探针/回归判定器 | 通过 / 29/29 PASS |
| 正式CM7完整重编 | 0 Error、0 Warning |
| 正式CM4完整重编 | 0 Error、0 Warning |
| 独立Watch CM7完整重编 | 0 Error、0 Warning |

专项执行真实HardwareControl.c；集成测试执行真实ControlTask_Process、原Control公共入口、app_messages.c及FreeRTOS queue/list；上下文和硬件/Storage公共请求边界为桩。任务阻塞/创建出口若被意外调用会使测试失败。本组不运行完整调度器或真实FatFS，不能当实板验收。

第一次生成工程缺少Tasks全局头文件路径，编译失败后仅修正生成脚本，再单独完整重编Watch target通过。没有通过改正式IncludePath或业务头文件规避问题。

没有修改原测试断言或Replay实现。未重跑本轮之外的全仓历史回归；旧R2报告中两项B1精确轨迹差异仍保持待审核，不在本轮改写期望。

## 4. 正式与验收镜像隔离

- 原MDK-ARM/h7_mac.uvprojx文件哈希不变，没有增加Target、测试宏、源文件或调整输出设置。首次生成仅复制原CM7调试选项到隔离uvoptx，清理副本旧断点/Watch/初始化脚本；原本机设置哈希核对不变，已有验收设置不覆盖。
- 正式CM7、CM4 Map均没有g_r2_hw；正式CM7也不链接HardwareControl.o。
- Watch Map确认ControlTask_Run引用R2_HardwareControlProcess，包含g_r2_hw，地址0x2000FBE0、大小24B，位于当前DTCM静态区域，不占用SDRAM或SD IDMA缓冲。
- 验收源码被错误加入未开启宏的构建时会#error，避免静默开放入口。
- 正式CM7 RW+ZI=1340+100932=102272B；Watch=1348+100948=102296B，实际链接净增24B（含布局对齐差异）。邮箱24B，另有私有序号4B及重入标记1B，不能把净增数简单当成各对象直接相加。
- Control任务栈仍3072B，优先级2。静态已知调用深度正式208B、Watch224B；适配器本地16B、已知深度216B。静态调用图不能代替实板栈余量。
- Watch生成工程使用绝对源路径，可能改变断言文件名字符串的链接布局；不能用Code总量简单比较运行性能。

原输出在构建前备份到output/CameraModuleR2/watch_build/previous_output；没有删除其他output。新工程和产物只位于该隔离目录，不提交生成工程、AXF、对象文件或用户Keil设置。

## 5. 烧录与实板检查

按 [KeilWatchUsage.md](KeilWatchUsage.md) 操作。应烧录独立Watch AXF，正式AXF没有邮箱；配套CM4使用原正式AXF。具体路径、哈希、命令、编译结果见watch_build_results.json。

初次测试建议先无Storage：系统STOP(command2/seq1) → 核验USB归还、Media、Device、Bridge、匹配ACK → START(command1/seq2) → 重新打开播放器。

录像/抓拍只在JPEG采集正常、USB已连接且卷已确认可写时操作。先显式Storage STOP并等待文件/租约完成，再系统STOP。邮箱不会自动补齐旧全局STOP的Storage封尾问题。恢复Storage也不等于原文件保存成功。

下面项目全部待实板：六种操作投递、真实START/STOP与ACK、真实AVI/JPG文件、失败/队列满、长期运行与栈余量。当前仅完成软件实现、软件专项和实际编译。

## 6. 复现命令

```powershell
python Tests/CameraModuleR2/run_watch_tests.py
python Tests/CameraModuleR2/run_watch_protocol.py
python Tests/CameraModuleR2/run_watch_build.py
python Tests/CameraModuleR2/audit_watch_scope.py
```

只有独立工程配置/验收源变化、正式两核构建已通过且工程哈希一致时，才可使用 `run_watch_build.py --watch-only` 仅重编独立Target。完整命令默认重编三项，不把旧报告当作新构建。

## 2026-11-14 本轮验收入口清理

用户确认只清理本轮Watch验收代码、调用点、临时工程和镜像，保留历史回归测试、验收文档及正式功能。

实际移除：HardwareControl.c/.h、hardware_control_test.c、control_integration_test.c、prepare_watch_project.py、run_watch_build.py、run_watch_protocol.py、run_watch_tests.py、audit_watch_scope.py及本目录__pycache__。ControlTask测试宏、头文件包含和条件调用点全部移除，文件与已通过BASEPRI修复的1c97d7c版本逐字一致。

本轮生成目录output/CameraModuleR2整体移除，删除前核对仓库内绝对路径和无重解析点；其余历史output和回归测试不清理。原正式Keil工程、BASEPRI修复、Camera/USB/Storage业务实现保持不变。

文档与既有软件结果保留作为历史记录；其中测试脚本和临时镜像路径已经失效。正常手动操作的实板结果按用户反馈记录，未新增故障注入或运行性能数据。

清理后直接完整重编正式CM7/CM4，不再生成独立验收工程。当前结果见cleanup_build_results.json及h7_mac_CM7_cleanup_rebuild.log、h7_mac_CM4_cleanup_rebuild.log；本轮不重新执行或新增已删除的Watch测试代码。

磁盘删除不会改变板上已烧录内容。后续应关闭旧验收工程，打开原MDK-ARM/h7_mac.uvprojx，选择h7_mac_CM7并Download正式镜像。正式符号中不再存在g_r2_hw是预期结果。
