# System Monitor 数据通路审核

基线为 `57eb588`。CM7 实际链接 FreeRTOS，在 `platform_rtos.c` 启动调度器。Camera、Control、IPC、Storage、Usb 使用 `xTaskCreateStatic`；Idle 栈由 `vApplicationGetIdleTaskMemory` 提供。当前 `configSUPPORT_STATIC_ALLOCATION=1`、`configSUPPORT_DYNAMIC_ALLOCATION=0`、`configUSE_MALLOC_FAILED_HOOK=0`、`configUSE_TRACE_FACILITY=0`、`configGENERATE_RUN_TIME_STATS=0`、`INCLUDE_uxTaskGetStackHighWaterMark=1`。当前固件没有可报告的 FreeRTOS 动态 Heap 或 CPU 占比。任务容量必须由创建元数据取得，即使当前全为静态任务，将来开启动态任务也使用同一登记流程。栈容量和高水位以 StackType_t 元素登记，转字节前检查乘法上溢。历史峰值是容量减历史最少剩余，不是即时栈占用。

现有只读 IPC 可提供对端在线、心跳进展、读错及业务掩码。其 V3 公共 ABI 为 320 B，固定业务载荷不包含任务表、Heap、CPU 或 CM7 uptime。保留区和已有字段不复用。CM4 不读取 CM7 TCB，不访问未知共享地址。第一阶段只交付 CM4 菜单和注入验证；CM7 目标继续原样构建，真实任务采样和传输记为 BLOCKED，等接口审核。

## 提请审核的生产通路

择一新增版本化诊断信封或 IPC 扩展能力位。即便利用现有保留的 4 KiB SRAM4 窗口，也需审核地址分配、链接布局、兼容策略及两核配置。推荐独立的 32 B 对齐只写槽，原 V3 字段保留，新旧固件通过新增诊断魔数与版本发现，不存在则 N/A。不能仅凭保留空间尚空自行写入。诊断槽的每字节归 CM7 单写，CM4 只通过 Service 以有界重试复制本地快照。以奇偶序号加屏障发布，核对 CM7 MPU 与 CM4 每端的属性；优先两核均用非缓存共享映射。若任何一端缓存，必须按缓存行对齐并在发布/读取顺序执行维护，再证明无撕裂。会话与序号区分 CM7 重启，不把旧记录延用到新会话。

诊断本地模型容量为 8 项，至少包含版本、会话 ID、代数、时间戳或接收时间、总数、已发布数、有效字段掩码及截断状态。每条任务包含生成标识（不能复用 TaskHandle 作为跨核 ID）、名称、状态、优先级、容量/HWM/CPU 各有效位。任务有删除或重建时从创建/销毁登记更新代数；采样不能在登记外保存会过期的 handle。列表超过容量时明确 TRUNCATED，选择按 ID 跟随，否则钳位旧索引。CM4 用本地接收时间判断 2 秒过期，不减去另一核时钟，重启或失联立即丢弃旧视图。

建议 CM7 低优先级按节拍采样，每 500–1000 ms 发布，分摊至多一项 HWM 扫描；保留一次有界并发安全读窗口，绝不逐 OLED 帧扫描任务。整表同一代数确认后才发布。若启用 trace/scheduler 统计，要单独测量暂停调度窗口、执行时间和 Camera/USB/Storage 延迟，审核 Idle 统计来源、共同测量域、溢出和任务重建。CPU 应以两个有效区间的 Idle/总运行计数差得出，分母为零或统计未启用显示 N/A。无动态 Heap 后端时不显示总 RAM、不虚构 FreeRTOS Heap=0；启用后用真实分配器容量、当前空闲、历史最小和分配失败事件分别采样。任何 CM7 生产或共享 ABI 布置变更都需审核后再实施。
