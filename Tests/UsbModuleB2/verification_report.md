# B2 USB业务状态迁移设计与验收报告

基线：8bfa9bc4316c0faaceef65bc90fbe84d03eae55f。
分支：codex/stage-B2-usb-module。

本轮只迁移USB业务状态所有权。新增专项121/121 PASS，两核正式完整重编均0 Error、0 Warning。
历史回归保留失败及不可运行夹具，不宣称全仓全绿；没有连接实板，不宣称Windows枚举或视频实板验收通过。

## 修改文件及职责

| 文件 | 改动 |
|---|---|
| CM7/Application/app_usb.c | 新增唯一业务上下文、命令、待发回复、Mode推进结果处理、停止收尾和EOF/FPS统计 |
| CM7/Application/app_usb.h | 新增普通C接口、只读状态副本及Diagnostic Observer契约 |
| CM7/Application/Tasks/usb_task.c | 移除业务静态变量，保留任务、消息、显式数据面、SOF诊断和状态发布 |
| MDK-ARM/h7_mac.uvprojx | 仅在CM7登记app_usb.c/.h，不改变宏、Target选项、输出路径或CM4 |
| Tests/UsbModuleB2/ | 新增差分、真实USB集成、范围审计、构建和回归记录 |

usb_task.h没有修改，原Debug布局保持。USB_Mode、USB_Device、USB_UVC及描述符/Payload、Bridge、App_Messages、App_Events均保持字节一致。
没有修改Camera、Storage、BSP、HAL、ST Core或CM4。没有新增MSC、Device Registry、消费者注册或新的恢复策略。

## 调用关系

~~~text
USB IRQ/HAL → BSP → USB_Device/ST Core
                         ├─ 同步EP0/Class回调、Token及资源状态
                         └─ 原App_Events适配 → 唤醒UsbTask

UsbTask_Process：
所有者、BOOT及wake_count
→ 原SOF观察
→ ACK尝试，再独立尝试FAULT
→ 在原位置读取本轮uvc模式
→ 原门控下最多接收一条命令 → App_USBHandleCommand
→ App_USBProcess（只处理Mode）
→ UVC模式下：
    stopping时CancelFrame
    → USB_UVC_Process
    → App_UvcBridgeUsbProcess(CanTransfer)
    → stopping时再次USB_UVC_Process
→ App_USBFinishRound(本轮uvc)
→ ACK尝试，再独立尝试FAULT

UsbTask_Run：
原位置PublishStatus
→ 原1000ms栈余量扫描
~~~

App_USBProcess不隐藏UVC或Bridge推进。FinishRound不调用Cancel、UVC Process或Bridge Process。

## 唯一上下文与状态边界

| 旧位置/变量 | App_USB字段 |
|---|---|
| s_usb_started | started |
| s_usb_stopping | stopping |
| s_usb_stop_command | stop_command |
| s_usb_ack / s_usb_ack_pending | ack / ack_pending |
| s_usb_fault / s_usb_fault_reported | fault / fault_reported |
| s_usb_fps_tick / s_usb_fps_frames | fps_tick / fps_frames |
| 原Debug业务观察值 | last_result、command_id、completed_frames、measured_fps_milli |
| 静态诊断入口 | observer |

state不保存为第二份业务状态，仍由stopping优先、started其次推导0/1/2。
Task保留TCB、2048-word且8字节对齐的栈、句柄、BOOT、等待和运行统计。
优先级4、20ms兜底等待、BOOT前无限等待、1000ms栈扫描保持。
SOF序号、合并观察计数和最大间隔仍由Task按原公式维护，不迁入业务上下文，也不解释为实际丢包数。

App_USB不复制Mode状态、Configured、Probe/Commit、Alt、EP1、Token、frame_owned、Payload或Bridge租约。
started只表示业务START成功；Mode RUNNING不等于当前正在视频传输；最终提交仍由UVC can_submit及Bridge决定。

## 对外接口

