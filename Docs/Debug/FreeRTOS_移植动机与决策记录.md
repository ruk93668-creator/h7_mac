# STM32H747 从裸机主循环迁移至 FreeRTOS：动机与架构决策记录

> 类型：架构决策 / 迁移前调试基线（不是故障已复现报告）  
> 项目：STM32H747 + OV5640 多接口 AI 智能视觉相机  
> 编写日期：2026-11-02  
> 代码基线：`main@2873d2cf1de0be457329622a2625fcc6608f675a`  
> 状态：**2026-11-03已启动CM7 P0最小内核移植；软件验证记录见 [移植说明](../FreeRTOS_P0移植说明.md)，实板闸门尚未验收**
> 适用范围：先 CM7，CM4 暂保留现有裸机 OLED/IPC 架构  
> 与开发计划的关系：P0 → P1 JPEG/MJPEG → P2 OLED/IPC → P8 Ethernet → P3 AI → P4–P7 → P9 Wi-Fi


## 面试阅读提示：这篇讲决策，不是假装已经解决的Bug

本篇仅保留当时的架构动机、方案取舍和迁移闸门，不复述具体故障排查。软件通过、架构选择和实板结果是三件事。

### 面试60秒复述

> 我选择引入FreeRTOS，不是因为裸机不能工作，而是后面AI、网络和存储会带来较长或不可预测的工作。为了减少迁移变量，先用一个静态Legacy任务保留原处理顺序，没有同时拆Camera、Media和USB。SysTick交给内核，HAL毫秒时基独立处理，并核对异常入口、IRQ优先级和栈。方案上把最小调度验证与低功耗验收分开，不在第一步同时改变任务拆分、睡眠和帧所有权。任务优先级也不能代替帧所有权协议。

**不要提前宣称的结果：**RTOS一定提高FPS、已经完成业务多任务隔离、软件仿真覆盖了真实睡眠时钟或30分钟视频稳定性。以下P0步骤是计划/闸门，不能全部当作已完成经验。

## 1. 为什么现在决定引入 FreeRTOS

**首先澄清：现有裸机系统不是运行失败。** 目前用户反馈 OV5640 → DCMI/DMA → SDRAM Media → USB FS UVC → Windows 已能稳定输出视频；本次迁移是为后续功能的**时间隔离、优先级控制、阻塞容忍和可维护性**做准备，不把“使用 RTOS”本身当成性能改善证据。

当前 CM7 主循环（`CM7/Core/Src/main.c`）：

```c
static void Main_Run(void)
{
    Framework_Run();
    USB_Mode_Process();
    USB_UVC_Process();
    App_Run();
}
```

- `Framework_Run()`：EventDispatch、TimerProcess；Timer 回调在调用者上下文同步执行。
- `USB_Mode_Process()`：USB 枚举、运行状态与受控重连。
- `USB_UVC_Process()`：软件构包、发送推进、完成确认和取消恢复；使用 USB SOF 作为 ISO 发送机会。
- `App_Run()`：CameraCapture_Process 与 UVC Bridge 帧借用/归还。

这是一条**协作式顺序调用链**：只有本次函数返回，后面的模块才有机会执行。当前业务简单时可控；一旦加入较长的同步工作，就必须把所有工作主动切成小步，否则 USB 发送与相机状态推进会受影响。

### 1.1 各项后续功能对应的具体调度矛盾

