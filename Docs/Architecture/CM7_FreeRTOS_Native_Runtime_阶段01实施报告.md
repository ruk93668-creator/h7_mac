# CM7 FreeRTOS Native Runtime 阶段01

基线 `8d3741a`；独立分支 `codex/cm7-native-runtime-stage01`，不合并main。参考阶段00审计，并按本轮决定：Initcall继续在main、Scheduler启动前执行。

## 启动顺序

```text
main：既有HAL/时钟/双核启动/MPU
  → App_TasksCreate
      绑定普通C事件后端和USB唤醒回调
      静态创建CameraTask、UsbTask、ControlTask
  → Framework_InitRuntime
      PLATFORM → BUS → DEVICE → MEDIA → SERVICE → APPLICATION
      仅准备：不启动Camera Stream、不连接USB
  → App_TasksRecordInitialization
  → Platform_RtosStart
      CameraTask/UsbTask先阻塞等待BOOT_READY
      ControlTask发布BOOT_READY
      UsbTask唯一一次启动TEST USB
```

任一创建或初始化失败，main保留init_error并停止启动；没有部分成功后继续运行。部分静态任务创建失败后不复用TCB、不自动重试，需要复位。Control不执行Initcall。准备阶段保留既有传感器Device初始化，但没有Application配置/启动输出和Media采集。

任务创建在Initcall前：FreeRTOS会保持BASEPRI屏蔽，不能把这个正常的调度前状态误判成非法启动。现有HAL TIM6优先级4高于屏蔽阈值5，调度前HAL Tick可继续工作；首个任务启动按原Port恢复屏蔽。

## 职责和所有权

|模块|本阶段职责|禁止/未实现|
|---|---|---|
|CameraTask|静态存在，BOOT后阻塞等待事件|不启动采集，不取帧，不借出Media缓冲区|
|UsbTask|独占TEST模式启动与USB_Mode_Process，事件唤醒或20ms检查|不调UVC Process，不读相机/Media|
|ControlTask|发布BOOT屏障，阻塞等待故障与1000ms诊断唤醒|不执行Initcall，不操作帧|
|app_tasks|组织创建、记录准备结果、发布BOOT入口|不做业务轮询|
|app_events|任务通知、FromISR、上下文/优先级校验|不把RTOS类型泄漏到下层|
|platform_rtos|调度器、Idle/Tick/故障Hook和静态Idle内存|不创建业务任务，不持有Main_Run回调|

优先级：Usb4、Camera3、Control2、Idle0。静态任务栈分别8KB、4KB、3KB；Idle1KB，主异常栈维持原配置。栈大小已检查链接结果，但未经过真实压力高水位验收。

只有业务任务和Application事件适配器使用TaskHandle_t。BSP/Device/Media/Service/Common没有FreeRTOS头文件或任务类型。USB IRQ仍按既有BSP/HAL/Core路径处理中断协议，Platform仅在状态提交后调用普通C通知，Application再转成任务通知。没有将整个USB中断协议搬到任务。

## 普通C事件契约

`Common/Inc/runtime_event.h`定义目标及提示位；`Common/Src/runtime_event.c`校验和转发一次绑定的静态后端。

- `RuntimeEvent_Post()`：普通初始化/任务上下文。
- `RuntimeEvent_PostFromIsr()`：外设ISR，IRQ数值优先级必须不小于5；USB为6。
- CAMERA_PROGRESS、USB_PROGRESS、WAKE、FAULT只是提示；重复可合并，不承诺事件次数。
- BOOT_READY仅Application发布，外部不能伪造。
- Scheduler/BOOT前仅锁存提示，不能让业务提前运行；Control提交屏障时合并广播。
- 等待通知不清除进入前已有的位，返回时一次取走；不在关中断或屏蔽内核API的临界区阻塞。
- ISR不执行任务等待、帧释放或业务处理。高紧急度ISR如需未来接入，必须额外设计延期机制；当前直接拒绝，不擅自修改硬件IRQ优先级。

这是唤醒契约，**不是跨任务命令队列或帧租借协议**。Camera/Media与USB之间不存在新增帧通信，后续阶段必须单独设计所有权确认及取消。

## 修改文件及原因

