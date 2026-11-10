# B3 Storage 业务模块实施及软件验收报告

## 1. Git 与范围

- 基线、本地 main、远端 main 核对：`d725a698203b3bcde479a290714a75b16bc9de16`。
- 开发分支：`codex/stage-B3-storage-module`；禁止合并 main。
- 开始时没有已跟踪文件修改，存在未跟踪 output/，全部保留。
- 只修改 app_storage.c、app_storage.h、Tasks/storage_task.c 三个生产文件。新增专项、结果、日志和本文；原测试断言未修改。
- Camera、USB、Bridge、Dispatcher、Media/FIFO、JPEG/AVI、StorageFS/DiskIO/SD_Block/BSP、Control/App_Messages、CM4、Keil 配置均保持基线内容。

## 2. 修改及完整边界

| 文件 | 修改原因 |
|---|---|
| CM7/Application/app_storage.c | 增加只读准备状态入口；以私有8B业务数据保存累计帧数和当前文件编号；原位置投影诊断 |
| CM7/Application/app_storage.h | 声明 App_StorageIsReady，说明不依赖诊断 ready |
| CM7/Application/Tasks/storage_task.c | 原准备门控改读 App_StorageIsReady，其他字节不变 |
| Tests/StorageModuleB3/ | 新旧 ARMCC5 轨迹、权威性测试、回归驱动、编译日志、范围审计、设计与验收记录 |

完整累计 Diff 由 Git 基线到本分支提供，不把一次性补丁、AXF、HEX、MAP 或旧 SHA 清单加入仓库。

没有把原来已经在 App_Storage 的业务再搬一遍；没有建立重复的状态机、计数权威、文件对象或租约账本。新增私有计数与原 Debug 是“业务来源＋诊断投影”的关系。

## 3. 顺序与同步

App_StorageProcess 的命令、帧处理、JPEG/CRC、文件写入、待归还、收尾、超时、FPS、缓存发布、自通知顺序完全保留。

- 每轮最多一条内部命令和一张帧；四个请求接口、四项队列行为不变。
- STOP 成功入队才关闭新帧入口；队列满不伪装成功。
- CameraTask 通过原 Bridge 入口执行 App_StorageProcessReturns；Storage 只标记 RETURN_PENDING。
- 临界区、PRIMASK 嵌套恢复及内存屏障序列完全一致。
- 正常 VIDEO STOP 仍为 AVI Finish → Close → Unmount。
- IDLE 仍可与待归还租约并存；不把文件关闭成功解释为租约已归还。
- 不新增 ACK/FAULT 协议、FinishRound、Observer 或恢复政策。

范围审计将批准的替换逐项反向还原后，与原 app_storage.c 逐字节相等；Task 反向替换门控后也与基线逐字节相等。诊断结构布局不变。

## 4. B3 专项实际结果

**51/51 PASS，0 FAIL。**

- 47项基线/候选正常条件与故障条件对比，完整轨迹一致。
- 4项权威性验证：准备 Getter 无副作用、诊断累计帧数不影响 FPS、关闭后篡改诊断文件编号不影响 Snapshot 回读、真实 Task 不因诊断 ready 被改写而停止推进。
- FPS、文件编号及 Task 门控的篡改现场同时运行旧实现，旧实现表现出诊断依赖，候选实现消除该依赖。这是本轮明确批准的变化，不要求与旧实现保持一致。
- 覆盖正常录像/抓拍、双段 JPEG、校验/回读损坏、短写、挂载及写入失败、Finish/Close/Unmount/Seek失败、命令FIFO及队列满、重复请求、无帧超时、旧session/generation、归还失败与重试、错误任务归还、缓存发布、Tick回绕、BOOT与栈扫描。
- 执行真实 app_storage.c、storage_task.c、jpeg_frame.c、mjpeg_avi.c。StorageFS、Media边界、RTOS由桩提供，不能称为真实SD卡或IDMA实板验证。