以下运行期接口只由UsbTask串行调用。Getter不是跨任务一致性接口，其他任务仍从App_Status读取副本。

| 接口 | 输入输出与副作用 |
|---|---|
| App_USBSetDiagnosticObserver | 绑定静态只读诊断入口，不推进业务 |
| App_USBCanAcceptCommand | 只读：!ack_pending && !stop_command |
| App_USBHandleCommand | 一条已出队命令；START/STOP/未知命令按原规则建立回复，不收发队列 |
| App_USBCanTransfer | 只读：started && !stopping；不代表USB/UVC就绪 |
| App_USBIsStopping | 只读停止要求，供Task显式安排数据面 |
| App_USBProcess | 仅原Mode推进及错误识别，同轮Mode错误可令后续Cancel执行 |
| App_USBFinishRound | 输入原位置采样的本轮uvc；停止判断、诊断及EOF/FPS，不隐藏数据面 |
| App_USBGetPendingAck / GetPendingFault | 分别复制记录，不消耗；输出NULL时返回false |
| App_USBConfirmAckEnqueued / ConfirmFaultEnqueued | 只在对应消息实际入队成功后调用，不代表Control已处理 |
| App_USBGetStatus | 只读状态、编号、结果、累计EOF及FPS；不读时钟或底层 |
| App_USBGetLastResult | 只读业务结果，不覆盖Task等待错误 |

没有机械增加SampleStatus：USB没有最近应用配置缓存，采集EOF/FPS只在原FinishRound时点发生。
App_USB没有FreeRTOS任务、队列或通知调用，不新增同步等待或锁。
确认接口只在本任务本次Send成功后立即调用，不跨轮保存待确认消息。

## Diagnostic Observer原位置对应

| 字段 | 基线usb_task.c原行号 | 新触发位置 |
|---|---:|---|
| command_id | 170 | HandleCommand保存ACK之后、实际执行命令之前 |
| result：START | 186 | ACK结果及pending更新之后 |
| result：Mode故障 | 207 | fault和stopping更新之后 |
| result：STOP结果 | 242 | ACK、pending及stop_command更新之后 |
| state | 247 | 停止处理之后，EOF采样之前 |
| completed_frames | 249 | 全局EOF读取之后，读取时间之前 |
| measured_fps_milli | 255 | 原公式计算之后，更新窗口基准之前 |

回调仅投影指定Debug字段，不修改业务、发送消息或访问硬件。
未知命令和STOP刚接收时不新增result写入。等待异常仍由Task在原分支写Debug.result。
PublishStatus保持原Debug口径及原调用位置，没有每轮用业务last_result覆盖Task等待错误。

## FinishRound逐语句对应

| 原usb_task.c位置 | 保留行为 |
|---|---|
| 222 | 仅stopping时执行停止判断 |
| 224 | 默认结果BUSY |
| 225–226 | Bridge Idle才调用Mode Stop |
| 227–229 | 非Idle且本轮UVC时才查询；查询成功、仍持帧、RECOVERY、错误非OK才采用错误 |
| 232–235 | 只有Stop成功才清started、stopping |
| 237–242 | stop_command且非BUSY时完成ACK并更新结果诊断 |
| 244–245 | 停止错误仅在原fault为空时保存 |
| 247 | 投影业务state |
| 248–249 | 读取跨Class累计EOF并投影 |
| 250–251 | 按原位置读取时间并计算elapsed |
| 252–257 | 满1000ms按原公式计算FPS，再更新基准 |

Cancel返回值仍按原代码忽略。Bridge禁用且仍借帧时内部也可能Cancel，本轮不去重。
Stop失败ACK发出后仍可后台重试，不新增Stop超时策略。
Mode Stop成功不能单独作为归还证明：原UVC先确认硬件安全，Bridge完成Return/ACK，随后才进入Mode/Core停止路径。
App_USB不直接释放Media帧。

## START、STOP及FAULT保留语义

