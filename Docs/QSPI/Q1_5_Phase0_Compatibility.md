# Q1.5 Phase 0-R1 兼容性与依赖矩阵

冻结主线为 `62b42d7c0b7df9eadafa00953ee8ce1229da9826`，QSPI 输入为 `5bdd963862431b490f7e12b21f94b8e7f19ac9e9`。本矩阵替代旧 Application 底座的 Phase 0 版本。仅源码审核，不移植、构建或运行。V3 引用均指冻结主线；当前 QSPI 工作树仍保留旧源码，不能把其中同名旧文件当作 V3 证据。完整差异与逐文件记录见 [R1 报告](Q1_5_Phase0_R1_Review.md)及 [机器清单](Phase0R1/source_diff_audit.json)。

## 启动、任务和队列边界

|对象|冻结V3事实|QSPI集成边界|
|---|---|---|
|创建入口|App_TasksCreate 依次准备事件、USB_MODE_NONE、消息池，再创建三个服务、Control 和 IPC，共五个正式任务|QSPI 产品仅提供 BSP，不增加第六个 RuntimeTarget/Service/队列/监督组件；独立测试任务只在测试目标分配，不能计入 created_tasks。见 [app_tasks.c](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/ApplicationV3/Core/app_tasks.c#L17)|
|Framework|App_Init 注册 APPLICATION 级，只准备视频/存储只读配置及 Record 槽；main 的框架初始化、任务准备和启动调度路径沿用|不在 Initcall 阻塞识别 Flash，不影响 SDRAM/相机设备准备。只允许测试专用 Initcall 做静态任务创建，线程进入后先等待。见 [app_main.c](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/ApplicationV3/Core/app_main.c#L11)|
|BOOT屏障|Control 先取得启动队列并订阅，再调用 App_TasksReleaseBoot；App_Events 校验唯一 Control 句柄并一次通知五个目标|测试任务不能调用 ReleaseBoot，不能 App_EventsAttach/Wait 借用任何业务身份，不添加通知位。见 [Control](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/ApplicationV3/Tasks/service_tasks.c#L464)、[屏障](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/ApplicationV3/Core/app_events.c#L130)|
|全部服务就绪|服务只在第一次 BOOT 后准备实例、报告启动结果；Control 验证 Record、服务身份、重复及截止时间，发布后 AppV3_BusinessReady 才为真；启动上限1000ms|测试线程只读 BusinessReady，BOOT通知与业务就绪不得混同。必须先经过独立运行授权和真实就绪门禁再调用 BSP，失败/超时记录并停住。见 [服务线程](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/ApplicationV3/Tasks/service_tasks.c#L29)、[启动汇总](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/ApplicationV3/Core/app_services.c#L194)|
|Runtime队列|9个业务槽各4项，3个记录信号槽；句柄代次、使用计数、订阅关系共同防止回收竞争；队列可读通知仅提示，事件分发共用4回调预算|QSPI 测试不占用池槽、服务邮箱、Record 或 PubSub，不改变消息容量/大小。采用自身静态报告和门闩，读缓冲内部独占。见 [queue_pool.c](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/ApplicationV3/Runtime/queue_pool.c#L6)、[事件循环](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/ApplicationV3/Runtime/service_event_loop.c#L95)|
|Control/Supervisor|Set/Query/Recover/Read 都校验 Control 所有权；启动后 Control 排空业务结果并推进依赖计划，业务回调要求短时非阻塞|QSPI 不进入 Supervisor 表，不使用 APP_V3_PIPE 位，不在 Control 或回调做 HAL 轮询。独立测试任务连 Supervisor_Read 也不能调用。见 [Supervisor](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/ApplicationV3/Tasks/service_tasks.c#L324)、[回调约束](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/ApplicationV3/Runtime/service_events.h#L80)|
|服务请求与数据|命令16B，AppMessage52B，IPC应用载荷80B；服务邮箱按requester+command_id校验，深度4、终结背压保留；帧通道检查方向与发送者|测试任务不冒用 Camera/USB/Storage/Control requester，不直接投帧/归还消息，不更改ABI。见 [消息路由](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/ApplicationV3/Core/app_messages.c#L80)、[邮箱](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/ApplicationV3/Runtime/service_mailbox.h#L14)|
|业务激活|服务发布READY不代表开始采集/UVC/录制；BOOT后不自动启流，控制计划由Control推进|A中的空闲V3+QSPI运行只证明空闲共存。活跃业务需要已批准的Control入口或独立既有软件夹具；当前不新增IPC协议、不插入生产任务测试代码。见 [Supervisor计划](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/ApplicationV3/Tasks/service_tasks.c#L293)|

## 调度、实时性与栈

|正式线程|V3入口和实际静态配置|同步QSPI影响|
|---|---|---|
|Camera|AppV3_CameraRun → AppV3_ServiceRun；1536 words、优先级3，栈属app_services.c|不得在Frame Acquire/Release、DMA轮换、控制启停期间夹入QSPI。排队延迟会推迟向USB/SD发帧及回收，不能仅以中断仍可抢占推定安全|
|USB|AppV3_UsbRun → AppV3_ServiceRun；2048 words、优先级4|10ms硬件进度复查、Reader Proof确认退出后才归还帧；同步QSPI不得放入服务推进或USB回调，否则会拉长Reader占用和取消窗口|
|Storage|AppV3_StorageRun → AppV3_ServiceRun；2048 words、优先级1|独占FatFS/AVI和Storage Lease；与优先级1测试线程轮转仍有CPU/总线竞争，须测写入长尾、队列背压和帧占用，不能只测平均FPS|
|Control|AppV3_ControlRun；768 words、优先级2，栈属service_tasks.c|启动期限1s、轮询100ms，Init/Recover最坏5.2s不可放入控制任务；不占用同步业务回调预算|
|IPC|IpcTask_Run；512 words、优先级1|原Heartbeat和80B应用快照发布保持；同级测试轮询不得饿死心跳。QSPI诊断不塞入现有IPC布局|
|测试目标独立线程|建议另分配StaticTask_t和768 words栈、优先级1；每次256B、总预算20ms、读间隔至少10ms|上述为待A验证的试验参数，生产五个任务栈/优先级均不改。测试线程栈水位用自身句柄采样，禁止借Control身份调用App_EventsReadStackWatermarks；任一实测水位/期限不满足则停测复审|

配置依据为 [服务清单](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/ApplicationV3/Core/app_services.c#L25)、[Control创建](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/ApplicationV3/Tasks/service_tasks.c#L541)、[IPC](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/ApplicationV3/Tasks/ipc_task.c#L10)、[USB推进](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/ApplicationV3/Services/USB/usb_service.c#L570)、[栈查询权限](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/ApplicationV3/Core/app_events.c#L282)。

内核仍为1kHz、抢占/同级轮转、静态分配、无Mutex/软件Timer，MAX_SYSCALL中断优先级5。TIM6=4、DCMI/DMA=5、USB=6、SDMMC=7不变。BSP仅短临界区取放操作权，HAL轮询持续消耗CPU；Init/Recover的5200ms预算及固定Tick探测不构成实板实时保证。Read公开预算最大60000ms、内部256B分段共享总期限，不能给每段重置预算。测试的20ms总预算并非严格墙钟保证，HAL Tick必须持续工作。ISR、屏蔽TIM6的BASEPRI、PRIMASK/FAULTMASK、调度锁或业务临界区内均不得调用。

## 外设、内存和工程

|对象|新主线复核|集成结论|
|---|---|---|
|QSPI六脚|PF6–10/PG6；SDRAM仍使用PF0–5/11–15和PG0–2/4/5/8/15；DCMI仍使用PC6/7、PG10、PH12、PE4–6、PB6/7、PA4/6|无直接重叠。保存恢复仅限所属位，共享GPIOF/G端口门与寄存器不能全量复位。源级结论需共存和电气证据支持|
|相机电源与I2C|新bsp_camera改为I2C1：PB9 SDA、PB8 SCL；新增PA15 DVDD、PH13 PWDN，保留PH15 RESET、PA8 XCLK；D2PCLK1为120MHz|旧矩阵的软SCCB描述失效。无QSPI脚位重叠；不改PLL或I2C mux，不在相机供电/初始化回调中访问QSPI。见 [bsp_camera.c](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/CM7/BSP/bsp_camera.c#L9)|
|时钟|main.c、SDRAM/DCMI/SDMMC/USB BSP时钟配方与旧main逐字节一致；HCLK240MHz，SDMMC/相机PLL1Q，FMC PLL2R，USB HSI48|QSPI D1HCLK分频2得配置120MHz，不改共享PLL。不把旧H2的实际单次通信外推为V3共存稳定性|
|SD和USB|StorageFS/sd_block未变；USB新增一致链路快照，Reader Proof记录accepted/retired及首错|测试不调用SD内部句柄、不改Framebuffer/帧指针，不用QSPI清理证明USB Reader已退出。两类FAULT/Recover严格独立|
|帧内存|Raw七缓冲不变，新增generation/lease_id/consumer验证与失败轮换锁存；JPEG FIFO新增消费者位及通用Acquire/Release；AVI索引上限由4096增至65536项（1MiB）|不新增QSPI SDRAM区域、不借CameraFrame或Lease缓冲、不改MPU/链接地址。同步CPU竞争可能延迟归还，须回归MediaLease和USB/SD联合路径|
|MPU/核心启动|main.c与CM4全树未变；SRAM4共享4KiB、SDRAM非缓存8MiB窗口、HSEM0启动握手及双核协议沿用|QSPI不Memory Mapped/XIP/DMA，不复用HSEM0作事务锁。M7独占QUADSPI，BSP原子权不保护另一核，CM4不触碰QSPI资源|
|FAULT/Cleanup/Recover|沿用QSPI输入契约，清理失败保留所有权，恢复仅显式检查资源/ID/状态；写历史风险拒绝恢复|不重构或移植本轮BSP；未来测试一次故障后静止，不重试Init/Recover，不联动Supervisor或其他设备复位|
|Keil|新main删除旧Application登记，共有36份V3生产文件，工程登记其中35份并使用Core Include路径；CM4有效C/ASM集合与TargetOption未变，Scatter、两核布局等差异见机器清单|仅从新main工程增加CM7 HAL QSPI/BSP登记。不能覆盖为旧QSPI工程，不能带入m7_project_test/H1/H2测试入口|
|产品边界|V3已经在冻结main内，OLED/Ethernet/QSPI其他分支均不作为该冻结输入的一部分|QSPI产品仅为BSP，不增Service/队列/Supervisor组件；不主动合入其他分支|

本节GPIO/时钟等未变项的文件级身份与Keil语义对照见 [全量审计](Phase0R1/source_diff_audit.json)。新低层变化的逐项影响见 [R1差异报告](Q1_5_Phase0_R1_Review.md)。保留H1/H2历史证据和用户取消旧MCU备份的事实。当前板上最后下载仍为H2是上一轮用户确认，本轮未连接板卡确认。
