# 阶段05C-2：Camera/USB跨核状态同步验收报告

基线：codex/stage05-ipc-runtime@133ffcdd4b99f900165fa8116001ccf7ee2f58af。
开发分支：codex/stage05-ipc-status。独立提交/推送，不修改或合并main。

## 结果与边界

软件实际执行1281次全部PASS，FAIL 0：原05B 512次、原05C-1 260+20次、新V3/状态448次、扩展真实内核41次。最后41项包含原20项再次执行，不能把这些全部说成互不重复的新场景。

两核最终正式完整重编、源码指纹及工程SHA见build_results.json，均要求0 Error、0 Warning。软件模拟不等于实板MJPEG/IPC并行验收。新增运行期实板尚未操作，没有填写虚假的FPS、丢帧或CPU影响结论。

## 协议升级

协议正式升为V3：IPC_VERSION=3，feature_flags=SEQLOCK(1)|APPLICATION(2)=3。Magic保留0x49504331，版本字段负责布局语义选择。V2和V3互不二进制兼容，按配套双核固件完整断电启动，不支持混烧、热升级或单核热复位恢复。

共享总大小仍320B：Header64B、CM7块128B、CM4块128B；地址仍0x38000000，窗口仍4KiB UNINIT，MPU不改。前7个32位基础字段不变。V2原reserved[25]不再以数组方式存业务；V3正式声明80B application结构及剩余reserved[5]，所有剩余保留字段仍强制为0。

CM7为Header及CM7块唯一写者；CM4只写自己的块。CM4块中的application必须全零，CM7读取时也检查此约束。两核只通过序号一致的完整128B状态访问业务字段，仍奇数更新、偶数提交、最多3次读取、序号耗尽拒绝回绕复用。

| 区域 | 地址 | 大小 |
|---|---|---:|
| Header | 0x38000000 | 64B |
| CM7基础字段 | 0x38000040 | 28B |
| CM7 application | 0x3800005C | 80B |
| CM7 reserved[5] | 0x380000AC | 20B |
| CM4状态块 | 0x380000C0 | 128B |
| 协议结束（不包含） | 0x38000140 | |

Header字段偏移仍为magic0/version4/layout_bytes8/header_bytes12/status_block_bytes16/feature_flags20/reserved24。状态块sequence0/owner4/block_bytes8/ready12/heartbeat16/fault_flags20/last_result24；application28，reserved108。编译期断言验证全部尺寸、业务每字段偏移和32B块对齐，正式Map再次确认。

## 正式业务字段偏移

下表“块内”相对CM7块0x38000040；“载荷内”相对application。

| 字段 | 类型 | 载荷内 | 块内 |
|---|---|---:|---:|
| valid_mask | uint32_t | 0 | 28 |
| camera_state | uint32_t | 4 | 32 |
| usb_state | uint32_t | 8 | 36 |
| control_state | uint32_t | 12 | 40 |
| camera_fps_milli | uint32_t | 16 | 44 |
| usb_fps_milli | uint32_t | 20 | 48 |
| captured_frames | uint32_t | 24 | 52 |
| dropped_frames | uint32_t | 28 | 56 |
| usb_completed_frames | uint32_t | 32 | 60 |
| width | uint32_t | 36 | 64 |
| height | uint32_t | 40 | 68 |
| format | uint32_t | 44 | 72 |
| last_fault | int32_t | 48 | 76 |
| camera_result | int32_t | 52 | 80 |
| usb_result | int32_t | 56 | 84 |
| control_result | int32_t | 60 | 88 |
| camera_age_ms | uint32_t | 64 | 92 |
| usb_age_ms | uint32_t | 68 | 96 |
| control_age_ms | uint32_t | 72 | 100 |
| snapshot_id | uint32_t | 76 | 104 |

valid_mask位0/1/2分别是Camera/USB/Control。格式编号为UNKNOWN=0、Y8=1、RGB565=2、YUY2=3、JPEG=4，Application显式映射Camera枚举，不能直接比较Camera原枚举数值。未成功启动过Camera时宽高0、格式UNKNOWN，不能把默认配置当已生效配置。

Camera/USB任务状态0停止、1运行或已启动、2停止中；USB已启动不等于主机已经配置或正在收视频。Control状态0 IDLE、1 START_CAMERA、2 START_USB、3 RUNNING、4 STOP_USB、5 STOP_CAMERA、6 ERROR。

