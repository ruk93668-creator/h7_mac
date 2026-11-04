# CM7 FreeRTOS Native Runtime：阶段00架构审计

日期：2026-11-04。仓库：ruk93668-creator/h7_mac。

基线：GitHub main `8d3741a74692ec7f16a78884d86ecfcf222260d3`。已通过 `git ls-remote origin refs/heads/main` 核对远端SHA。本轮分支：`codex/cm7-native-runtime-stage00`，不合并main。

## 1. 结论与审计边界

当前版本是“FreeRTOS承载一个裸机业务循环”，还不是Native Runtime。最主要的迁移风险是把原来依赖单线程顺序的调用拆到多个任务，却继续共用同一套状态与ISR屏蔽机制。

建议采用：**CameraTask独占相机与Media任务侧API；UsbTask独占USB/UVC任务侧API；ControlTask通过命令和确认组织业务生命周期。** ISR保留已有时限敏感的硬件处理。跨任务只传递描述信息和租借凭据，不传FreeRTOS内部类型到业务底层，不允许多个任务共同轮询同一个模块。

本轮只新增本报告。未修改生产代码、测试源码、断言、Keil配置或CM4。未重新编译、运行回归或操作实板。报告中的测试数字来自基线已有验收记录，不表示本轮重新执行通过。风险编号区分“当前限制”和“拆任务后的阻断风险”，不把未来并发问题描述成当前已复现故障。

工作区已有用户修改：`CM7/Service/usb_uvc_desc.h`（注释）、`MDK-ARM/h7_mac.uvoptx`、`MDK-ARM/h7_mac.uvprojx`（含优化选项）；`output/`、`tmp/`保留。以提交内容为设计基线，这些修改不纳入提交。已有本地AXF只用于观察初始化表顺序，不能冒充从干净提交重新构建的固件。

已确定的目标边界：

1. `CM7/Application/Tasks/`分别定义业务任务及自己的静态栈/控制块。
2. `CM7/Application/app_tasks.c`集中组织创建和启动前失败处理，不承载业务大循环。
3. `Platform/platform_rtos.c`只提供RTOS环境、调度器、Idle/Tick/故障Hook，不保存Main_Run回调或创建LegacyVideo业务任务。
4. 保留Framework Initcall的等级、链接段和fail-fast语义；最终取消CM7 Main_Run、App_Run、Framework_Run的全局运行时串行调度。
5. BSP、Device、Media、Service不引入TaskHandle_t、QueueHandle_t、TickType_t或FromISR调用；FreeRTOS业务适配在Application，通用事件契约在Common。

## 2. 已审阅的代码范围及来源

覆盖：CM7 main/异常入口、Common/Src中的SystemInit、Platform RTOS与HAL时基、Framework Init/Runtime/Event/Timer、Application Camera/Bridge/Video配置、Device注册表及OV5640、Media Snapshot/Stream、BSP Camera/DCMI/USB/SDRAM、Platform USB Device、UVC Class/Payload/Descriptor/Mode、IPC，以及相关HAL/LL和FreeRTOS Port。

所有源码位置均以 `8d3741a` 为准。文中“建议”“阶段01以后”不是现有API。未来文件名是实施建议，阶段00没有创建这些C/H文件。

主要证据索引：