- START成功仅为软件启动成功，通常仍等待枚举；不等待Commit或Alt1。
- START BUSY仍写业务fault，不按Camera的规则忽略BUSY。
- 成功START才清fault_reported，并先取时间再取全局EOF建立窗口。
- STOP先置stopping/stop_command，BUSY不完成ACK。
- STOP其他错误可完成失败ACK，但继续保留stopping后台推进。
- 未知命令回复INVALID_ARG，不额外更新result或触发硬件操作。
- ACK和FAULT在每轮前后分别尝试；ACK失败不跳过FAULT。
- FAULT保留原初始化及消息来源，command_id不新增关联语义。
- 模式错误继续使用Device设备级结果，不替换为最近一次接口操作结果。

## 三种模式

| 模式 | 行为 |
|---|---|
| UVC | 正常Mode→UVC→Bridge；停止Cancel→Process→Bridge(false)→Process→FinishRound |
| TEST | 不执行UVC/Bridge数据面分支，仍执行原停止检查及统计 |
| NONE | START返回成功并置业务started，但不连接USB；不增加跳过Task或特殊停止分支 |

Reset、Disconnect不直接清业务started；Suspend不变成STOP；原Core/UVC处理资源和状态，Bridge按原查询及归还协议推进。
EP0仍由Core同步处理，未改为任务消息。DataIn/SOF/Incomplete及Token、Abort、Payload逻辑未迁入业务上下文。

## 软件验证

| 项目 | PASS | FAIL |
|---|---:|---:|
| UVC/TEST/NONE各32组基线与当前逐调用对比 | 96 | 0 |
| 独立Getter及ACK/FAULT接口 | 6 | 0 |
| 真实BSP/Core/Mode/UVC/Bridge/Task集成 | 19 | 0 |
| B2新增专项合计 | 121 | 0 |
| 未修改的B1 Camera/Bridge/Storage/Media回归 | 56 | 0 |

差分记录每次依赖调用、消息字节哈希、当时12个Debug字段及业务、Mode/UVC、帧所有权观察。
96组基线与当前轨迹逐记录相等。三种模式执行相同事件序列；TEST/NONE中未进入的UVC分支不被当作实际Abort验收。

真实集成链接当前生产BSP、USB Device、ST Core、USB_Mode、UVC Class/Descriptor/Payload、Bridge、新App_USB和UsbTask。
仅HAL硬件、RTOS环境、消息运输、Media及Dispatcher/Storage边界使用桩。
执行了真实EP0配置及Probe/Commit/Alt，未手工伪造有效Commit。
这些是模拟器执行结果，不是实板、USB线缆或Windows验收。

### 差分与接口逐例结果

