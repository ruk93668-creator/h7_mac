# B1 Camera业务状态迁移验收报告

基线：e44f549ee39a18f51fa2473d5ef4b10ce3b5753e。
分支：codex/stage-B1-camera-module。

本轮仅迁移状态所有权，不实施新的业务策略。新增软件专项通过；历史回归仍有失败及不可运行夹具，不宣称全仓测试全绿。实板尚未验收。

## 修改范围

仅修改三个生产文件：

- CM7/Application/Tasks/camera_task.c：保留BOOT、等待、队列收发、Bridge每轮推进、诊断投影、App_Status发布和栈统计。
- CM7/Application/app_camera.c：集中唯一业务上下文，承接命令、待发回复、Media推进、停止判断、Device调用和FPS统计。
- CM7/Application/app_camera.h：增加普通C接口、只读状态副本和诊断观察者契约。

没有修改Bridge、Media、Device、USB、Storage、Control、CM4、消息/事件协议、Keil工程、HAL、DMA或SDMMC。新测试均在本目录，历史测试断言和记录原样保留。

## 调用顺序

~~~text
CameraTask：所有者及BOOT检查 → wake_count
→ 尝试ACK，独立尝试FAULT
→ CanAcceptCommand → 最多接收一条 → HandleCommand
→ App_CameraProcess（原App_CameraRun及Media错误识别）
→ App_UvcBridgeCameraProcess(CanOfferFrame)
→ App_CameraFinishRound（停止完成判断、原统计及取时）
→ 再次尝试ACK，独立尝试FAULT
→ CameraTask_PublishStatus（SampleStatus后GetStatus）
→ 按原1000ms周期扫描栈余量
~~~

## 接口契约

| 接口 | 契约 |
|---|---|
| CanAcceptCommand | !ack_pending && !stop_command；判断后才允许队列出队 |
| HandleCommand | 复制原命令为ACK；START保留BUSY及失败清理；STOP只登记停止意图；未知命令保留INVALID_ARG ACK |
| Process | 只推进已prepared的Media，按原条件记录FAULT及进入stopping |
| CanOfferFrame | running && !stopping，不申请或释放帧 |
| FinishRound | Stop成功后才检查Bridge Idle；保留Stop失败ACK及后台重试；随后更新统计 |
| GetPendingAck / GetPendingFault | 分别读取副本，不消耗记录；ACK发送失败不能阻断FAULT尝试 |
| ConfirmAckEnqueued / ConfirmFaultEnqueued | 仅实际Send返回SYS_OK时调用；表示入队，不表示Control已消费 |
| SampleStatus | 只在原发布位置采样最近实际运行配置 |
| GetStatus / GetLastResult | 只读，无配置采样、硬件推进或业务状态变化 |
| SetDiagnosticObserver | 任务循环前绑定；回调只更新对应Debug字段，不反向驱动业务 |

运行期接口由CameraTask串行调用，不新增跨任务直调。Configure及原单帧/Stream API保留原调用语义。其他任务继续通过App_Status读取跨任务副本。

START仍先建立ACK与命令诊断，再执行Bridge Begin、Camera Stream启动；启动失败可能消耗epoch，保持现状。
STOP仍等待Media安全停止及Bridge归还；非BUSY失败回复ACK后仍继续后台停止。
FAULT成功入队后仍按原规则保持fault_sent，成功START才重置，不增加新的重复故障通知策略。
Confirm接口只紧接成功入队调用，不跨轮保存待确认临时消息。

## 唯一上下文与诊断

上下文包含：

| 字段组 | 字段 |
|---|---|
| 配置与设备 | config、config_set、configured、initialized、device |
| 业务运行 | running、prepared、stopping |
| 命令与回复 | ack、ack_pending、stop_command、command_id |
| 错误 | fault、fault_sent、last_result |
| 统计 | fps_tick、fps_frames、captured_frames、dropped_frames、measured_fps_milli |
| 实际配置采样 | applied、applied_valid |
| 诊断投影入口 | observer |

prepared与initialized保留原有不同含义，不合并。状态0/1/2按stopping优先、running其次推导，不新增独立业务枚举状态。
Task仅保留TCB、1536-word且8字节对齐的栈、任务句柄与原Debug。优先级3、20ms等待、BOOT前无限等待、1000ms栈扫描均保持。

Observer在原赋值位置投影command_id、result、state、计数和FPS。Debug业务字段不参与决策；BOOT和运行统计仍由Task维护。
Task等待错误仍写Debug.result，普通Getter不覆盖该值，IPC继续发布原口径。
SampleStatus在原发布位置保存实际运行配置，GetStatus只复制；不把默认配置提前标成已应用。

CameraCapture_Stop与CameraCapture_IsStopped双重判断保持；硬件保护停止后DRAINING不代表所有帧已归还。Bridge及SD归还仍走原路径。

## 新增可执行专项