| 计划能力 | 裸机同步执行的潜在问题 | FreeRTOS 能提供什么 | FreeRTOS 不能自动解决什么 |
| --- | --- | --- | --- |
| P1 JPEG/MJPEG | 编码、可变长帧管理或图像控制可能增加 CPU 占用 | 可将长工作放入低优先级任务 | DMA 缓冲容量、JPEG 格式、硬件吞吐仍要单独解决 |
| P8 Ethernet/lwIP | 网络协议处理、重传、连接等待可能占用主循环 | 将网络任务与高优先级 USB 视频推进分开 | PHY/RMII 硬件故障、Cache 一致性、带宽限制不会因 RTOS 消失 |
| P3 AI 推理 | 单次推理可能持续几十毫秒甚至更久，造成 UVC SOF 软件处理不及时 | 允许 USB 视频任务抢占 AI 任务，AI 独立限频 | 长时间关中断、不可抢占临界区仍会破坏实时性 |
| P4 SD/FatFs | 文件写入、擦除或介质响应时间不确定 | 低优先级 Recorder/Storage 任务与有界消息队列隔离 | SD 带宽、队列积压和文件系统掉电一致性仍需设计 |
| P6 USB UVC+MSC | 两种 Class 共享 USB 总线，同时有视频和磁盘请求 | 可分离应用侧存储处理和视频数据准备 | EP0/端点资源、FS 带宽和同一 FAT 卷多写者问题需专门设计 |
| P7 BLE | GATT 请求、配置保存与状态上报带来新的异步工作 | 可通过 Queue/Notify 将控制请求交给应用处理 | BLE 芯片固件/Host 协议栈适配及权限校验仍需完成 |

**核心动机：从“所有模块都必须快速返回”转为“不同业务拥有可定义的抢占关系与等待机制”。**

### 1.2 为什么不继续用现有 Framework 轮询扩展

现有 Framework 的 Initcall、Device Export、Event、最小堆 Timer、Media 帧状态机有明确价值，应该保留。

继续纯轮询也并非错误，但需要自己逐步补齐任务优先级、唤醒、等待、互斥、超时、任务诊断及调度约束；当 AI、网络、存储引入不确定执行时间时，这些内容会逐渐成为另一套自研调度器。选择成熟 FreeRTOS 内核可减少这部分通用调度机制的重复实现。

**保留边界：**
- Initcall / Device Model：继续承担分层初始化和设备抽象，不与 FreeRTOS 的 Task 混为一谈。
- Framework Event / Timer：初期保留，由专用服务任务推进；后续若与 RTOS 机制重复，再按调用上下文和功能判断是否替换。
- BSP / Device / Media / UVC：继续管理寄存器、硬件状态机、帧所有权和协议，不应直接依赖业务任务名。
- IPC：CM7/CM4 的 SRAM4 + HSEM 机制保持，FreeRTOS 任务通知不能跨核直接替代 HSEM。
- VideoConfig：仍是静态配置单一入口，本轮不引入无缝运行时重配置。

## 2. 当前源码显示的迁移敏感点

以下均根据 `main@2873d2c` 检查，需在 P0 逐项复核：

