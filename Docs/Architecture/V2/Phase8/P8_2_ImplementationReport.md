# P8.2 实施报告

P8.1 输入 ba70003d5f07777384a4415d33ef69289b049f38。P8.2 实现 182dbb0ba4c06048e312077cbe57174e6e29baf4。构建快照与此实现的全部更改 C/H 逐字节一致，专项编译源 SHA256 已逐项复核。

## 生产文件

- CM7/ApplicationV2/Core/Runtime/v2_rtos_transport.c 和 .h，创建九条静态队列、执行业务角色校验和通知。
- CM7/ApplicationV2/Core/Runtime/v2_rtos_runtime.c 和 .h，使用真实 Control Service、Execution Manager、三模块 Lifecycle/Endpoint，以隔离端口运行单次诊断。
- CM7/ApplicationV2/Modules/Camera/camera_endpoint.c，接收前校验 Module、Session、Request 单调性，与 USB/Storage 的现有判断对齐，防止已消费的重复请求抢占当前 Endpoint。
- CM7/Application/app_tasks.c、Tasks/control_task.c、Tasks/camera_task.c、Tasks/usb_task.c、Tasks/storage_task.c，绑定原任务和 BOOT 轮次。
- MDK-ARM/h7_mac.uvprojx，沿用 CM7 Native Runtime 分组，仅增加两对 C/H。

Legacy 仍独占 Camera、USB、Storage 硬件。P8.2 只允许隔离实例执行静态 Configure/Cleanup。Camera Activate 跨真实队列进入 Lifecycle 并得到明确拒绝，使 EM 可以复验补偿。USB/Storage Activate 和所有原生 I/O 都被拒绝，无硬件启动、DMA、资格或 Frame Lease 副作用。BOOT、主循环、原任务优先级和等待值保留。IPC V3、52B/56B、Replay 和 M4/OLED 不变。

## P8.3 准入条件

人工审核本轮源、矩阵和初始软硬件边界。在升级受信物理端口后，必须逐项证明 V1 的 DMA/后端、G/H/B/M/S/T 真实关联、L_admit/L_close/L_verify 及 ACK 真实消费，再允许打开业务资格的释放/停止通路。P8.2 的诊断事实不能用于实际资源释放。

Grant → Frame Lease 与 Stable Admission Closure 继续 NOT_PROVEN。实际 FreeRTOS 抢占时序、真机栈高水位、双核板级心跳及 V1 实板验收仍未执行。本轮停在 P8.2。
