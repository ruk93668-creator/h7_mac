# ApplicationV3 与 IPC

App 通过 CM4 IPC Service 读取本地应用视图。Camera、USB 和 Control 按独立有效位及新鲜位显示，错误沿用正式结果码。CM7 在线状态来自 IPC 存活检测。About 从 Service 的运行时快照读取最近心跳，只在在线且观察到进展时显示计数。当前 80 B ABI 只定义 Camera、USB、Control 三个数据源。Storage 状态未发布，显示 `N/A`。Ethernet 也显示 `N/A`。

| CM7 任务 | 创建中的栈元素数 | 字节 | 水位来源 |
| --- | ---: | ---: | --- |
| Camera | 1536 | 6144 | `app_services.c` 定义容量，`App_EventsReadStackWatermarks` 读取水位 |
| USB | 2048 | 8192 | 同上 |
| Storage | 2048 | 8192 | 同上 |
| Control | 768 | 3072 | `service_tasks.c` 创建，`App_EventsReadStackWatermarks` 读取水位 |
| IPC | 512 | 2048 | `ipc_task.c` 每 1000 ms 自采样 `uxTaskGetStackHighWaterMark(NULL)` |

`StackType_t` 为 4 B。水位是历史最小剩余量。已知容量减该值才是历史峰值占用。当前产品禁用 FreeRTOS 运行统计，CPU 占比、Heap、任务表和 Uptime 均无受支持数据源，System Monitor 显示缺失或失联。

**诊断扩展提案，待批准。** 另设版本化诊断消息，固定容量 8、会话与递增代次、采样时间、任务总数、有效掩码。CM7 低频采样预先登记任务元数据，捕获真实容量与水位，按会话和稳定 ID 处理重建，超额显式标记截断。CM7 唯一写入，CM4 只读两个相同的偶数序号之间的快照。需先审核共享区内存属性和双核同步规则，再择用既有非缓存共享机制或明确的 cache clean/invalidate 加屏障；接收端以 CM4 本地时间判定过期，断链丢弃上个会话。禁止临时借用原 ABI 保留位。

**业务控制提案，待批准。** BOOT 后由显式请求启动 Camera、USB 或 Storage。CM4 可通过未来的独立请求/回复记录提交版本、发送方会话、命令号、目标掩码和参数。CM7 IPC Task 验证后交给 ControlTask，由其调用 `AppV3Supervisor_Set/Query/Read/Recover`。回复带原会话和命令号、结果及最终状态。重试在同一会话按命令号去重，重启废弃旧会话。当前菜单只读。公共 ABI、MPU 和共享布局沿用主线。
