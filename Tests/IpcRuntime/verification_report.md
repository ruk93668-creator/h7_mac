# 阶段05C-1：双核运行期IPC与Heartbeat验收报告

基线：codex/stage05-ipc-core@dfa9bfcd6f2793b6bdafa66128ff8dd48b0a0fd2。
开发分支：codex/stage05-ipc-runtime。只推送独立分支，不合并main。

## 实现与权限

保留05B的320B V2布局、4KiB UNINIT窗口、Non-cacheable属性、固定32位字段、Header校验、3次序号读取重试和禁止序号回绕复用。共享头与状态布局文件、Scatter和MPU未改。

新增IPC_Core_ConfigureRuntime与IPC_Core_PublishRuntime。Configure仅允许本核初始化窗口、已初始化、普通线程、有效协议时执行一次；窗口关闭后不可替换。运行期每次发布检查冻结的身份回调，并复用原Ipc_Publish字段验证、writer_busy和奇偶提交。IPC_Core_PublishLocal在初始化后仍拒绝调用，未重新开放旧接口。

身份检查先于屏蔽中断执行，否则App_EventsIsOwner对PRIMASK的检查会错误拒绝合法Task。随后进入原短临界区取得单写者权限；不在整个共享状态发布期间屏蔽中断。身份回调和上下文只存本地，不能使用共享窗口作为可变身份上下文。普通C权限是模块调用契约，不是防止任意C代码直接写裸共享地址的硬件隔离。

CM7的Service由Application在任务创建时登记静态身份检查。SERVICE Initcall冻结Core授权，仍不启动运行期；只有BOOT之后的IPC Task可以Start/Process。Service与Common未引入FreeRTOS类型或头文件。

CM4的Service在初始化时冻结私有Timer身份，创建独立100ms周期Framework Timer。只有私有回调执行期间且处于普通线程，运行期发布才被允许。回调第一次运行建立本地计时基线，不与OLED刷新Timer绑定；OLED业务未修改。Framework主循环若被其他业务阻塞，IPC也会推迟，这由心跳诊断反映，不宣称它是独立硬件看门狗。

## 调用链

```text
CM7 main原EarlyInit/HSEM0流程不变
  → App_TasksCreate
      → Camera / Usb / Control / IPC静态创建
      → IPC登记身份检查和BOOT接收端
  → Framework SERVICE Init冻结运行期授权
  → main记录初始化结果
  → Scheduler启动
  → Control统一广播BOOT_READY
  → IPC Task开始运行期计时
  → RuntimeProcess(本地Platform_TimeGetMs)
      → 有限读取对端完整状态
      → 本地观察进展/超时
      → 发布本核心跳及累计故障
  → vTaskDelay(100ms)，不忙等

CM4原HSEM0等待/唤醒不变
  → Framework SERVICE Init初始化CM4块
  → 冻结私有Timer授权 / 创建并启动100ms周期Timer
  → Framework主循环TimerProcess
  → 私有IPC Timer回调
  → 同一份Common运行期处理
```

RUNTIME_TARGET_IPC追加在既有Camera/USB/Control后，旧编号不变，RUNTIME_TARGET_COUNT由3变4。任务计数检查改为该枚举常量，原app_events的数组、完整性检查和BOOT循环自动覆盖4个目标。g_app_tasks_debug.created_tasks=4；调度器启动后加Idle共5个内核任务，当前配置不启用FreeRTOS软件Timer任务。

IPC Task优先级1，512 Word（2048B）静态栈和静态TCB。原Camera、USB、Control优先级、栈和业务逻辑不变。CM4新增一个Framework Timer对象，不新增RTOS任务。

## 心跳与本地判定

每个实际处理周期最多成功提交一次本核心跳。uint32_t加1自然回绕，发布失败不伪增成功次数。Process不足100ms直接返回，不补发调度推迟期间的历史心跳。Task使用100ms阻塞延时，执行耗时和调度竞争会影响实际间隔，max_step_gap_ms与scheduling_delays记录这种影响。

启动期从本核Runtime_Start起算。第一份Ready快照只记录心跳基线；必须看到后续不同值才能ONLINE。Ready不等于ONLINE，不比较两核Tick。对端心跳MAX到0、跳过多个计数都视为新进展，不要求数值递增。

用本地uint32_t(now-last_progress)测量观察进展间隔；未握手时使用now-start_tick。500ms是超时阈值，实际发现时间受处理周期及调度延迟影响。首样本晚到不延长启动期限。

- STARTING：尚未观察到后续进展。
- ONLINE：已经观察到进展，且未达到超时阈值。
- STARTUP_TIMEOUT：启动窗口内未取得进展。
- TIMEOUT：曾ONLINE，之后500ms未观察到进展。
- PROTOCOL_ERROR：本次协议读取错误；与时间超时分别累计。

一次BUSY只累计busy_reads，不直接失联；连续BUSY若超过无进展期限，也会超时。调度器长时间未调度本任务，恢复后若成功读到新心跳，优先记录刚观察到的进展；若无新值则超时。没有把本核调度停顿伪装成准确的对端故障定位。

看到新心跳后可从超时恢复ONLINE，但历史fault_flags保留。恢复ONLINE只说明重新观察到心跳变化，不代表单核热复位恢复或新会话验证。

共享fault_flags使用原IPC_FAULT_HEARTBEAT_TIMEOUT等位，不改变旧位位置。协议不可识别时Core拒绝发布，错误保留在本地运行期诊断，不向未知布局写故障字段。