FPS来自原任务的最近实际统计窗口，单位千分之一FPS，不取video_config目标值；停止后保留的上一个窗口仍是历史测量，要结合任务状态解释。帧计数采用原实际完成/丢帧计数，允许uint32_t自然回绕，不用USB提交次数冒充完成帧数。

last_fault按Control实际处理顺序记录：业务故障消息、匹配ACK失败、自身超时或命令编号耗尽。只增加本地观测，不改变原g_control_task_debug.last_fault的旧语义（该旧字段主要记录收到的故障消息），不因成功恢复自动清除最近故障历史。

## 本地快照与唯一发布

Camera/Usb/Control任务每轮原Process结束后调用各自的私有PublishStatus，仅读自己的任务变量，不读取其他任务调试结构。各自的App_StatusPublish接口验证真实任务身份、参数及字段范围，在同一个短FreeRTOS临界区提交完整来源记录、有效位和本地时间戳。没有用volatile假定多字段原子性。

IpcTask每100ms调用App_StatusGetSnapshotAt，在一次临界区取得三个来源、编号和本地采样Tick。三个来源来自各自最近一次完整记录，不承诺它们是在同一个物理瞬间更新。每个来源的年龄用于描述这种差异。

IpcTask把载荷和本核采样时刻交给IPC_Service_SetApplication，再走原RuntimeProcess与受控Core发布。运行期会补入采样到处理入口的同核等待延迟，年龄饱和相加，不把绝对Tick传到CM4。其他任务不能调用Core运行期发布冒充IpcTask，也不能替换初始化时冻结的身份回调。

Application/GetSnapshotAt的两个输出不能重叠；Service和Application输出均拒绝4KiB共享窗口别名、NULL、未对齐及地址溢出。正式共享层每个业务字段按32位显式读取/写入，不用跨核结构体memcpy或强制指针转换。

原Camera/USB/Control的Process状态机分支、命令/ACK、启停顺序和所有权逻辑保持不变。Control仅在原故障处理点旁记录观测值；逐函数审计去除这些只观测调用后，与基线一致。没有改变DCMI、DMA、MJPEG FIFO、UVC、帧租借、OLED或时钟/HSEM流程。

## CM4统一读取与新鲜度

IPC_Service_GetApplicationStatus返回IpcApplicationView_t完整本地副本，仅读私有缓存，不把共享指针交给调用者。SYS_OK表示取得了本地视图，不保证所有来源有效或新鲜。

- valid_mask：缓存中存在结构合法的来源记录。
- fresh_mask：来源年龄小于500ms、接收后经过时间小于500ms，且本地Heartbeat状态ONLINE；当前读取为SYS_OK或短暂BUSY时才允许。
- receive_age_ms：距离CM4首次观察到该新业务编号的本地时间。
- peer_state/read_result：原Heartbeat与协议诊断，单独保留。

重复业务编号不能刷新缓存接收时刻。源任务停止更新时，CM7来源年龄超过500ms后饱和为UINT32_MAX，直到该任务再次提交；CM4同一编号过期后也保持过期，防止长期Tick回绕把旧记录变新鲜。编号MAX→0按新值处理，编号不是会话标识。

传输年龄由发送端记录年龄＋同核采样等待＋接收端本地经过时间估计，不比较两核Tick。没有跨核同步时钟，运行期处理入口到最终提交/观察之间的IRQ或调度延迟不能精确消除，fresh_mask是观察新鲜度，不是对物理采样时刻的硬实时证明。实板需记录最长调度间隔及视频负载影响。

协议错误、版本错误或持续BUSY不会替换已验证业务缓存；旧数据可仍valid，但fresh为0。一次BUSY可在期限内保留fresh，不立即误判失联。初始无有效来源时valid/fresh均0，不能把全零数据当真实运行状态。

## 修改清单

新增Common/Inc/ipc_application.h、Common/Src/ipc_application.c：正式80B载荷、固定偏移断言、验证、缓存与新鲜度。

修改Common/Inc/ipc_shared.h、ipc_status.h、ipc_core.h及Common/Src/ipc_core.c：版本3、功能位3、正式字段、每字段序号保护、CM4载荷全零规则。