ARMCC5生成ARM机器码，由Unicorn执行真实源码。各结果JSON注明桩替换边界，不把桩结果当作硬件证明。

| 分组 | PASS | FAIL |
|---|---:|---:|
| 基线/当前逐调用等价34组 + 新接口5组 | 39 | 0 |
| 真实Bridge/Dispatcher/Storage/JPEG/AVI联动 | 11 | 0 |
| 真实CameraCapture停止、持帧与DRAINING | 6 | 0 |
| 合计 | 56 | 0 |

逐调用等价比较外部调用及参数、消息内容、每个记录点的11个Debug字段、业务标记、FAULT交付、FPS基准和Device状态。
34组轨迹逐记录相等。Run路径另外覆盖等待错误、BOOT等待切换和栈扫描位置。

集成专项执行实际Bridge、Frame Dispatcher、AppStorage、JPEG结构验证、CRC和AVI封装。
Device/Media边界、UVC服务、通道运输及StorageFS介质是测试桩。没有真实SD写入。
真实Media专项使用实际camera_capture.c；JPEG FIFO借用条件及BSP硬件是桩，不证明真实CT/NDTR、FE或总线时序。

### 行为及接口逐例结果

| 编号 | 场景 | 结果 |
|---:|---|---|
| 0 | BOOT gate | PASS |
| 1 | Owner gate | PASS |
| 2 | STOP before START | PASS |
| 3 | Unknown command | PASS |
| 4 | Bridge BUSY | PASS |
| 5 | Bridge IO | PASS |
| 6 | Configure failure | PASS |
| 7 | Media init failure | PASS |
| 8 | Device start failure | PASS |
| 9 | Media start failure | PASS |
| 10 | Start rollback failure | PASS |
| 11 | ACK full retry | PASS |
| 12 | Repeated START | PASS |
| 13 | FAULT and full queue | PASS |
| 14 | DRAINING not stopped | PASS |
| 15 | STOP BUSY retry | PASS |
| 16 | STOP error background retry | PASS |
| 17 | Bridge return pending | PASS |
| 18 | Device stop failure | PASS |
| 19 | Stats unavailable | PASS |
| 20 | Tick and frame wrap | PASS |
| 21 | FPS overflow guard | PASS |
| 22 | Wait error result retained | PASS |
| 23 | STOP then START FIFO | PASS |
| 24 | Repeated STOP | PASS |
| 25 | FPS windows | PASS |
| 26 | Media invalid after start | PASS |
| 27 | ACK full does not block FAULT | PASS |
| 28 | Real task wait and stack cadence | PASS |
| 29 | Task stack and priority | PASS |
| 30 | Duplicate task create | PASS |
| 31 | Fault notification reset | PASS |
| 32 | TEST output path | PASS |
| 33 | NONE output path | PASS |
| 34 | NULL command | PASS |
| 35 | NULL reply | PASS |
| 36 | ACK retention and confirm | PASS |
| 37 | Explicit sample and pure getter | PASS |
| 38 | Independent FAULT confirm | PASS |

### Bridge与Storage逐例结果

| 编号 | 场景 | 结果 |
|---:|---|---|
| 0 | USB+SD lease and AVI finish | PASS |
| 1 | Two-span JPEG | PASS |
| 2 | Offer queue full retry | PASS |
| 3 | Storage slot occupied | PASS |
| 4 | Storage I/O failure | PASS |
| 5 | SD return failure retry | PASS |
| 6 | Camera STOP with dual loans | PASS |
| 7 | USB cancel failure retains frame | PASS |
| 8 | Return ACK full retry | PASS |
| 9 | Duplicate Return | PASS |
| 10 | Wrong lease Return | PASS |

### 真实Media逐例结果

| 编号 | 场景 | 结果 |
|---:|---|---|
| 0 | YUY2 held frame STOP | PASS |
| 1 | BSP stop error retry | PASS |
| 2 | JPEG held frame STOP | PASS |
| 3 | JPEG FAULT DRAINING | PASS |
| 4 | JPEG FAULT without loan | PASS |
| 5 | BSP BUSY retry | PASS |

## 未改动历史回归对照

在output下分别导出基线与当前源码快照运行同一批22组脚本。当前快照只覆盖三个授权生产文件。
原工作区测试源码和已有报告没有被运行器改写。