逐项清单见 results.json；完整逐调用轨迹位于本地 output/StorageModuleB3/*_trace.json。

## 5. 原回归对比

27组均在原基线与候选快照执行。8组可以完整通过，共637项PASS：B2Behavior 102、B2Integration 19、B1Behavior 39、B1BridgeStorage 11、B1Media 6、EP0IRQ 4、Strings 8、IpcStatus 448。

**B3专项51＋上述有效回归637＝688项PASS；不能据此宣称全仓全绿。**

另外19组在基线和候选均失败或不能编译/链接，逐项结果一致，新增退化为0。下表直接保留实际执行结果；不同历史框架的unique_cases、executions、errors不混算成一个总数。

| 组 | 候选实际结果 | 与基线逐项比较 |
|---|---|---|
| B2Behavior | {'total': 102, 'passed': 102, 'failed': 0} | 一致 |
| B2Integration | {'total': 19, 'passed': 19, 'failed': 0} | 一致 |
| B1Behavior | {'total': 39, 'passed': 39, 'failed': 0} | 一致 |
| B1BridgeStorage | {'total': 11, 'passed': 11, 'failed': 0} | 一致 |
| B1Media | {'total': 6, 'passed': 6, 'failed': 0} | 一致 |
| CameraApp | 未生成运行结果：编译/链接失败 | 一致 |
| CameraContext | 未生成运行结果：编译/链接失败 | 一致 |
| CameraStream | 未生成运行结果：编译/链接失败 | 一致 |
| CameraDeviceLifecycle | 未生成运行结果：编译/链接失败 | 一致 |
| JpegMedia | 未生成运行结果：编译/链接失败 | 一致 |
| JpegDevice | 未生成运行结果：编译/链接失败 | 一致 |
| ContinuousMedia | 未生成运行结果：编译/链接失败 | 一致 |
| Bridge | 未生成运行结果：编译/链接失败 | 一致 |
| NativeIntegrated | 未生成运行结果：编译/链接失败 | 一致 |
| NativeTest | 未生成运行结果：编译/链接失败 | 一致 |
| Core | {'total': 204, 'passed': 201, 'failed': 3, 'assertions': 2947} | 一致 |
| EP0IRQ | {'total': 4, 'passed': 4, 'failed': 0} | 一致 |
| Probe | {'total': 89, 'passed': 0, 'failed': 89} | 一致 |
| Cancellation | {'total': 39, 'passed': 0, 'failed': 39} | 一致 |
| Payload | {'unique_cases': 62, 'executions': 124, 'passed': 122, 'failed': 2} | 一致 |
| UvcLifecycle | {'total': 40, 'passed': 34, 'failed': 6} | 一致 |
| EP1 | {'total': 45, 'passed': 0, 'failed': 45} | 一致 |
| Streaming | {'total': 36, 'passed': 1, 'failed': 35} | 一致 |
| Descriptors | {'total': 8, 'passed': 7, 'failures': 0, 'errors': 1} | 一致 |
| Strings | {'total': 8, 'passed': 8, 'failed': 0} | 一致 |
| VideoConfig | 未生成运行结果：编译/链接失败 | 一致 |
| IpcStatus | {'total': 448, 'passed': 448, 'failed': 0} | 一致 |

历史编译/链接失败共11组：CameraApp、CameraContext、CameraStream、CameraDeviceLifecycle、JpegMedia、JpegDevice、ContinuousMedia、Bridge、NativeIntegrated、NativeTest、VideoConfig。包括旧夹具缺少新依赖、引用已迁移私有变量、未链接FIFO/同步/Storage接口等。未修改历史断言或恢复被清理的历史测试入口。

其余8个失败组保留原失败/错误。regression_results.json记录第一诊断与逐项结果。本轮没有重新引入已移除的SD压力任务或验收固件。

SDMMC行为保障包括正式全量编译、禁止修改路径审计和现行集成回归。当前仓库没有可执行的原08B块设备专项源文件，不能虚报重新运行556项，也不能将StorageFS桩解释为真实IDMA错误注入。

## 6. 正式完整构建

| Target | Error | Warning | 程序大小 |
|---|---:|---:|---|
| h7_mac_CM7 | 0 | 0 | Code 136662，RO 3046，RW 1328，ZI 99144 B |
| h7_mac_CM4 | 0 | 0 | Code 26302，RO 1438，RW 936，ZI 3576 B |

使用未改动的正式 MDK-ARM/h7_mac.uvprojx 全量重编，编译前备份原输出，未改目录、文件名或Target。日志为本目录 h7_mac_CM7_rebuild.log、h7_mac_CM4_rebuild.log。

- 两核AXF、MAP存在；CM4 HEX存在。
- 本轮CM7正式构建没有生成HEX，未为此更改用户原配置，也不把旧HEX当新固件。
- CM7 MAP确认 App_StorageIsReady 被 StorageTask 引用；s_business 为8B私有对象。
- 固件、MAP及此前输出备份只保留本地output/与原工程输出位置，不提交。

## 7. 保留问题与限制

1. Control停止Camera/USB仍未通知Storage。Storage可因5秒无帧进入FAULT；异常Close路径跳过AVI Finish。本轮按约束不修复。
2. Storage IDLE不是全部租约归还证明，不能据此重建Media或强制释放帧。
3. result仍有原来的Task等待错误、BUSY、恢复结果等覆盖语义；不增加首故障锁存。
4. 损坏队列凭据等异常路径保持基线，不建立新的强制回收策略。
5. 不优化录像帧率，不修复或断言DCMI FE根因，不新增SD所有权机制。
6. 软件轨迹不覆盖真实总线仲裁、Flash执行时序、IDMA及卡片延迟。

## 8. 实板验证清单（开发阶段未执行，后续用户反馈见第9节）

使用本分支CM7 AXF及配套CM4固件；不要加载此前验收镜像。正式固件无新增测试入口或自动写卡行为，保存仍通过现有公共请求接口。

- [ ] 冷启动Camera/USB/IPC/OLED正常，视频格式、目标帧率保持用户现有配置。
- [ ] USB-only运行；观察实际USB/Camera FPS、丢帧及心跳，与基线相同条件比较。
- [ ] 在已授权FAT卷上手动抓拍，确认JPG可打开，保存计数、回读CRC和实际文件编号一致。
- [ ] 手动录像后明确调用Storage STOP，确认AVI封尾、关闭，电脑可播放。
- [ ] USB＋SD并行；观察IPC、栈余量、实际FPS和租约最终归还。
- [ ] 重复START/STOP及无卡/掉卡时，检查原错误与恢复语义；异常文件不标记为正常封尾。
- [ ] 文件业务IDLE后继续确认pending_returns最终为0、return_result成功；不能只看IDLE。
- [ ] 不用高频断点或频繁栈扫描测视频性能；本轮仍保留每1000ms栈扫描。

不要仅为证明诊断无业务权威而在正常实板运行中修改文件系统、租约或硬件寄存器；诊断篡改已由独立软件测试完成。

## 9. 交付状态

软件专项、有效回归、范围审计和两核完整编译已完成，生产成果提交为 `34ad0562a2ad844ca1b7e4caf2ef080088f19713`。

用户在获得当前固件的手动录像与停止步骤后，反馈“功能正常”，随后明确授权“合并”。据此记录：B3录像功能经用户实板反馈正常，已获准合并main；不将该反馈扩展为全部异常场景、长期稳定性或性能测试通过。第8节未逐项提供结果的项目继续保留待验证。

合并基线为 `d725a698203b3bcde479a290714a75b16bc9de16`，采用快进方式纳入生产成果与本次验收状态记录，不增加生产修改，不改写历史、不强制推送。实际完成状态与最终提交号由main及origin/main引用核对，输出与用户本地文件保留。