| 编号 | 模式 | 场景 | 结果 |
|---:|---|---|---|
| 0 | UVC | BOOT gate | PASS |
| 1 | UVC | Owner gate | PASS |
| 2 | UVC | STOP before START | PASS |
| 3 | UVC | Unknown command | PASS |
| 4 | UVC | START IO failure | PASS |
| 5 | UVC | Not initialized | PASS |
| 6 | UVC | ACK failure with FAULT | PASS |
| 7 | UVC | Configured after START | PASS |
| 8 | UVC | Repeated START BUSY fault | PASS |
| 9 | UVC | Mode error same-round cancel | PASS |
| 10 | UVC | Abort failure ownership | PASS |
| 11 | UVC | Bridge Return ACK wait | PASS |
| 12 | UVC | Mode Stop IO retry | PASS |
| 13 | UVC | Mode Stop BUSY retry | PASS |
| 14 | UVC | Stream query failure | PASS |
| 15 | UVC | Class absent on STOP | PASS |
| 16 | UVC | EOF FPS sample | PASS |
| 17 | UVC | Tick and EOF wrap | PASS |
| 18 | UVC | FPS overflow guard | PASS |
| 19 | UVC | Task wait error retained | PASS |
| 20 | UVC | SOF coalescing | PASS |
| 21 | UVC | SOF wrap | PASS |
| 22 | UVC | ACK and FAULT independent retry | PASS |
| 23 | UVC | STOP START FIFO | PASS |
| 24 | UVC | Repeated STOP | PASS |
| 25 | UVC | EOF event observation (stub) | PASS |
| 26 | UVC | Frame loan then STOP | PASS |
| 27 | UVC | Wait and stack cadence | PASS |
| 28 | UVC | Task stack and priority | PASS |
| 29 | UVC | Duplicate task create | PASS |
| 30 | UVC | Task allocation failure | PASS |
| 31 | UVC | Fault report reset on START | PASS |
| 32 | TEST | BOOT gate | PASS |
| 33 | TEST | Owner gate | PASS |
| 34 | TEST | STOP before START | PASS |
| 35 | TEST | Unknown command | PASS |
| 36 | TEST | START IO failure | PASS |
| 37 | TEST | Not initialized | PASS |
| 38 | TEST | ACK failure with FAULT | PASS |
| 39 | TEST | Configured after START | PASS |
| 40 | TEST | Repeated START BUSY fault | PASS |
| 41 | TEST | Mode error same-round cancel | PASS |
| 42 | TEST | Abort failure ownership | PASS |
| 43 | TEST | Bridge Return ACK wait | PASS |
| 44 | TEST | Mode Stop IO retry | PASS |
| 45 | TEST | Mode Stop BUSY retry | PASS |
| 46 | TEST | Stream query failure | PASS |
| 47 | TEST | Class absent on STOP | PASS |
| 48 | TEST | EOF FPS sample | PASS |
| 49 | TEST | Tick and EOF wrap | PASS |
| 50 | TEST | FPS overflow guard | PASS |
| 51 | TEST | Task wait error retained | PASS |
| 52 | TEST | SOF coalescing | PASS |
| 53 | TEST | SOF wrap | PASS |
| 54 | TEST | ACK and FAULT independent retry | PASS |
| 55 | TEST | STOP START FIFO | PASS |
| 56 | TEST | Repeated STOP | PASS |
| 57 | TEST | EOF event observation (stub) | PASS |
| 58 | TEST | Frame loan then STOP | PASS |
| 59 | TEST | Wait and stack cadence | PASS |
| 60 | TEST | Task stack and priority | PASS |
| 61 | TEST | Duplicate task create | PASS |
| 62 | TEST | Task allocation failure | PASS |
| 63 | TEST | Fault report reset on START | PASS |
| 64 | NONE | BOOT gate | PASS |
| 65 | NONE | Owner gate | PASS |
| 66 | NONE | STOP before START | PASS |
| 67 | NONE | Unknown command | PASS |
| 68 | NONE | START IO failure | PASS |
| 69 | NONE | Not initialized | PASS |
| 70 | NONE | ACK failure with FAULT | PASS |
| 71 | NONE | Configured after START | PASS |
| 72 | NONE | Repeated START BUSY fault | PASS |
| 73 | NONE | Mode error same-round cancel | PASS |
| 74 | NONE | Abort failure ownership | PASS |
| 75 | NONE | Bridge Return ACK wait | PASS |
| 76 | NONE | Mode Stop IO retry | PASS |
| 77 | NONE | Mode Stop BUSY retry | PASS |
| 78 | NONE | Stream query failure | PASS |
| 79 | NONE | Class absent on STOP | PASS |
| 80 | NONE | EOF FPS sample | PASS |
| 81 | NONE | Tick and EOF wrap | PASS |
| 82 | NONE | FPS overflow guard | PASS |
| 83 | NONE | Task wait error retained | PASS |
| 84 | NONE | SOF coalescing | PASS |
| 85 | NONE | SOF wrap | PASS |
| 86 | NONE | ACK and FAULT independent retry | PASS |
| 87 | NONE | STOP START FIFO | PASS |
| 88 | NONE | Repeated STOP | PASS |
| 89 | NONE | EOF event observation (stub) | PASS |
| 90 | NONE | Frame loan then STOP | PASS |
| 91 | NONE | Wait and stack cadence | PASS |
| 92 | NONE | Task stack and priority | PASS |
| 93 | NONE | Duplicate task create | PASS |
| 94 | NONE | Task allocation failure | PASS |
| 95 | NONE | Fault report reset on START | PASS |
| 96 | — | NULL command | PASS |
| 97 | — | NULL reply | PASS |
| 98 | — | Pure getters and ACK confirmation | PASS |
| 99 | — | FAULT confirmation preserves ACK | PASS |
| 100 | — | ACK confirmation preserves FAULT | PASS |
| 101 | — | Diagnostic observer is optional | PASS |