修改Common/Inc/ipc_runtime.h、Common/Src/ipc_runtime.c：保留心跳机制，增加业务载荷发布/接收及同核等待补偿。

新增CM7/Application/app_status.c/.h：各来源单写者和一致汇总。

修改CM7/Application/Tasks/camera_task.c、usb_task.c、control_task.c：处理轮次结束后提交本地记录；Control附加只观测故障记录。修改ipc_task.c/.h：唯一汇总和发布、snapshot_result诊断。

修改两核Service/ipc_service.c/.h：CM7设置待发布载荷；CM4统一读取和Watch视图。MDK-ARM/h7_mac.uvprojx只加入所需源文件/头文件，保留原本机设置。

Tests/IpcV2和Tests/IpcRuntime只迁移V3字段预期、补新依赖桩和编译项，保留全部原场景及安全断言。Tests/IpcStatus新增V3/快照/真实内核测试、命令、结果及正式构建日志。Docs/Design新增V3说明并给历史V2入口加迁移提示。

用户原video_config、Keil选项、MJPEG日志及未跟踪文件全部保留，不混入本次提交。未修改CM7/CM4 main、BSP、Device、Media、HAL/LL或ST USB Core。

## 软件验收

| 套件 | 实际执行 | PASS | FAIL |
|---|---:|---:|---:|
| 原05B | 512 | 512 | 0 |
| 原05C-1 Runtime | 260 | 260 | 0 |
| 原05C-1 Task | 20 | 20 | 0 |
| 新V3/状态 | 448 | 448 | 0 |
| 扩展真实内核 | 41 | 41 | 0 |
| 合计 | 1281 | 1281 | 0 |

原05B/05C-1仍全部执行。由于正式协议升级，预期feature_flags由1变3，reserved末项由24变4，未删除测试或放宽未知保留字段规则。旧报告保留历史意义，不冒充新实板结论。

新套件覆盖每字段交错、一致汇总、初始无效、陈旧/恢复、采样后等待、Tick回绕、编号/计数回绕、字段边界、错误结果、版本/功能位、权限、共享区别名及通信异常。实际内存钩子检查临界区，真实FreeRTOS套件检查BASEPRI=0x50。

模拟器执行ARMCC5真实ARM指令，但不模拟物理NVIC抢占/SVC/PendSV异常返回，也不模拟两颗CPU真正并行的总线。新V3套件纯C端口和真实内核套件分层验证，不把测试桩等同于全机联调。

正式两核完整编译结果、程序大小与资源差异见后续构建表；源码指纹确保日志对应最终生产代码。完整USB/Camera历史套件本轮未全量运行，不能用本轮IPC测试替代。

## 实板与性能

当前本地保留的用户配置为320×240 MJPEG，目标10FPS、最大帧70000B，本任务没有修改或提交它。目标值不是实测值。Git干净检出时配置以仓库文件为准，实板应继续使用已验证的工作配置。

需要配套烧录CM7/CM4 V3后完整断电重启，MJPEG与IPC同时运行，检查CM4 fresh_mask=7以及同一snapshot_id下的数据。步骤、Watch、比对表和性能记录模板见Docs/Design/IPC_V3_ApplicationStatus.md。

本轮新增硬件结果全部未验证：数据一致性、实际FPS/丢帧、最长调度间隔、任务剩余栈、长时间稳定性及新增临界区对DCMI/UVC的影响。静态资源差异可由Map确认，不能据此声称无性能影响。不实现OLED界面或单核热复位恢复。

## 最终编译与静态资源差异

两核正式完整重编均为0 Error、0 Warning。以下单位为字节，对比基线133ffcd的同一正式Target日志：

| Target | Code增量 | RO-data增量 | RW-data增量 | ZI-data增量 |
|---|---:|---:|---:|---:|
| CM7 | +2068 | +0 | +20 | +348 |
| CM4 | +1132 | +0 | +0 | +272 |

两次构建均保留原本机视频配置，本次只提交工程新增项。共享区本身仍320B；新增RAM用于本地记录/缓存/诊断，没有新增图像缓冲或整帧复制。静态增量不代表CPU占用或实际FPS影响已经测量。实际源码指纹和命令见build_results.json。