RuntimeProcess的返回值是本次本核发布结果；它可以在对端超时时返回SYS_OK，让自己的心跳继续运行。对端状态必须检查peer_state/read_result/peer_age_ms，不用Task.result或Core最近操作结果推断对端ONLINE。

本地g_ipc_runtime_debug的写入和GetRuntimeStatus读取都经过同一短临界区，防止高优先级任务读到半更新诊断。Watch本身仍可能分批刷新字段，不等价于API一致快照。

## 修改清单

| 文件 | 修改原因 |
|---|---|
| Common/Inc/ipc_core.h、Common/Src/ipc_core.c | 初始化冻结的运行期授权，复用原安全发布逻辑 |
| Common/Inc/ipc_runtime.h、Common/Src/ipc_runtime.c | 新增不依赖RTOS的心跳、首次握手、进展间隔和诊断 |
| Common/Inc/runtime_event.h | 追加IPC目标，保留既有目标编号 |
| CM7/Application/app_tasks.c | 创建IPC任务，按4目标检查BOOT准备 |
| CM7/Application/Tasks/ipc_task.c/.h | 新增唯一静态IPC任务和身份检查 |
| CM7/Service/ipc_service.c/.h | 运行期登记、Start/Process、诊断读取及原初始化授权冻结 |
| CM4/Service/ipc_service.c/.h | 私有Framework Timer、唯一回调发布及诊断读取 |
| MDK-ARM/h7_mac.uvprojx | 两核Common组加入runtime，CM7组加入IPC Task，保留本机已有设置 |
| Tests/IpcV2/protocol_test.c、run_tests.py | 仅补依赖桩与真实runtime编译项，原512项断言不变 |
| Tests/IpcV2/commands.json | 原512项本轮实际编译链接记录；results.json重跑结果与原版一致 |
| Tests/IpcRuntime/ | 新运行期、真实内核任务专项及正式构建记录 |
| Docs/Design/IPC_Runtime_Heartbeat.md | 接口、观察变量与实板验收说明 |

用户原video_config.c、Keil设置和MJPEG日志修改完整保留，不作为本次提交内容。没有修改CM7/CM4 main、公共时钟、HSEM、BSP、Device、Media、Camera/USB/MJPEG/OLED或HAL/LL。

## 软件验收

实际使用ARMCC5.06 update7 build960，CM7/CM4各O0/O2生成ARM指令并在Unicorn执行。不是只做静态搜索。

| 套件 | 实际执行 | PASS | FAIL |
|---|---:|---:|---:|
| 原05B回归 | 512 | 512 | 0 |
| 新Runtime（65场景×4组合） | 260 | 260 | 0 |
| 真实FreeRTOS内核任务专项 | 20 | 20 | 0 |
| 合计 | 792 | 792 | 0 |

Runtime覆盖心跳周期、首次采样/后续进展、500ms启动/运行超时、Tick与心跳回绕、Ready边界、BUSY、协议错误、发布失败、授权身份和CM4 Timer创建/启动失败。实际内存写钩子验证诊断副本写入时临界区处于有效状态。

Task覆盖4目标计数、BOOT前无限阻塞、BOOT广播、延后事件保留、只有IPC任务能通过身份检查、100ms阻塞参数、任务创建/附加/授权失败、重复与迟到创建拒绝、静态栈对齐。模拟器显式选择当前TCB及内核运行态，没有模拟真实SVC/PendSV/NVIC抢占。

测试夹具曾出现两项问题：Port私有CriticalNesting不能直接extern链接；ARMCC把memclr桩中的memset调用转换为自身递归。最终分别由模拟器初始化私有Port状态和桩内直接逐字节清零解决；没有修改生产逻辑或降低断言来绕过这些问题。

实际命令见README；逐项结果见results.json、task_results.json；原512项结果仍在Tests/IpcV2。此轮未执行其他Camera/USB的全部历史软件套件，不把源码未改和正式编译通过当作这些套件已经执行。

最终正式完整重编：h7_mac_CM7、h7_mac_CM4均0 Error、0 Warning。构建使用保留了用户已有本机设置的正式工程，未声称是在丢弃用户设置后的干净工作树中构建。

| Target | Code | RO-data | RW-data | ZI-data |
|---|---:|---:|---:|---:|
| CM7 | 95730 | 2774 | 1204 | 44484 |
| CM4 | 22938 | 962 | 936 | 2744 |

上述程序大小以本轮最终Keil日志为准；若后续配置变化须重新查看Map。IPC Task静态栈2048B、TCB76B；各核运行期上下文76B、诊断副本60B。未引入动态内存。

正式两核最终完整重编结果、源码指纹和工程SHA见build_results.json及rebuild.log。共享区Map仍为g_ipc=0x38000000、320B，RW_SHARED为UNINIT、Max=4KiB。新增资源在各核普通RAM，不占SDRAM视频区域。

## 剩余风险

新增运行期实板尚未执行；05B曾由用户反馈冷启动布局和原功能正常，不能扩展为05C已实板通过。真实心跳周期、任务栈余量、双核总线可见性及高负载下的最长调度间隔仍须观察。

没有热复位恢复或boot_id会话协议；不测试单核Reset作为正常恢复方式。长期停机使本地32位Tick跨过完整回绕周期不属于当前周期观察假设。序号耗尽依旧NO_RESOURCE而非在线清零重用。

心跳说明IPC执行者得到运行机会，不表示Camera/USB/OLED所有业务都健康。超时说明没有及时观察到进展，不能单独区分对端停机、本核饿死、共享区异常或调试器暂停。Task和Timer都不是独立硬件看门狗。