### 真实USB集成逐例结果

| 编号 | 模式 | 场景 | 结果 |
|---:|---|---|---|
| 0 | — | STOP before enumeration | PASS |
| 1 | — | Configured without Commit/Alt1 | PASS |
| 2 | — | Real ISO EOF and Bridge Return | PASS |
| 3 | — | STOP in flight waits for ACK | PASS |
| 4 | — | Abort failure retains frame and retries | PASS |
| 5 | — | Reset preserves business and returns frame | PASS |
| 6 | — | Disconnect preserves business and returns frame | PASS |
| 7 | — | Suspend Resume preserves loan | PASS |
| 8 | — | DataIn only confirms in Task | PASS |
| 9 | — | EP0 device descriptor | PASS |
| 10 | — | EP0 STALL next SETUP recovery | PASS |
| 11 | — | ISO Incomplete with failed Abort | PASS |
| 12 | — | Return ACK queue full | PASS |
| 13 | — | Hardware Stop failure retry | PASS |
| 14 | — | Repeated STOP | PASS |
| 15 | — | Real TEST enumeration | PASS |
| 16 | — | NONE no physical connection | PASS |
| 17 | — | Repeated START BUSY fault | PASS |
| 18 | — | Mode enumeration timeout | PASS |

## 历史回归对照及未完成项目

在output/UsbModuleB2/regression下创建基线与当前快照，执行25组未修改脚本，原工作区旧测试及报告保持不变。

| 分组 | 基线与当前结果 |
|---|---|
| B1Behavior | 记录39；PASS 39；FAIL 0 |
| B1BridgeStorage | 记录11；PASS 11；FAIL 0 |
| B1Media | 记录6；PASS 6；FAIL 0 |
| CameraApp | 未完成编译/链接，无可执行结果 |
| CameraContext | 未完成编译/链接，无可执行结果 |
| CameraStream | 未完成编译/链接，无可执行结果 |
| CameraDeviceLifecycle | 未完成编译/链接，无可执行结果 |
| JpegMedia | 未完成编译/链接，无可执行结果 |
| JpegDevice | 未完成编译/链接，无可执行结果 |
| ContinuousMedia | 未完成编译/链接，无可执行结果 |
| Bridge | 未完成编译/链接，无可执行结果 |
| NativeIntegrated | 未完成编译/链接，无可执行结果 |
| NativeTest | 未完成编译/链接，无可执行结果 |
| Core | 记录204；PASS 201；FAIL 3 |
| EP0IRQ | 记录4；PASS 4；FAIL 0 |
| Probe | 记录89；PASS 0；FAIL 89 |
| Cancellation | 记录39；PASS 0；FAIL 39 |
| Payload | 记录124；PASS 122；FAIL 2 |
| UvcLifecycle | 记录40；PASS 34；FAIL 6 |
| EP1 | 记录45；PASS 0；FAIL 45 |
| Streaming | 记录36；PASS 1；FAIL 35 |
| Descriptors | 记录8；PASS 7；FAIL 0；ERROR 1 |
| Strings | 记录8；PASS 8；FAIL 0 |
| VideoConfig | 未完成编译/链接，无可执行结果 |
| IpcStatus | 记录448；PASS 448；FAIL 0 |