1. **SysTick / HAL Tick 冲突**：`CM7/Core/Src/stm32h7xx_it.c` 当前 `SysTick_Handler()` 调用 `HAL_IncTick()`，`CM7/Platform/platform_time.c` 返回 `HAL_GetTick()`。FreeRTOS 也需要 SysTick、PendSV、SVC 异常入口。不得保留两个独立的 SysTick Handler，也不能在移植时让 `HAL_Delay` / `Platform_TimeGetMs` 停止前进。推荐把 CM7 HAL time base 改为独立 TIM 定时器，由 FreeRTOS 管理 SysTick；需检查时钟配置和具体 HAL 驱动。
2. **中断优先级及 FromISR 限制**：目前 CM7 DCMI、DMA1 Stream0 IRQ 的 HAL NVIC 优先级为 5，USB OTG_FS 为 6。FreeRTOS 采用 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 约束可调用 `...FromISR()` 的中断。必须结合实际 `__NVIC_PRIO_BITS`、优先级分组及 FreeRTOSConfig 校验，不能在未核对前随意从这些 ISR 调用 RTOS API，也不能直接套另一项目的优先级常量。
3. **现有 USB 锁的语义**：`CM7/Platform/usb_device.c` 中 `USB_Device_Lock()` 通过屏蔽 `OTG_FS_IRQn` 保护 USB 内部状态。这只隔离 USB ISR，**不等价于不同 FreeRTOS 任务之间的互斥**。若拆分多任务调用 USB 接口，必须先核实所有共享对象是否为单任务所有；需要跨任务时使用明确的互斥/消息传递，不可只依赖 `NVIC_DisableIRQ`。
4. **UVC 每毫秒 ISO 发送机会**：`USB_UVC_Process()` 在主循环中构包与确认，USB SOF 不能被当作可无限积压的队列。迁移后高优先级任务必须能及时运行，且不能通过无休止忙等饿死其他任务；要测量真正成功完成的 FPS、漏 SOF 和取消/恢复情况。
5. **旧的 PRIMASK 临界区**：DCMI/Media 中使用 `__disable_irq()` / 恢复 PRIMASK 操作，用于共享硬件状态快照。引入调度器后需审查临界区嵌套、最大关中断时间和任务竞争；不可机械替换，更不可在临界区调用可能阻塞的 RTOS API。
6. **启动与双核顺序**：CM7 当前先 HAL/时钟初始化、HSEM0 唤醒 CM4、MPU SDRAM 设置，随后 `Framework_InitRuntime()`，最后进入裸机 `while(1)`。移植首先保证既有电源/时钟/SDRAM/CM4 早期启动流程不变；决定哪些 Service/Application 必须在调度器启动后初始化，避免初始化函数等待尚未运行的任务。
7. **内存与编译器适配**：当前 `MDK-ARM/h7_mac_CM7_custom.sct` 将通用 RW/ZI 放在 `0x20000000` 起始的 128 KiB 区间，另设 `0x38000000` 4 KiB 共享段。需单独预算 FreeRTOS 任务栈、Kernel 堆、ISR 栈、AI Arena 与 USB/Media 静态缓冲；核对 ARM Compiler 5 对应 FreeRTOS Cortex-M7 Port 的支持，严禁栈与共享段重叠。

## 3. 为什么只先移植 CM7，暂不动 CM4

- CM7 负责相机、DCMI/DMA、Media、USB，以及后续 JPEG、AI、网络、存储；并发需求主要集中在这里。
- CM4 OLED I²C2 异步刷新与现有 Framework 已经有实板基线，不应无必要地同时引入第二套调度变量。
- STM32H747 为双核，**两核各有自己的异常向量、SysTick 和调度上下文**；CM7 FreeRTOS 不会自动管理 CM4 任务。
- 待 P2/OLED+IPC 工作量证明 CM4 需要独立任务调度时，再单独评估 CM4 FreeRTOS；不作为本轮目标。

## 4. 两种迁移策略及选择理由

| 方案 | 优点 | 问题 | 决策 |
| --- | --- | --- | --- |
| A. 一次性拆为 USB / Camera / AI / Framework 多任务 | 看起来更快进入多任务形态 | 同时修改线程所有权、任务同步、执行顺序，难定位回归 | **不采用** |
| B. 先把现有 `Main_Run()` 放进单个 Legacy/VideoTask，再逐步按所有权拆分 | 更容易保持现有初始化、帧借用和时序；每步可对照 | 初期不会立刻提高并发性，需要下一步渐进拆分 | **采用** |

**关键：移植成功不等于任务数量多。** 首先证明 RTOS 接管调度后现有 UVC 不退化，再让 USB/Camera/Framework 以清晰的单写者原则渐进解耦。AI 和 Storage 尚未开发，不预先创建大量空任务。

## 5. P0 FreeRTOS 实施步骤与阶段闸门

### P0.0 固定原始基线（只读记录）
- 记录提交 SHA、PCB 版本、160×120 YUY2 10 FPS 配置，原有视频软件回归结果。
- 实板记录 USB Windows 预览稳定性、真实成功输出 FPS、DCMI/DMA Error、丢帧、复位原因。
- 没有该基线，就无法证明引入 FreeRTOS 后是否出现性能回归。

### P0.1 FreeRTOS 内核最小启动（不拆业务）
- 选择与 ARMCC5/Cortex-M7 匹配的 Kernel/Portable Port，建立 `FreeRTOSConfig.h`、异常向量与静态任务/堆配置。
- 理清 `SysTick`、`PendSV`、`SVC`、HAL Tick 的唯一所有权，校验 IRQ 优先级规则。
- 用最小任务、空闲任务和 `vTaskStartScheduler()` 验证 1 ms Tick 与任务切换，不修改 CM4 启动与 OLED。
- 验收：启动、切换、Tick、栈水位、断言与 HardFault 诊断均正常；CM7/CM4 各自编译 0 Error/0 Warning。