|文件|原因|
|---|---|
|Common/Inc/runtime_event.h、Common/Src/runtime_event.c|无RTOS类型的提示契约和后端分发|
|CM7/Application/app_events.c/.h|任务通知、BOOT、ISR适配|
|CM7/Application/app_tasks.c/.h|唯一任务创建组织入口|
|CM7/Application/Tasks/camera_task.c/.h|Camera静态任务与阻塞入口|
|CM7/Application/Tasks/usb_task.c/.h|TEST控制面唯一业务执行者|
|CM7/Application/Tasks/control_task.c/.h|屏障与故障提示接收|
|CM7/Application/app_main.c/.h|移除Legacy App_Run路径，Initcall只校验TEST准备状态|
|CM7/Application/video_config.c|本阶段默认USB TEST，保留相机参数但不启用采集|
|CM7/Core/Src/main.c|创建、准备、记录、启动Scheduler；移除全局轮询|
|CM7/Platform/platform_rtos.c/.h|删除Legacy任务和回调，保留RTOS环境|
|CM7/Platform/FreeRTOSConfig.h|显式启用获取当前任务句柄接口|
|CM7/Platform/usb_device.c/.h|普通C状态通知注册和触发，不改协议/端点所有权|
|CM7/Service/usb_mode.c/.h|原APPLICATION表项改为准备，增加只读模式结果|
|MDK-ARM/h7_mac.uvprojx|CM7加入源文件和Tasks路径；CM4显式排除新增组|
|Tests/NativeRuntimeStage01/|新专项、实际结果、构建日志、源码/链接审计|
|本报告|职责、失败策略和验证边界|

`UsbMode_AppStart`保留历史静态符号/Initcall表项便于对照，但函数现在仅调用幂等准备，不连接USB。实际启动在UsbTask。CM7不再链接LegacyVideo/Main_Run/App_Run/Framework_Run。Framework源码仍保留，不影响CM4。

## 验证结论

详见 `Tests/NativeRuntimeStage01/README.md` 与逐例JSON。

- 新任务/事件56/56，USB真实Core集成15/15。
- 历史USB204中201通过、3个自动连接断言失败；原断言未修改。基线204/204通过。
- EP0 IRQ4、VideoConfig28、CameraAppInit13、CameraContextRefactor72、CameraStreamState75全部通过。
- 合计执行467项，464通过、3失败；原Legacy专项11项因已移除接口编译不兼容，未执行。
- 源码与链接审计15/15：受保护目录无差异、没有下层任务依赖、SVC/PendSV/SysTick向量及三个静态任务链接正确。
- CM7 TEST Native及CM4完整重编均0 Error、0 Warning。

**没有将历史全量回归标为通过。** 三个旧自动连接断言需后续人工批准更新规范；本轮遵守“不修改历史断言”。没有为凑通过数恢复Scheduler前连接。

## 用户修改保护与版本管理

本轮开始已有usb_uvc_desc.h注释、Keil调试设置、两核优化级别和工程分组顺序修改，另有output/、tmp/。这些内容保留工作区，不加入本次提交。正式编译基于干净工程配置叠加本次文件加入改动，构建生成的RTE文件恢复原字节。main不移动，不合并；只推送独立分支。

## 遗留风险和下一阶段

1. 还未执行实板TEST枚举、真实SVC/PendSV切换、WFI长期唤醒和任务栈压力验证。软件夹具跳过首个SVC并手动选择上下文，不能代替异常切栈验收。
2. 三个业务任务目前只有TEST控制面可运行。UVC配置在App_Init明确拒绝，禁止把原裸机视频通过结论沿用到Native。
3. USB ISR与UsbTask仍共享既有Core状态，沿用原保护；本轮测试覆盖控制面交错，不等于完成未来UVC并发审计。
4. FAULT提示可合并；Control只记录通用故障，原始原因分别看g_usb_task_debug.result和init_error，不做自动恢复。
5. 事件后端一旦绑定、部分任务创建失败后不支持重建；复位是本阶段明确的恢复方式。
6. 下一阶段先验收实板TEST控制面，再独立明确CameraTask独占Media、UsbTask独占UVC及跨任务帧借用/取消协议，不能直接复用全局轮询桥接。