有逐项结果的历史执行记录共1093条：PASS 874，FAIL 219。
这里包含B1的56条记录，不是去重后的用例数。另有11组未完成编译/链接，Descriptor有初始化错误。

未发现基线已通过而当前失败的执行记录，不等于全仓全绿。
Descriptor原32项未执行，其附带String测试仍执行，脚本summary出现total=8、passed=7、errors=1；不能据此声称完整描述符通过。
旧Native集成夹具在基线已缺FIFO、IpcTask、StorageTask及Platform Sync等依赖；本轮又因没有链接新app_usb.c而出现App_USB未解析符号。
这些脚本仍属于不可运行夹具，不能声称错误原因完全相同，也没有用生产兼容别名或降低断言来绕过。
其余历史USB/UVC失败及旧Camera私有字段耦合保留。完整逐组结果和错误尾部见regression_results.json。

## 正式全量构建

直接使用MDK-ARM/h7_mac.uvprojx执行UV4 -j0 -r；未新增临时验收工程、切换产品宏或更改输出位置。
原输出先复制到output/UsbModuleB2/build/previous_output保留。

| Target | Error | Warning |
|---|---:|---:|
| h7_mac_CM7 | 0 | 0 |
| h7_mac_CM4 | 0 | 0 |

CM7输出仍为MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf及.map；原CreateHexFile=0，没有新增HEX。
CM4输出仍为MDK-ARM/h7_mac_CM4/h7_mac_CM4.axf、.hex及.map。
build_results.json记录产物存在性、编译日志哈希和源码哈希。

本地完整编译日志：
- output/UsbModuleB2/build/h7_mac_CM7_rebuild.log
- output/UsbModuleB2/build/h7_mac_CM4_rebuild.log

测试镜像、对象文件、完整隔离源码快照和原输出备份均不加入提交。

## 范围及结构审计

scope_audit.json验证：

- 仅三个Application生产文件和必要CM7工程登记变化。
- CM4工程内容逐字保持；工程其他变化仅两个文件登记。
- 指定冻结模块、usb_task.h及历史测试保持不变。
- Create、Run、PublishStatus保留原语句，Run只新增循环前的观察者绑定。
- SOF观察保持原语句。
- 命令、Mode及FinishRound主体经变量归属替换后与基线语句一致；Observer为明确的诊断插入点。
- 显式数据面顺序保持，FinishRound不隐藏数据面。
- App_USB不调用FreeRTOS消息或等待API。

该脚本为B2合并前范围门禁，包含main未修改检查；不是合并后长期业务CI。

## 风险与待实板验证

1. Observer和Getter副本增加普通函数调用开销，未测量实板CPU占用，不保证指令周期完全相同。
2. 新接口仅保证所属UsbTask串行使用，不提供任意任务并发快照契约。
3. 原START BUSY故障记录、FAULT交付标记、失败ACK后后台停止等行为均保留。
4. 原Control协调停止策略未变，不新增模块自主STOP或Camera Shutdown。
5. 未验证实板TEST/UVC枚举、真实视频FPS、Reset/插拔/挂起恢复、USB+SD并行及长期运行。
6. 不把Mode RUNNING、业务started或Mode Stop返回成功单独当作视频运行或帧归还证明。
7. 历史失败和无法运行测试仍需独立维护，本轮不修复。

建议实板顺序：正常UVC枚举及视频→关闭/重开播放器→在途STOP→复位/USB拔插→USB与SD并行；
观察g_usb_task_debug、g_bridge_debug及IPC/OLED，不修改私有业务状态以强制释放帧。

## 重跑

~~~powershell
python Tests/UsbModuleB2/run_tests.py
python Tests/UsbModuleB2/run_integration.py
python Tests/UsbModuleB2/run_regression.py
python Tests/UsbModuleB2/run_build.py
python Tests/UsbModuleB2/audit_scope.py
~~~

运行器使用当前本机ARMCC5/Keil和已有Unicorn/pyelftools依赖；报告区分静态检查、机器码执行、正式构建与实板验收。