| 分组 | 基线与当前结果 |
|---|---|
| CameraApp | 未完成编译/链接，无执行结果 |
| CameraContext | 未完成编译/链接，无执行结果 |
| CameraStream | 未完成编译/链接，无执行结果 |
| CameraDeviceLifecycle | 未完成编译/链接，无执行结果 |
| JpegMedia | 未完成编译/链接，无执行结果 |
| JpegDevice | 未完成编译/链接，无执行结果 |
| ContinuousMedia | 未完成编译/链接，无执行结果 |
| Bridge | 未完成编译/链接，无执行结果 |
| NativeIntegrated | 未完成编译/链接，无执行结果 |
| NativeTest | 未完成编译/链接，无执行结果 |
| Core | 执行记录 204；PASS 201；FAIL 3 |
| EP0IRQ | 执行记录 4；PASS 4；FAIL 0 |
| Probe | 执行记录 89；PASS 0；FAIL 89 |
| Cancellation | 执行记录 39；PASS 0；FAIL 39 |
| Payload | 执行记录 124；PASS 122；FAIL 2 |
| UvcLifecycle | 执行记录 40；PASS 34；FAIL 6 |
| EP1 | 执行记录 45；PASS 0；FAIL 45 |
| Streaming | 执行记录 36；PASS 1；FAIL 35 |
| Descriptors | 执行记录 8；PASS 7；FAIL 0；ERROR 1 |
| Strings | 执行记录 8；PASS 8；FAIL 0 |
| VideoConfig | 未完成编译/链接，无执行结果 |
| IpcStatus | 执行记录 448；PASS 448；FAIL 0 |

可逐项对照的历史执行记录为1037条：PASS 818、FAIL 219；这些是执行记录而非去重用例数。
另有11组未完成编译/链接，Descriptor脚本还有初始化错误。上述失败在指定基线复现，未发现原先能执行通过而本轮失败的记录。

Descriptor原32项测试因setUpClass错误未执行，脚本仍执行附带字符串测试，summary出现total=8、passed=7、errors=1的计算结果。
不把这个汇总解释成完整描述符验收。独立String专项8/8通过。

必须保留的兼容性问题：Ov5640Lifecycle与JPEG Device旧测试直接访问s_camera_initialized；
基线已因缺少FIFO等桩无法链接，本轮迁移后首次失败变为私有变量名不存在。
这是旧夹具对私有实现的耦合，未通过生产兼容宏或修改旧断言绕过。

其余不可运行夹具包含旧Dispatcher/Storage/Platform Sync依赖缺失。
旧USB/UVC断言存在配置和协议基线差异，本轮未修复。
完整原始错误及基线/当前逐例结果见regression_results.json和本地output/CameraModuleB1/regression日志。
“不新增已通过用例失败”不等于历史测试全绿，也不表示所有无法编译项的错误原因完全相同。

## 正式完整构建

直接使用原MDK-ARM/h7_mac.uvprojx执行UV4 -j0 -r，没有生成临时验收工程、切换Target配置或改输出路径。
之前的输出先复制到output/CameraModuleB1/build/previous_output保留。

| Target | Error | Warning |
|---|---:|---:|
| h7_mac_CM7 | 0 | 0 |
| h7_mac_CM4 | 0 | 0 |

输出目录继续为MDK-ARM/h7_mac_CM7/和MDK-ARM/h7_mac_CM4/，文件名保持原Target名。
实查CM7的AXF/MAP存在；其原CreateHexFile=0，本轮未生成HEX，也没有改开关。CM4的AXF/HEX/MAP均存在。
工程文件SHA未改变，产物存在性及日志SHA见build_results.json。

编译日志：
- output/CameraModuleB1/build/h7_mac_CM7_rebuild.log
- output/CameraModuleB1/build/h7_mac_CM4_rebuild.log

不将镜像、对象文件、隔离源码快照或备份加入提交。

## 范围审计与保留问题

- 只有三个授权生产文件变化；历史测试、CM4和Keil工程未改变。
- main和origin/main在检查时仍为e44f549，本轮独立分支不合并main。
- 保留既有Stop重复尝试、FAULT交付标记、错误保留、停止后统计口径及被忽略的发布返回值。
- 不新增消费者注册、Camera Shutdown、消息幂等协议、SceneManager或Transport Service。
- API不提供任意任务并发访问保证；运行期业务仍由CameraTask串行执行。
- Observer增加普通函数调用及小型状态副本开销，未测量实板CPU占用和实时性，不保证指令周期与原实现相同。

## 待实板复核

1. 按原配置配套烧录，确认BOOT、OLED/IPC、Camera及USB启动。
2. YUY2和JPEG正常视频、重复打开关闭、USB Reset/Disconnect，观察USB实测FPS。
3. USB与SD同时工作，确认录像、帧归还及停止仍按原行为执行。
4. 持帧停止、归还失败、采集错误后的DRAINING，不根据Debug状态强行释放帧。
5. 观察任务栈余量、IPC新鲜度及长期运行。本轮未连接实板，以上均未验证。

## 重跑命令

~~~powershell
python Tests/CameraModuleB1/run_tests.py
python Tests/CameraModuleB1/run_integration.py
python Tests/CameraModuleB1/run_media.py
python Tests/CameraModuleB1/run_regression.py
python Tests/CameraModuleB1/run_build.py
python Tests/CameraModuleB1/audit_scope.py
~~~

审计脚本属于B1审批时的范围门禁，包含main尚未合并检查，不作为合并后长期业务CI。
运行器依赖本机ARMCC5/Keil路径和已有Unicorn/pyelftools依赖。