### P0.2 迁移现有业务到单个任务（保持执行顺序）
- 第一版将原 `Main_Run()` 四项调用按原顺序移动到 Legacy/VideoTask，先避免人为拆分 Media 与 UVC 帧生命周期。
- 任务必须有可测量的让出 CPU/等待策略，不能依赖同优先级轮询任务碰运气；更不能用无限忙等独占高优先级。
- 确认 `Framework_InitRuntime()` 各级 Initcall 与任务创建的先后关系，避免未启动调度器时调用阻塞型 API。
- 验收：原 YUY2 UVC 协商、完整帧、取消/重连、DMA 错误计数及 CM4 OLED 功能没有回归。

### P0.3 渐进式任务隔离（先保实时视频）
- 明确唯一 USB 状态机调用者：USB UVC 处理必须具备及时调度机会；对 SOF 采用可验证的事件唤醒或短周期调度。
- 再将 Camera/Framework 从 Legacy 任务拆出；拆分前逐个标注 API 的“任务线程上下文 / ISR / 可阻塞性 / 共享锁”。
- 队列只传帧描述/事件，不复制整帧；保留 Media 的 `IN_USE` 与 UVC Bridge 借用/归还规则。
- 如果拆分导致 USB 时序变差，先回退到 P0.2，再单独处理竞争；不以增加任务数为目标。

### P0.4 实板回归与结项
- 软件：原视频配置测试与 USB Core/UVC/Media/Camera 全套适用回归（历史固定包长用例须与当前 1023B 协议基线区别处理），CM7/CM4 完整重编。
- 实板：Windows UVC 连续预览至少 30 分钟；成功输出 FPS 和丢帧不明显劣于 P0.0，DMA Error 不新增，USB 断开/重连及 CM4 OLED 不失效。
- 记录 CPU 任务运行占比（若可测）、Task Stack High Water Mark、Heap Free/Minimum Ever Free、最长临界区时长及异常恢复次数。
- 每个关卡保留 commit、配置、日志、截图、测试脚本；任一步不通过，回退到上一稳定点。

## 6. 必须防止的两个误解

**误解一：“换成 FreeRTOS，USB FPS 一定会提升。”**  
不成立。USB FS 1023B/1ms 的物理限制、DCMI DMA 与 SDRAM 争用都不会凭空消失；RTOS 可以让高优先级软件及时运行，但需要用真实 FPS/错误计数来验证。

**误解二：“有了任务优先级，就不需要所有权状态机。”**  
不成立。Media 的 DMA 双缓冲/三缓冲、UVC 的异步帧借用以及 USB Endpoint Abort/完成确认必须完整保留。调度器负责“何时运行”，Media/UVC 各自负责“谁拥有资源、何时允许归还”。

## 7. 本记录的结论与实施约束

- **决定：** FreeRTOS 移植作为 P0，先 CM7，CM4 暂不迁移。
- **动机：** 在现有稳定视频基础上，为 JPEG、Ethernet、AI、SD、MSC、BLE 的可控并发调度与系统级资源隔离建立基础。
- **严格边界：** 不为移植重写 BSP、Device Model、Media、USB Core/Class；不在同一提交中修改相机 DMA 流水线和大量业务模块。
- **尚未验证：** RTOS 对实时性、系统吞吐和故障率的实际改善效果；只能通过 P0 实板回归判定。
- **防偏航：** P0 验收后立刻回到 P1 JPEG/MJPEG 主线；复杂 RTOS 调度优化与 CM4 RTOS 移植不自动进入当前范围。

---
*本文属于迁移决策和 Debug 经验记录；后续实施中另附 FreeRTOS 移植日志、软硬件回归结果与问题闭环，不用计划描述冒充已经完成的功能。*
