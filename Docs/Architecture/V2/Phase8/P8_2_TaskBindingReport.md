# P8.2 任务绑定和 Legacy 隔离

| 原任务 | P8.2 所有者职责 | 原调度 |
| --- | --- | --- |
| CameraTask | VCameraEndpoint_RunRound，仅操作隔离实例 | 在原 CameraTask_Process 完成后调用，20 ms 兜底 |
| UsbTask | VUSBEndpoint_RunRound，仅操作隔离实例 | 在原 UsbTask_Process 完成后调用，20 ms 兜底 |
| StorageTask | VStorageEndpoint_RunRound，仅操作隔离实例 | 在原存储轮次后调用，100 ms 兜底 |
| ControlTask | Control Service、CSExecutionEndpoint、EM 及客户端 | 原 BOOT 放行及本地编排后执行，100 ms 兜底 |
| IpcTask | 原 IPC V3 状态和心跳 | 源码未动，100 ms 周期 |

原 BOOT 屏障由 ControlTask 在全部任务 attach、Framework 初始化记录成功且调度器运行后放行。九队列在创建业务任务后、调度器启动前准备，只有各自任务看到 BOOT_BIT 才可推进。ControlTask 在短临界区发布一次不可变候选。各模块进入原任务回调后，按所属角色初始化 Context、绑定候选、再推进 Endpoint；跨任务不裸读模块私有状态。Control 的快照来自匹配完成及成功提交的消费确认。

同一任务先推进 Execution，再推进 Control 事务；CSExec_Submit/Read/Consume 使用真实执行端。诊断通过真实依赖计划配置所有三模块。Camera Activate 得到 SYS_ERR_NOT_SUPPORTED，原 EM 根据真实完成事实逆序清理三份诊断配置。异步失败不释放物理资源，也不会触发 Legacy 的 STOP/FAULT。

两个 Cortex-M 镜像的 Initcall 与 Device Export 的条目和大小均与 P8.1 一致。CM4 HEX 逐字节相同。未改 main.c、IPC、BSP、Device、Media、USB、Storage 或原调度参数。实际板上抢占和栈余量需另取证。
