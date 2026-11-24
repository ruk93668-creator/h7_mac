# PreMerge-R1 生产源码逐文件审核

所有条目按实际文件职责核对中文注释、正式调用路径、状态与资源边界。业务逻辑列来自清理前后完整 C 记号和预处理行比较。声明与源码散列证据位于 `output/ApplicationV3/PreMerge-R1-3f8a573/file-audit.json`。

## 测试剥离

- 原工作目录的 `board_smoke.h`、`record_board.h`、`raw_board.h` 已移到隔离归档。Control 轮询、Watch 邮箱、JPEG 联合变量、Raw Watch 对象、测试故障环、Mock 命令分发表均只在独立测试树或历史提交中复现。
- 三服务源文件、Media 和 UVC 的本地测试版已同步为正式提交源码。Production 仅保留实际硬件请求、合法只读诊断和已有结果布局中的兼容位。业务代码不执行 Mock Handler。
- 在整个 CM7 生产 C/H 和正式 `MDK-ARM/h7_mac.uvprojx` 中检索 BoardSmoke、RecordBoard、JointBoard、测试开关、Mock 编号、对应 Watch 符号和停用预处理分支。命中为零。生产 Map 另行核对链接符号。

| 文件 | 注释规范与审核点 | 测试代码 | 业务逻辑变化 | 审核结论 |
| --- | --- | --- | --- | --- |
| CM7/ApplicationV3/Core/app_context.h | 视频与存储配置的只读装配及获取入口 | 0 | 无 | PASS |
| CM7/ApplicationV3/Core/app_events.c | 任务上下文、BOOT屏障、ISR通知及栈水位访问边界 | 0 | 无 | PASS |
| CM7/ApplicationV3/Core/app_events.h | 事件发布接口及无限等待哨兵 | 0 | 无 | PASS |
| CM7/ApplicationV3/Core/app_main.c | 唯一应用Initcall及服务准备错误传播 | 0 | 无 | PASS |
| CM7/ApplicationV3/Core/app_main.h | 应用初始化调用入口 | 0 | 无 | PASS |
| CM7/ApplicationV3/Core/app_messages.c | 固定队列深度、通道源和接收者授权 | 0 | 无 | PASS |
| CM7/ApplicationV3/Core/app_messages.h | 通道和消息类别、会话及帧租约字段含义 | 0 | 无 | PASS |
| CM7/ApplicationV3/Core/app_services.c | 固定服务表、独占静态栈、启动结果聚合 | 0 | 无 | PASS |
| CM7/ApplicationV3/Core/app_services.h | 发布前准备、启动结果队列和超时退出 | 0 | 无 | PASS |
| CM7/ApplicationV3/Core/app_status.c | 跨任务状态发布、读取和启动记录 | 0 | 无 | PASS |
| CM7/ApplicationV3/Core/app_status.h | 状态快照字段与启动汇总的一致性边界 | 0 | 无 | PASS |
| CM7/ApplicationV3/Core/app_tasks.c | 应用任务创建及有序释放BOOT的门禁 | 0 | 无 | PASS |
| CM7/ApplicationV3/Core/app_tasks.h | 创建、准备和放行进度的只读诊断 | 0 | 无 | PASS |
| CM7/ApplicationV3/Core/video_config.c | JPEG和Raw正式配置及第三MBR分区 | 0 | 无 | PASS |
| CM7/ApplicationV3/Core/video_config.h | AVI单文件容量、时间和历史保留字段 | 0 | 无 | PASS |
| CM7/ApplicationV3/Runtime/queue_pool.c | 静态队列配额、代号、访问计数及回收临界区 | 0 | 无 | PASS |
| CM7/ApplicationV3/Runtime/queue_pool.h | 票据代号、任务等待和ISR非阻塞接口 | 0 | 无 | PASS |
| CM7/ApplicationV3/Runtime/service_event_loop.c | 队列复查周期、单轮预算与事件进展 | 0 | 无 | PASS |
| CM7/ApplicationV3/Runtime/service_event_loop.h | 可读回调、私有上下文和任务所有权 | 0 | 无 | PASS |
| CM7/ApplicationV3/Runtime/service_events.c | Control订阅、票号撤销、结果保留和背压 | 0 | 无 | PASS |
| CM7/ApplicationV3/Runtime/service_events.h | 硬件报告、业务终结、调用上下文及诊断 | 0 | 无 | PASS |
| CM7/ApplicationV3/Runtime/service_mailbox.c | 命令身份校验、有无硬件快照的终结及队满保留 | 0 | 无 | PASS |
| CM7/ApplicationV3/Runtime/service_mailbox.h | 邮箱深度、终结标记、命令与临时回执 | 0 | 无 | PASS |
| CM7/ApplicationV3/Runtime/service_record.c | 静态记录发布状态、票号、等待者、归还和销毁 | 0 | 无 | PASS |
| CM7/ApplicationV3/Runtime/service_record.h | 只读服务实例与必须配对关闭的引用 | 0 | 无 | PASS |
| CM7/ApplicationV3/Services/Camera/camera_service.c | 传感器启动回退、帧交接、真实停止证据和排空 | 0 | 无 | PASS |
| CM7/ApplicationV3/Services/Camera/camera_service.h | 硬件请求编号及Camera任务初始化、撤回边界 | 0 | 无 | PASS |
| CM7/ApplicationV3/Services/Storage/storage_service.c | 挂载义务、录像采样、同步写入和终结 | 0 | 无 | PASS |
| CM7/ApplicationV3/Services/Storage/storage_service.h | 硬件查询、录像命令及文件和排空标志 | 0 | 无 | PASS |
| CM7/ApplicationV3/Services/USB/usb_service.c | 设备启动、实际链路、读者证明及送还 | 0 | 无 | PASS |
| CM7/ApplicationV3/Services/USB/usb_service.h | USB生命周期和只读帧租约调用限制 | 0 | 无 | PASS |
| CM7/ApplicationV3/Tasks/ipc_task.c | IPC任务配置、快照周期与最低栈水位 | 0 | 无 | PASS |
| CM7/ApplicationV3/Tasks/ipc_task.h | IPC任务诊断、队列与入口声明 | 0 | 无 | PASS |
| CM7/ApplicationV3/Tasks/service_tasks.c | Control启动门限、Supervisor依赖和异步命令等待 | 0 | 无 | PASS |
| CM7/ApplicationV3/Tasks/service_tasks.h | 静态任务句柄、调度配置和BOOT位 | 0 | 无 | PASS |
| CM7/ApplicationV3/Tasks/supervisor.h | 已活动、待确认组件以及查询、恢复接口 | 0 | 无 | PASS |
| CM7/BSP/bsp_camera.c | 控制引脚、I2C时基、总线超时和错误处理 | 0 | 无 | PASS |
| CM7/BSP/bsp_camera.h | 摄像头接口及底层电源、时钟和读写契约 | 0 | 无 | PASS |
| CM7/Media/Inc/camera_capture.h | 采集通知、停止事实、Raw和JPEG归还限制 | 0 | 无 | PASS |
| CM7/Media/Inc/camera_mjpeg_fifo.h | 物理块容量、公开租约身份和占用只读查询 | 0 | 无 | PASS |
| CM7/Media/Src/camera_capture.c | 固定七槽、双DMA轮换、Legacy读者和保护性排空 | 0 | 无 | PASS |
| CM7/Media/Src/camera_mjpeg_fifo.c | 解析段、DMA块引用、显式和兼容持有者 | 0 | 无 | PASS |
| CM7/Platform/usb_device.c | 配置、端点资源、挂起和总线恢复事实 | 0 | 无 | PASS |
| CM7/Platform/usb_device.h | 链路和端点快照、异步通知及串行操作契约 | 0 | 无 | PASS |
| CM7/Service/mjpeg_avi.h | SDRAM索引空间、文件位置和写入终结事实 | 0 | 无 | PASS |
| CM7/Service/usb_uvc.h | 读者提交与退休证明、状态查询和帧接口 | 0 | 无 | PASS |
| CM7/Service/usb_uvc_class.c | 流协商字段、USB帧时标、包令牌及读者注销 | 0 | 无 | PASS |
| Common/Inc/camera_config.h | 格式、像素尺寸、单帧或跨环尾只读描述 | 0 | 无 | PASS |