| 位置 | 审计依据 |
|---|---|
| [main.c:95](../../CM7/Core/Src/main.c#L95)、:174、:187 | Main_Run顺序、Initcall和启动调度器位置 |
| [platform_rtos.c:22](../../CM7/Platform/platform_rtos.c#L22)、:87 | Platform创建LegacyVideo并执行s_run_once |
| [FreeRTOSConfig.h:12](../../CM7/Platform/FreeRTOSConfig.h#L12) | 1kHz Tick、静态分配、5档任务优先级、中断阈值 |
| [framework_init.c:9](../../CM7/Framework/framework_init.c#L9) | 按等级扫描表，首次失败返回 |
| [app_camera.c:60](../../CM7/Application/app_camera.c#L60)、:139 | Media先停、等待引用释放，再Device_Stop；启动回滚 |
| [app_uvc_bridge.c:67](../../CM7/Application/app_uvc_bridge.c#L67) | 同一个函数同时调用UVC与Media |
| [camera_capture.c:509](../../CM7/Media/Src/camera_capture.c#L509)、:606、:778、:933 | Stop/DRAINING、停止查询、ISR轮换及Get/Release |
| [bsp_dcmi.c:365](../../CM7/BSP/bsp_dcmi.c#L365)、:556、:667、:1007 | DBM启动、硬件停止、地址替换、TC/FRAME配对 |
| [usb_device.c:746](../../CM7/Platform/usb_device.c#L746)、:870、:978、:1284 | 仅屏蔽USB IRQ的锁、EP0取消、Reset、Class释放 |
| [usb_uvc_class.c:245](../../CM7/Service/usb_uvc_class.c#L245)、:330、:966 | 借帧、Process、Alt生命周期 |
| [usb_uvc_payload.c:96](../../CM7/Service/usb_uvc_payload.c#L96)、:177、:234 | Build/Complete/Abort分离 |
| [bsp_usb.c:186](../../CM7/BSP/bsp_usb.c#L186)、:335、:599、:618 | Abort和PIO所有权、WFI时钟、真实IRQ入口 |
| [framework_event.c:62](../../CM7/Framework/framework_event.c#L62)、[framework_timer.c:254](../../CM7/Framework/framework_timer.c#L254) | 当前无任务并发保护的FIFO和Timer |

外部规则只用于解释FreeRTOS机制，项目实际行为以本地代码为准：

- [FreeRTOS官方Cortex-M中断优先级说明](https://www.freertos.org/Documentation/02-Kernel/03-Supported-devices/04-Demos/ARM-Cortex/RTOS-Cortex-M3-M4)：只允许合规优先级ISR使用FromISR API。
- [FreeRTOS-Kernel V10.6.2 task.h](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/V10.6.2/include/task.h)：任务通知接口与语义。
- 本地 `portable/RVDS/ARM_CM7/r0p1/portmacro.h:103–115`：FromISR请求PendSV，临界区使用BASEPRI；不是由Application任意调用任务函数完成切换。

## 3. 当前启动顺序与问题

```text
Reset / 启动文件（MSP）
  → MPU_Config
  → CM4启动并等待其进入STOP
  → HAL_Init（CM7 HAL Tick实际由TIM6提供）
  → SystemClock_Config
  → BSP_DCMI_EarlyInit / BSP_Camera_EarlyInit / IPC_Service_EarlyInit
  → HSEM释放CM4
  → MPU_ConfigSdram
  → Framework_InitRuntime（仍在调度器启动之前）
  → Platform_RtosStart(Main_Run)
      → 创建LegacyVideo + 内核创建Idle
      → 启动调度器（任务使用PSP）
```

Initcall有等级顺序，但**同等级没有显式依赖排序**，取决于链接表布局。已只读解析现有AXF的ER_FW_INIT，观察到：

| 等级 | 当前AXF内的执行顺序 | Native迁移关注点 |
|---|---|---|
| PLATFORM | App_UvcBridgePrepareOutput | 读取默认配置、配置UVC/Bridge并选择Boot Mode；尚未启动USB |
| BUS | Device_RegisterAll → BSP_Camera_Init → BSP_DCMI_Init → BSP_SDRAM_Init → BSP_USB_Init | 总线/时钟准备；Camera与DCMI的共享GPIO预配置已经在CM4唤醒前完成 |
| DEVICE | Device_InitAll → USB_Device_Init | OV5640目前只完成板级Init；USB Core准备但尚未连接 |
| MEDIA | 当前无表项 | CameraCapture_Init实际在App_CameraPrepare内执行 |
| SERVICE | IPC_Service_Init → USB_Mode_Init | IPC置ready；USB注册唯一Class |
| APPLICATION | App_Init → UsbMode_AppStart | 当前先开启传感器和连续采集，再启动USB连接 |

本地AXF表范围为0x0800E530至0x0800E5C0，12项，每项12B；地址是本地构建观察值，不能作为未来固定布局。等级顺序由framework_init.c保证，表内相对顺序不能作为长期依赖契约。

**启动风险：**DCMI/DMA和USB IRQ在业务任务运行前就可产生事件。现在ISR只更新本地状态，不调用RTOS，因此尚能在无任务条件下处理；新增通知后，不能只因TaskHandle非NULL就从启动前ISR调用内核。创建对象、绑定接收端、调度器运行、模块准备和开放外设必须有明确顺序。

Native最终建议：保留CM4 Boot、HAL/时钟/MPU的前置顺序；main调用App_TasksCreate后由Platform启动调度器；ControlTask在其他任务等待BOOT_READY时执行Initcall。Initcall只完成配置和基础初始化，不在APPLICATION等级偷偷开启采集或连接USB。成功后由Control向CameraTask/UsbTask发启动命令，分别由所有者执行Device/Media与USB Start。失败时不发布BOOT_READY，执行能确认安全的清理并报告。

初始化期间ControlTask是明确的临时初始化权限持有者；运行期权限在BOOT_READY后交给对应任务。不能让Device_InitAll与CameraTask并发调用同一设备。Framework同等级依赖不得靠调整Keil Group碰巧解决。

## 4. 当前运行与ISR调用链

```text
LegacyVideo，任务优先级3
  Framework_Run → EventDispatch → TimerProcess
  USB_Mode_Process
  USB_UVC_Process
  App_Run → App_CameraRun → CameraCapture_Process
          → App_UvcBridgeProcess
  vTaskDelay(1)
```

该调用顺序实际承担了任务间互斥与先后关系。移除时必须逐项替代，不能只删除Main_Run再让各任务同时调用原函数。

### 4.1 DCMI/DMA快路径必须保留在ISR

```text
DMA1_Stream0 IRQ → Dcmi_CheckErrors → HAL_DMA_IRQHandler
    → M0/M1完成回调 → 检查CT → 锁存TC
DCMI IRQ → Dcmi_CheckErrors → HAL_DCMI_IRQHandler
    → FrameEventCallback → 锁存FRAME并重新使能FRAME中断
任一回调 → Dcmi_StreamTryPair
    → TC与FRAME均存在且无错误
    → 完成目标不是CT当前目标，无新增TC挂起
    → 临时授予replace_allowed
    → Media CameraStream_OnFrameComplete → CameraStream_Rotate
    → BSP_DCMI_ReplaceTarget（检查CT、NDTR、地址、错误）
    → 成功后提交Media所有权，撤销授权并清pending
```

**禁止把Rotate/ReplaceTarget改成“ISR发消息，任务收到再改地址”。** ReplaceTarget只允许当前配对回调，且要求剩余Word大于32并前后核对CT/NDTR。DMA不会因CPU屏蔽中断而暂停；任务调度等待可能直接错过替换窗口。

当前三缓冲有IN_USE时可以合法丢弃后续完成帧而不替换地址，回调仍返回SYS_OK。因此BSP回调成功不等于产生了新的READY帧。将来的事件应是“采集进展/状态可能变化”提示，CameraTask仍须调用Media检查实际可取得帧。

错误路径：DMA/DCMI ISR → Dcmi_RecordError（首次结果与错误位锁存）→ 任务侧TakeEvents/CameraCapture_Process → BSP_DCMI_Stop。**停止/等待/恢复不可放入新ISR通知回调。**

### 4.2 USB协议与发送路径

```text
OTG_FS IRQ → HAL_PCD_IRQHandler
  → HAL_PCD_SetupStageCallback → BSP回调表 → Usb_Setup
    → 官方USBD_LL_SetupStage → 标准请求或UVC_Setup
  → DataIn → BSP清理PIO传输 → Usb_DataIn记录完成序号
    → 官方Core → UVC_DataIn锁存匹配Token，进入WAIT_CONFIRM
  → SOF → Usb_Sof去重、递增序号 → 官方Core → UVC_Sof覆盖最新机会
  → ISO Incomplete → 保留硬件/PIO所有权 → UVC_RECOVERY

任务侧USB_UVC_Process
  → 处理Cancel/Recovery，确认Abort后归还
  → 匹配真实完成Token，再CompletePacket推进偏移
  → 每个新SOF最多提交一包
  → Build → 保存Token/在途序号 → Transmit
```

EP0 Setup、Reset、Disconnect、Class Init/DeInit和SET_INTERFACE目前会在USB IRQ链同步执行；UVC生命周期也会同步Close/Abort端点。不能假定它们已经在任务内，也不能将原始Setup指针或Class上下文指针直接排队后继续使用。若未来延期控制请求，需要单独设计EP0事务与响应机制；不列入阶段01。

应用适配层应在**已有协议处理及状态提交之后**发唤醒提示，不替换、不延后EP0核心处理。

## 5. 中断、临界区与任务优先级

| 来源 | 当前抢占优先级 | 现有职责 | 未来是否允许调用RTOS |
|---|---:|---|---|
| TIM6_DAC | 4 | HAL_IncTick | 禁止；高于syscall阈值 |
| DMA1_Stream0 | 5 | DMA错误、TC、同步Media轮换 | 仅末端Application适配回调可用FromISR |
| DCMI | 5 | FRAME/错误及配对 | 同上；与DMA同级不会互相抢占 |
| OTG_FS | 6 | HAL/Core/EP0/SOF/完成/生命周期 | 适配回调可用FromISR；不能阻塞 |
| SysTick / PendSV | 15 | 内核Tick和上下文切换 | 由官方Port独占 |
| SVC | 官方Port入口 | 启动首个任务 | 不改向量归属 |

configPRIO_BITS=4，NVIC分组编码3，对应4位抢占、0位子优先级。configMAX_SYSCALL_INTERRUPT_PRIORITY=0x50，即库级阈值5。数值0–4的IRQ不能调用FromISR；数值5及更大者满足优先级必要条件，但仍须确保内核已就绪、句柄有效、不在不允许的临界区操作。

任务优先级是另一套数值：建议初始UsbTask=4、CameraTask=3、ControlTask=2、Idle=0，均在configMAX_PRIORITIES=5内。这个分配是待验证建议，不能把任务优先级4与TIM6 IRQ优先级4视为相同等级。

三种保护不可混用：

| 机制 | 当前保护对象 | 审计结论 |
|---|---|---|
| PRIMASK全局屏蔽 | Media Get/Release、DCMI Start/Replace的短原子操作 | 阻止常规IRQ和PendSV执行，但DMA继续；严禁在里面等待、排队阻塞或HAL延时 |
| USB_Device_Lock | 只屏蔽OTG_FS IRQ | 保证一个任务与USB IRQ互斥；不是多任务锁 |
| FreeRTOS BASEPRI临界区 | 内核屏蔽优先级5–15 | 保留TIM6优先级4；不能机械替换所有现有PRIMASK而不复核硬件操作 |

例如：UsbTask禁用USB IRQ后被更高优先级任务抢占，后者仍能进入USB API并改上下文。s_control_busy的“检查再置位”也不是任务锁。首选单任务所有权消除交叉调用，不在底层到处塞互斥量。

## 6. 模块依赖审计

当前CM7业务代码中，实际使用FreeRTOS任务类型/API的业务适配集中于Platform/platform_rtos.c及配置文件。BSP、Device、Media、Service没有直接依赖FreeRTOS任务类型，这个边界可以保留。

但当前Application桥接函数同时调用USB与Media；从“同一任务”拆分后必须重做Application适配，不能把现有app_uvc_bridge.c原封不动放入UsbTask。

| 当前文件/状态 | Native要求 |
|---|---|
| Platform持有LegacyVideo栈、TCB、优先级、Main_Run回调 | 移到Application/Tasks；Platform只启动环境 |
| Framework_Event FIFO共享head/tail/count，无锁、无FromISR边界 | 不能直接作为多生产者跨任务总线 |
| Framework_Timer对象池/堆及Process无任务互斥 | 不能由三个任务各自Process同一实例 |
| CM7除框架定义外，没有查到正式模块创建Framework Timer或订阅/发布Framework Event的调用 | 可以取消CM7全局运行入口，但迁移时仍须复核新增用户代码 |
| CM4 OLED仍使用Framework Timer，CM4 main仍Framework_Run/App_Run | 保留CM4、Common兼容接口和相关链接配置 |
| IPC Service仅有EarlyInit及SERVICE阶段ready标记 | 不能假定已有周期心跳任务；ControlTask如新增心跳需独立需求和测试 |
| usb_mode.c在Service | 保持其模式策略职责，由UsbTask串行推进 |

公共接口建议只依赖stdint、stdbool、SysResult_t和业务公共结构体。任务句柄、队列、静态队列内存、FromISR和portYIELD_FROM_ISR仅存在Application适配/任务代码及RTOS Platform内部。底层通过注册的普通C回调通知，不包含Application头文件，也不直接引用CameraTask/UsbTask函数。

## 7. 共享状态和所有权矩阵

以下是稳态所有权；初始化在任务开放前完成并明确交接。ISR可修改的子集单列，不意味着一个任务能够独占硬件所有访问。

| 对象 | 当前修改者 | Native任务侧唯一所有者 | ISR权限 | 不允许 |
|---|---|---|---|---|
| OV5640 Device状态、配置有效标志、软件SCCB | App + Device框架 | CameraTask | 无SCCB/Device生命周期操作 | Control/Usb直接Device_Start/Stop或configure |
| s_capture的config/mode/state/last_result | App/Media | CameraTask | 仅既有回调维护Stream子对象 | UsbTask直接Media Get/Release/Stop |
| stream的DMA索引、READY/IN_USE/FREE | Media ISR +任务Get/Release | CameraTask调用任务侧API | 既有Rotate，短临界区保护交界 | 任务侧复制或重置活动Stream对象 |
| DCMI句柄、硬件目标和错误锁存 | BSP ISR及Start/Stop | 通过CameraTask调用BSP/Media | 既有TC/FRAME配对和地址替换 | Control直接重建DMA/HAL句柄 |
| USB Core/PCD/端点/固定Class池 | USB IRQ +主循环 | UsbTask | 现有HAL/Core回调 | Camera/Control调用USB生命周期API |
| UVC上下文、Payload、借帧指针 | USB IRQ +Process | UsbTask | 协议、Token/机会及Close确认 | CameraTask查询后直接提交、修改Payload |
| Mode Context | Initcall +USB_Mode_Process | UsbTask | 无直接模式管理 | Control直接RequestReconnect/Start/Stop |
| 租借账本（未来Application对象） | 当前s_bridge | Camera记录Media租出；Usb记录借用与归还待确认 | 不跨层释放 | 用completed_frames变化替代归还确认 |
| 业务配置 | 冷启动App配置 | 初始化后只读，Control管理策略 | 不修改 | 运行期热切换被默默接受 |
| g_ipc字段 | 两核各自Service | 运行期如需写M7字段，交Control适配 | 保留当前内存屏障 | 将跨核协议误当作FreeRTOS队列 |

现有volatile只限制编译器访问优化，不提供任务互斥、多字段事务或跨模块所有权。HAL锁和Device BUSY也不是完整多任务协议。

## 8. SDRAM、缓存和资源边界

当前布局：SDRAM业务窗口[0xD0000000,0xD0020000)，MPU Region2为128KiB Normal、Non-cacheable、Shareable、RW、XN。首1KiB保留。

| 区域 | 地址范围，不含结束地址 | 容量 |
|---|---|---:|
| Stream buffer 0 | [0xD0000400,0xD000A400) | 40960B |
| Stream buffer 1 | [0xD000A400,0xD0014400) | 40960B |
| Stream buffer 2 | [0xD0014400,0xD001E400) | 40960B |
| 尾部保留 | [0xD001E400,0xD0020000) | 7168B |

Snapshot复用起始地址，容量63KiB；不能与Stream同时运行。每块起始地址满足32B对齐。当前有效YUY2帧38400B，小于40KiB；通知或队列只复制CameraFrame_t/租借凭据，不复制整帧。

三缓冲实际上是“双DMA目标 + 一个待消费/被消费者持有的槽位”，不是可同时借出三帧的队列。第三槽IN_USE时允许丢帧，不得为了让UsbTask拿到新帧而覆盖IN_USE。

USB当前为PIO：CPU从SDRAM读取图像到Payload包，再拷入BSP的端点PIO暂存，最后CPU写USB FIFO。USB没有直接DMA读取SDRAM，因此“零拷贝”只描述Media到消费者传递指针，不代表USB整条链没有复制。

现有Scatter的RW_IRAM1位于DTCM 0x20000000、128KiB；任务栈/TCB/队列/USB PIO CPU缓冲可放这里，**DCMI DMA图像不得搬入DTCM**。当前CM7及实际Common/SystemInit源码未找到主动开启D-Cache/I-Cache的调用，不将此静态观察当作实板CCR读数；SDRAM Non-cacheable属性仍须保留。未来启用Cache或USB DMA属于独立任务。

## 9. CameraTask、UsbTask、ControlTask契约

### 9.1 CameraTask

- 唯一调用App_CameraPrepare/Init/InitStream/Stop及所有Media任务侧API的运行期任务；内部私有函数是否重命名留到实施时审查。
- 串行配置、Device_Start、Media Start、事件处理、故障Stop及Device_Stop。
- 既有ISR继续Rotate；任务只收唤醒并读取实际状态，不在任务中补做过期的DMA目标替换。
- 只有收到有效的取帧需求、USB可提交信用和可用READY时才取得IN_USE。
- 接受归还必须匹配当前租借凭据；只由CameraTask调用StreamReleaseFrame。
- 无自动无限重启；停止失败或仍DRAINING保持故障/占用，不伪造IDLE或READY。

### 9.2 UsbTask

- 唯一调用USB_Mode、USB Device公开控制接口及USB_UVC的运行期任务。
- 注册、Start/Stop/同模式Reconnect、UVC Process和帧借用均在此任务串行执行；EP0/HAL ISR保持既有同步协议处理。
- GetStreamStatus确认can_submit并且没有旧租借后，发一个取帧需求给CameraTask。
- 收到图像凭据后校验配置/长度/epoch，再SubmitFrame。成功只代表借用，不能立即发归还。
- 仅有效查询frame_owned=false，或在UsbTask正常环境下确认活动Class已安全销毁，才发送归还凭据；查询IO/BUSY等错误不是归还依据。
- Alt0、Reset、Disconnect及Abort失败必须持续推进和保留账本，不能先清指针或删除任务。

### 9.3 ControlTask

- 执行有屏障的Initcall引导、配置策略、启动/停止命令和故障汇总。
- 不调用Media/UVC数据面API，不拿CameraFrame_t去处理像素；不直接改Device.state。
- 发异步命令并等待对应ACK；等待时阻塞自身，不阻塞CameraTask/UsbTask。
- 超时只改变业务事务结果和告警，不意味着硬件已停止或图像可回收。
- 运行期修改分辨率/格式/输出配置继续返回NOT_SUPPORTED，不把Native迁移变成热切换开发。

## 10. 公共事件与Application适配设计（待实施）

建议新增 `Common/Inc/runtime_event.h` 定义普通C通知契约及事件编号；`Application/app_events.c/.h`绑定接收者并映射到FreeRTOS。任务代码位于Application/Tasks，Platform不反向依赖Application。

必须区分两种信息：

| 类别 | 内容 | 可否合并 | 存储与满载策略 |
|---|---|---|---|
| 唤醒提示 | CAMERA_PROGRESS、USB_SOF、USB_TX_PROGRESS、USB_STATE_CHANGED | 可，保留最新状态 | 通知位/粘滞状态；醒来后读取真实状态，不累计SOF发送额度 |
| 可靠事务 | Start/Stop/Cancel命令、FrameOffer、FrameReturn、ACK | 不可任意覆盖或丢失 | 静态队列或持久单槽；满时返回BUSY/保留待发送记录并重试 |

普通C接口可区分Notify与NotifyFromIsr，参数中不含FreeRTOS类型。Application后端负责在任务/ISR上下文调用对应内核接口；FromISR汇总higherPriorityTaskWoken，在快路径结束后请求PendSV。底层通知回调不得阻塞、调用Device或等待消费者。

需要额外区分：Dcmi_RecordError既可由ISR调用，也可在Start/Stop的任务侧临界区调用；函数名带Error不代表当前处于ISR。不能给它无条件绑定FromISR发送。任务侧操作可通过返回值由所有者处理，恢复中断后再发普通通知；ISR路径才使用ISR适配。进入PRIMASK临界区后不得调用会阻塞的队列操作。阶段01必须覆盖同一来源的任务/ISR两种通知上下文。

绑定对象必须在开放IRQ前完成并保持静态生命周期。调度器尚未运行时只锁存模块状态或保持外设不启动；不得从启动前ISR通知无效任务。优先级4及更高的IRQ不走RTOS后端。通知失败必须保留错误/待处理状态，不允许故障静默丢失。

建议接入点：

- Camera：TC/FRAME处理完成、Media轮换提交后，或BSP错误位锁存后，通知CameraTask检查状态。回调成功但丢帧时允许合并为一次PROGRESS。
- USB：SOF序号更新、DataIn Token提交、Incomplete/Reset/Disconnect状态提交，以及Commit/Alt改变可提交条件后，通知UsbTask。只通知，不在回调中完成桥接。
- Control：模块状态ACK/故障的持久消息；不是在每个1ms SOF上唤醒ControlTask。

正确的等待必须防止“读到无工作 → 清掉刚到的通知 → 永久等待”。使用不会在等待入口清掉新到提示的通知方式，并在处理循环中重新检查持久工作条件。SOF可以合并，帧归还不能用可丢失的通知值承载全部凭据。

现有Framework_EventPost并不满足上述契约：只有id+uint32 data，head/tail/count无并发保护，也没有可靠归还/满载重试协议。不能只把任务句柄塞入data来绕过分层。

## 11. 帧租借协议与停止协议

### 11.1 跨任务图像凭据

现有CameraFrame_t只有格式、尺寸、指针和长度，无Stream epoch、租借编号或物理Buffer Handle。Media的StreamReleaseFrame也没有Handle参数，释放的是当前consumer_index。

因此Application适配层必须增加自己的有界账本，例如：`stream_epoch + lease_id + CameraFrame_t`。它是跨任务租借凭据，**不是UVC包Token**。包Token、Core IN sequence、连接epoch、Stream epoch和租借ID不能互相代替。

首版限制全系统最多一个对USB借出的帧；CameraTask保留租借记录，即使FrameOffer已经在队列中，也视为尚未归还。UsbTask不直接调用Media Release。

```text
UsbTask：can_submit=true，无旧帧
  → FrameRequest(request_id)
CameraTask：取到Media IN_USE
  → 保存(epoch, lease_id)，发送FrameOffer
UsbTask：保存凭据 → SubmitFrame
  ├─ 失败：新帧未被UVC借用 → FrameReturn
  └─ 成功：保持借用 → Process/状态查询证明归还 → FrameReturn
CameraTask：核对epoch/lease → StreamReleaseFrame成功 → ReturnAck
UsbTask：收到匹配ACK → 清除凭据，允许下一次FrameRequest
```

补充约束：

1. FrameOffer队列发送失败时，CameraTask仍拥有租借并负责安全归还/重试，不可遗忘IN_USE。
2. Return队列满或ACK丢失时保留本地待发送状态；重复Return可重发ACK，但不能第二次释放Media。
3. 旧epoch/旧lease的Return不得释放新帧；记录最近已完成凭据用于幂等确认。
4. 连接Reset不自动使Media帧失效；旧租借仍需完整归还。Stream epoch只在旧引用完全清零后推进；计数溢出不得静默复用。
5. 主机未Commit/Alt1时不主动占用Media，避免第三槽长期IN_USE；已排队Offer则必须按协议收回。
6. Cancel返回SYS_OK只是登记请求；completed_frames增加、busy=false、超时都不能单独授权归还。

### 11.2 正常主动停止

ControlTask进入STOPPING并冻结新的取帧需求 → 命令UsbTask取消当前借帧 → UsbTask继续Process直至确认不再访问帧 → CameraTask收到并确认FrameReturn → CameraTask调用App_CameraStop → Media/DCMI/DMA安全停止且无引用 → Device_Stop → 报告成功。

Control不能先等待Camera Stop成功再通知USB取消，否则Media因IN_USE返回BUSY，USB又等控制端，形成循环等待。停止事务期间两个所有者任务继续消费归还和故障消息，不能相互同步阻塞。

### 11.3 故障停止

DCMI错误时CameraTask先沿现有Media路径停止硬件；有IN_USE则进入DRAINING，保持传感器运行状态等待归还。与此同时请求UsbTask取消；Abort失败必须继续保留借用并报告，不强制回收。收到归还、Media完整退出后才Device_Stop。

USB发生Reset/Disconnect时，既有Close/DeInit确认归还后通知UsbTask处理租借；安全销毁Class不等于跨任务ReturnAck已完成。若硬件停止失败、Class仍存活或所有权不明，保持RECOVERY和持有，不靠Control超时抹掉状态。

当前Device框架Start/Stop失败会进入ERROR，Device_Stop只接受RUNNING，没有通用ERROR恢复入口。Native首版应报告并阻止重启；恢复策略需要独立设计，不得由任务直接写READY。

## 12. 时间、阻塞和等待策略

| 机制 | 当前行为 | Native契约 |
|---|---|---|
| 内核Tick | SysTick 1kHz | 仅内核调度等待，ms用pdMS_TO_TICKS转换；小非零时限不能被换算为0 |
| HAL/Platform时间 | TIM6优先级4，Platform_TimeGetMs→HAL_GetTick | 保留为驱动/协议时间基准；不要把RTOS Tick数直接当毫秒 |
| Snapshot超时 | 100ms，仅Process时检查 | CameraTask等待时带最近截止时间，不能无限等帧通知 |
| 连续采集 | 当前没有无帧超时 | 无帧看门狗是未解决策略，不能默认已存在 |
| DCMI Stop | 硬件停止检查10ms，另含HAL停止/恢复成本 | 只在任务上下文执行；10ms不是整个Stop的已测最坏时长 |
| USB Mode枚举 | 5000ms | UsbTask有截止时间唤醒，即使没有新IRQ也推进超时 |
| USB同模式重连 | 断开等待500ms | 定时等待后推进，不能依赖SOF；断开期间本来没有SOF |
| UVC首包节奏 | Commit帧间隔，当前25FPS目标为40ms | 继续按单调SOF序号及既有wrap比较；SOF不积累信用 |
| UVC IN_FLIGHT | 依赖DataIn/Incomplete/生命周期事件，未见独立任务侧完成超时 | 不能宣称丢回调后一定自动归还；检测/取消策略待定义 |
| UVC RECOVERY | 每次Process可重试Abort | 必须有有界重试唤醒/退避；等不到IRQ时也能继续，不能高优先级无限忙等 |
| SCCB/组件配置 | DWT位延时、组件毫秒轮询 | 只在CameraTask或独占初始化阶段；不能长时间全局关中断保证软件IIC |

现有USB EP Abort/Flush并非全部由HAL Tick计时：本地LL `USB_EPStopXfer` 在约第915行使用循环计数上限10000；FIFO Flush也是计数式等待。不能把这些上限直接说成10ms，也不能靠提高Tick优先级保证其最坏时长。

25FPS是配置值。当前1023B包扣2B头后承载1021B图像，38400B需要38包：前37包1023B，末包625B。每SOF最多一包时，理想发送机会占用38ms，40ms周期只剩约2ms调度余量。新的任务通知和调度延迟必须实测，不能从CPU频率或软件PASS推断实测25FPS。

## 13. ISR时限、睡眠和内存预算

DCMI/DMA优先级5高于USB6；TIM6的4可抢占两者。USB_UVC_Process目前整个构包、PIO提交及部分恢复过程都位于USB IRQ屏蔽区；1021B像素复制及1023B BSP暂存复制不会屏蔽DCMI，但会延迟USB IRQ。USB IRQ内同步Close/Abort也可能运行有界轮询，必须测量最长时间。Native迁移不能在这些区域里加入队列阻塞或大段日志。

保留已经修复的WFI配置：USB2控制器Sleep时钟使能、ULPI Sleep时钟使能关闭；IdleHook的WFI不搬入业务任务。configUSE_TICKLESS_IDLE=0不代表自定义IdleHook没有睡眠。任务拆分可能增加空闲占比，必须复测WFI枚举/取流，不推断内部“时钟冲突”机制。

初始静态资源预算建议：Camera栈4KiB、USB栈8KiB、Control栈3KiB、Idle沿用1KiB，合计16KiB。相比旧业务8KiB+Idle1KiB，多7KiB栈，再计新增TCB、队列及账本。它只是启动预算，最终按链接Map、栈水位、异常嵌套和最坏路径校准，不能直接宣布足够。

建议命令队列Camera/USB各8项、Control 16项，消息固定32B时约1024B队列存储；FrameOffer和FrameReturn各1个持久槽、带ACK及重试；实际消息sizeof必须编译断言，不能靠估计分配。内核仍使用静态分配，不添加heap_x.c，不使用标准malloc。当前UVC的USBD_malloc仍映射到1024B固定Class池。

现有USB BSP的每端点PIO槽按32B对齐后为1056B，9槽共9504B；Payload包存储1024B；这些资源不能在新任务中再次复制一套。现有MSP为4KiB，任务PSP分别配置。高水位是历史最小剩余Word，不是当前瞬时余量。

## 14. 风险清单及严重程度

P0表示进入多任务实施前必须明确和验证的阻断项，不表示全部在当前单任务版本已复现。

| ID | 等级 | 类型与证据 | 必须满足的处理/验收 |
|---|---|---|---|
| R01 | P0 | 迁移风险：USB锁仅屏蔽IRQ，usb_device.c:746；control_busy非原子任务互斥 | UsbTask为唯一任务调用者；并发注入不能出现第二个任务进入USB控制路径 |
| R02 | P0 | 迁移风险：Bridge直接同时调用Media/UVC，app_uvc_bridge.c:88、:114、:166 | 拆成任务消息及有确认的租借，UsbTask不得直接Media Get/Release |
| R03 | P0 | 硬实时边界：ReplaceTarget依赖临时授权/CT/NDTR，bsp_dcmi.c:693、:1007 | 保留ISR轮换；任务通知只能在关键提交后；延迟注入无错帧交付 |
| R04 | P0 | 当前接口限制：CameraFrame_t及Release无epoch/handle；跨任务后旧归还可能释放新帧 | Application有界租借账本、幂等ACK、旧凭据拒绝；队列满不能丢归还 |
| R05 | P0 | 启动风险：APP Initcall开采集/连接，早于scheduler，main.c:175–187 | readiness屏障及单一启动入口；创建失败/Initcall失败不开放依赖IRQ |
| R06 | P1 | 现有同步行为：USB Setup/Reset/DeInit可在IRQ中Close/Abort | 只加末端唤醒；保持EP0协议与所有权，测量ISR最长时间 |
| R07 | P1 | 迁移风险：Device Registry/BUSY、OV配置标志依赖串行调用 | Device和SCCB只归CameraTask；生命周期重入仍正确拒绝 |
| R08 | P1 | 唤醒缺口：当前没有Native事件后端；连续帧成功还可能是合法丢帧 | progress作为提示；真实READY由Media查询；错误锁存不丢失 |
| R09 | P1 | 框架限制：Event FIFO/Timer缺少多任务保护 | 不直接复用为跨任务总线；CM7全局轮询退出，CM4保持 |
| R10 | P1 | 超时风险：Snapshot/Mode/重连依赖Process，RECOVERY可能无后续IRQ | 每个任务有最近截止时间/有界恢复唤醒；无IRQ时仍推进 |
| R11 | P1 | 当前策略缺口：Stream无无帧看门狗，UVC IN_FLIGHT无独立任务超时 | 记录为待决策，不用超时授权释放；故障测试保留所有权 |
| R12 | P1 | 实时风险：USB构包屏蔽USB IRQ，Close/Abort轮询；25FPS余量有限 | 实测IRQ屏蔽、SOF到提交、DMA替换窗口、帧率/丢帧 |
| R13 | P1 | 内存风险：三缓冲只有一帧可借；DMA1不可用任务DTCM代替SDRAM | 保持128KiB MPU/40KiB槽/单租借，Map与范围检查 |
| R14 | P1 | 已知测试债务：DcmiReplaceTarget 45项基线失败 | 单独修正过时FIFO断言的任务与审批；本轮不改断言，不宣布全绿 |
| R15 | P2 | 文档漂移：bsp_dcmi.h:55及.c:364仍写仅检查/NOT_SUPPORTED；payload.h:64仍写最大512 | 后续单独更正注释，判断以真实实现为准，本轮不改代码 |
| R16 | P1 | 状态语义：USB Mode RUNNING只检查fatal result，不因IsConfigured=false必然退出 | Control不能把Mode RUNNING当成可发送；UsbTask以can_submit及生命周期为准 |
| R17 | P1 | 恢复限制：Device Start/Stop失败进入ERROR，当前无通用ERROR恢复入口 | 上报、禁止重启，不直接写READY；恢复另立需求 |
| R18 | P2 | 静态任务栈和队列预算尚未验证 | 栈水位、FPU上下文、MSP嵌套、中断负载及RAM Map验收 |
| R19 | P2 | 长运行边界：Payload token及IN sequence到UINT32_MAX后拒绝复用 | 专项测饱和和受控维护策略；不能Reset清零旧Token掩盖问题 |

R14已有证据：Tests/Ov5640Lifecycle报告在未修改main和生命周期版本都为88/133，45个失败ID/行号完全一致，首个断言仍要求DMA_FIFOMODE_DISABLE。不能通过修改统计、删除用例或放宽所有安全断言“修复”。这不阻止本轮文档交付，但阻止宣称下一阶段全量验收已全绿。

R19在当前25FPS、每帧38包且持续成功发送的理想条件下，约950包/秒，32位计数约52天达到上限；这只是容量推算，实际速率与寿命需测量，不是实板持续运行结果。

## 15. 分阶段实施文件清单与验收闸门

所有阶段都应独立审查、逐步回归；不能在一次任务里同时重写HAL、Media轮换和UVC协议。下面文件仅列候选范围，执行前再依据最新main复核。

| 阶段 | 候选新增/修改文件 | 目标与禁止越界 | 验收标准 |
|---|---|---|---|
| 00，本轮 | Docs/Architecture/本报告 | 静态架构审计，生产/断言零改动 | 基线SHA、证据引用、职责/通信/风险和后续计划完整；仅文档提交 |
| 01，Native骨架/启动 | 新增Application/app_tasks.c/.h、Tasks/camera_task.c/.h、usb_task.c/.h、control_task.c/.h、app_events.c/.h；Common/Inc/runtime_event.h；修改platform_rtos.c/.h、FreeRTOSConfig.h、main.c、app_main.c、usb_mode.c中的自动Start表项及Keil Group | 任务在Application创建，Platform不知业务；保留Initcall、门控业务启动；不改DMA轮换/Probe/Commit | 静态创建失败注入；无IRQ通知无效句柄；BOOT_READY前无业务启动；任务循环可阻塞；Native TEST枚举；双核编译 |
| 02，Camera所有者 | Tasks/camera_task、app_camera、app_events；必要时Media或BSP增加普通C进展通知入口 | CameraTask所有权；保留ISR Rotate/Replace；Device状态仍归框架 | Snapshot/Stream、DRAINING、故障Stop、地址切换窗口及旧引用测试；合法丢帧不当错误；Camera接口无跨任务调用 |
| 03，USB所有者与租借 | Tasks/usb_task、app_events、app_uvc_bridge适配拆分；必要时usb_device/usb_uvc_class增加来源无关唤醒回调 | 唯一UsbTask推进Mode/UVC；通用通知不引入相机；不改描述符/34B协议 | 原USB204、EP0竞态4及现有UVC测试保留；SOF合并/Token匹配/Abort所有权正确；消息满/旧归还/重复ACK测试 |
| 04，Control协调/退出轮询 | Tasks/control_task、app_tasks、app_main、main、Framework构建选择 | Control命令事务；CM7不再调用Main_Run/App_Run/Framework_Run；CM4继续原框架 | 正常/故障停止完整握手；无循环等待；FrameReturn不丢；全局运行入口零引用；BOOT/RESET/STOP不会重复Start |
| 05，整体验收 | 新增Tests/NativeRuntime相关测试与报告，必要的诊断接口 | 不靠改断言掩盖缺陷，不同时加业务功能 | 软件全量明确计数，历史失败单列闭环；CM7 UVC/TEST、CM4全重编0/0；实板时序/睡眠/压力验证 |

Phase01与02/03联动点不能靠临时跨层调用顶替：阶段01可先用Native TEST模式和任务握手验证骨架，相机/UVC桥接暂不启用；现有已验证运行路径可作为独立回归构建保留。若保留迁移开关，两条路径必须编译期互斥，不能同时运行Legacy与Native任务。最终完成04后，正式CM7路径取消Legacy全局轮询，不把过渡开关当作完成Native迁移。

阶段01若要求保持真实相机UVC完整业务同时切到Native，必须连同02/03中的最小所有权契约一起实施，不能先让三个任务直接轮询旧模块。

## 16. 验收矩阵：软件、编译与实板分别记录

### 16.1 软件与静态依赖

- 扫描BSP/Device/Media/Service/Common，禁止新增FreeRTOS头、任务句柄与FromISR依赖；Application任务层及RTOS Platform为白名单。
- 确认业务任务定义在Tasks，创建仅由app_tasks组织；Platform无LegacyVideo、s_run_once、业务优先级/栈。
- 确认CM7无Main_Run/App_Run/Framework_Run的活动全局运行链；Initcall段、六等级顺序、fail-fast及CM4构建保持。
- 任务创建失败、Initcall失败、通知未绑定/内核未运行、开机早到IRQ、重复Start/Stop均有故障注入。
- 通知合并、消息队列满、先到后等、ACK丢失、重复归还、旧epoch、类池地址复用、取消后迟到IRQ均不丢所有权。
- 构包/Submit/Complete明确分离；同一SOF最多一次；Incomplete不是完成；Abort失败不释放；Release失败保留账本。
- Camera ISR保留CT/NDTR窗口检查；正常IN_USE背压允许丢帧，绝不覆盖借出的物理槽。
- 超时触发只能请求取消/恢复，不能强制释放；断开无SOF时恢复和500ms重连仍可推进。

已有相关套件：CameraAppInit、Ov5640Lifecycle、CameraContextRefactor、CameraStreamState、DcmiReplaceTarget、UvcBridge、VideoConfig、UsbCoreDecoupling、Ep0IrqRace、UvcStreaming/UVC生命周期与Payload、FreeRtosP0。部分历史套件曾使用512B/10FPS假设，执行前需识别正式能力变化并逐项解释，不能批量替换断言或丢弃用例。新任务测试应真实使用内核调度/异常或明确模拟边界；旧内核测试绕过首个SVC，不能证明多任务硬件上下文切换。

### 16.2 正式编译

阶段01起：ARMCC5正式CM7 UVC、CM7 TEST、CM4完整重编，分别记录Error/Warning、Code/RO/RW/ZI、任务栈/队列/TCB和Scatter放置。检查官方HAL/LL、USB Core、FreeRTOS Port哈希及CM4源代码未意外变化。测试与UVC配置要恢复到明确的默认配置，不靠本机未提交选项获得假通过。

### 16.3 实板闸门（阶段00未执行）

1. 真实SVC/PendSV、PSP/MSP、FPU上下文、任务切换和各任务栈水位；空闲任务持续有机会运行。
2. 保留WFI的USB枚举、反复插拔、Reset/Alt切换；睡眠时钟位与电源拓扑保持记录。
3. DCMI TC/FRAME、DMA替换前后CT/NDTR、最长全局关中断、USB IRQ屏蔽时长；用DWT/示波器/跟踪记录，不在高频ISR打印。
4. SOF到UsbTask唤醒/提交延迟、成功帧率、丢帧、ISO Incomplete、DMA FE/TE/DME/OVR计数；25FPS配置不能充当实测结果。
5. 持续预览至少30分钟；反复停止/启动、USB断连、消费者长时间持帧、Abort失败与停止失败，验证无死锁/越权释放。
6. CM4 OLED与IPC、相机XCLK、SDRAM时序不退化；不因移植RTOS再次配置共享GPIO或PLL。

## 17. 未解决问题与阶段01建议

未解决：

- 公共事件的具体函数签名、后端绑定和满载反馈尚未实现。
- 租借epoch/lease、ACK重试、命令容量和停止事务截止时间尚未落成；现有Bridge不能直接多任务化。
- 现有USB锁/Device状态保护不能被宣称任务安全；应由所有权约束解决，确需多调用者时另审任务互斥。
- Stream无帧检测、UVC在途无回调检测、Abort重试频率及Device ERROR恢复策略未确定。
- 各任务实际WCET、栈需求、SOF余量和IRQ负载尚无Native实测。
- 45项历史DCMI断言失败尚未处理；不能写成“所有原回归已通过”。
- 当前头文件的旧注释与实现不符；本轮仅指出，未修改。

**阶段01建议：先完成静态任务骨架、公共通知适配和启动屏障，以TEST模式验证控制面。** 具体交付应限定：

1. main保留硬件/双核Boot，App_TasksCreate创建对象，Platform_RtosStart仅启动调度器；任何创建失败都不启动外设。
2. ControlTask独占一次Initcall引导；CameraTask、UsbTask等待BOOT_READY。修改已有自动Start入口，确保APP等级不再早开采集/USB。
3. 唤醒/命令接口先通过模拟任务和故障注入验证，不接入帧桥接；实现来源无关的普通C绑定，不把FreeRTOS类型下沉。
4. UsbTask承担TEST模式Init后Start和模式处理，使用有截止时间的阻塞等待；CameraTask先完成命令/确认骨架，不擅自变成第二个旧轮询任务。
5. 提交依赖扫描、创建/初始化失败用例、通知丢失竞争用例及三种正式构建记录。通过后再分别实施Camera数据面和UVC租借协议。

阶段00允许进入阶段01的设计准备，但不代表Native Runtime已实现、线程安全已验收或真实视频性能已通过。

## 18. 本轮交付检查

- 远端main与基线SHA已核对；独立分支，不提交/合并到main。
- 新增文件仅本Markdown，未改生产或测试断言。
- 对所有原受Git跟踪文件做前后SHA256对照，排除本报告后必须完全一致；用户已有修改保持原字节。
- 本轮没有重跑软件测试或Keil构建；没有新实板验证声明。
- 仅推送审计分支，后续实施需独立任务和审查。
