# Phase 8.0 集成实施计划

对应冻结 Phase 8 Runtime Reconfiguration。P8.1～P8.5 顺序待批准。每一阶段单独提交和验证，不改变 Phase 0～10 或五核心分工。开工前重新核对两端 SHA 和 Keil 外部修改。

| 阶段 | 改动候选及实施步骤 | 验证和下一闸门 | 回退 |
| --- | --- | --- | --- |
| P8.1 CM7 登记及静态装配 | 经批准仅登记 `MDK-ARM/h7_mac.uvprojx` 的 CM7 Target 和 V2 现有 C/H，增加板级 descriptor、profile 和静态装配准备。保留 main.c、CM4、旧 BOOT 与任务运行。核对 include、重复符号、Initcall/Device 段与链接裁剪 | ARMCC5 两核完整 Rebuild、0E/0W、ABI 52/56，Map 区分真正链接和被裁剪的 V2 对象，记录 SHA256。Legacy 实板基准与独立 V2 软件结果分列 | 按提交退回已知工程配置或固件。只撤销审过的登记差异，保留用户 Keil 配置 |
| P8.2 真实任务运输 | Core/Runtime、Modules 增加任务 transport bindings。候选修改 `app_tasks.c`、`app_events.c`、Camera/USB/Storage/Control Task。静态队列分别承载请求、完成、消费。Control/Execution 角色若同处 ControlTask，按明确顺序串行推进；保留 IPC。先用隔离端口运行 | 实际 FreeRTOS 测唯一 owner、队列满、合并通知、重复/过期、首错及补偿。报告容量、每轮服务批次、等待及 high-water。V2 此时没有真实启停权 | 禁用 V2 transport 调度，保留旧通知和任务。不能靠清空队列解决正在执行的事务 |
| P8.3 真实媒体、资格与停止证据 | 审核 `CameraCapture`/JPEG FIFO 原账本只读查询、`app_camera.c`/Qualification、`app_uvc_bridge.c`、`app_frame_dispatcher.c`、`app_storage.c` 和 V2 Ops。原 DMA/Retain/Release 算法沿用，每物理源仅一套关联 Context。CameraTask 增加有界资格处理。可用每次仅一套硬件权威的独立诊断固件取证 | G/H/B/M/S/T 和关闭窗口都需要真实轨迹。Raw 单 owner、JPEG 双 owner、SD-only、双 span、ACK 实际消费和独立退出应验证。真实 DCMI、USB、IDMA 实板证据签核前关闭 release/idle 成功路径并禁止 P8.4 接管 | 禁用未授权选择或恢复 Legacy-only。已开始的事务必须安全排空或保留阻塞，禁止重建 Context 清除持有 |
| P8.4 单一 BOOT/Watch 入口切换 | 经审批改 `control_task.c` 与已验证的任务端口。沿用 BOOT 屏障，再通过受信 Control 请求启动。Watch/录像/抓拍/停止映射显式命令，Phase7 只有 VALIDATE/START/STOP 类操作，Storage 用户命令语义需要审批。选择 V2 时排除 Legacy 命令执行；底层真实启动只提交一次 | 冷启动、部分失败、独立退出、最后消费者退出和重启、后台恢复与保护停止。Camera Activate 和 ACQUIRE 只能有一个实际启动提交者，另一方读取事实。普通 STOP 只有 FinishRound 推进 | 只在证实硬件静止后启用 Legacy-only 固件，不在未退休 G/M 时在线移交。保留故障和磁盘证据 |
| P8.5 Runtime Reconfiguration | 在既有五核心内补齐 Desired/Applied/revision。Control 受理，Composition 计算不可变候选，Execution 组织旧 G 关闭、排空、重配和激活，模块报告事实。新配置实际成功后才提交 Applied | 实板验证 Raw↔JPEG、双消费者、拒绝、失败回退、旧消息、队列背压和主机重连。区分候选合法、底层应用成功和真实 FPS | 在可验证静止后通过新事务恢复旧 Applied。补偿结果不得覆盖原首错；未知硬件状态保持 BLOCKED，整固件回退另走受控流程 |

## 接合约束

1. Camera task 先 BeginRound，再推进完成消费、受限命令与资格接收。其后 Camera Process → Bridge/Storage Return → FinishRound。资格读停止结果必须使用 FinishRound 已记录的匹配事实。独立的运输背压不应阻断本轮可执行的 Return，积压时用原通知机制续唤醒。
2. L_admit 是 Camera owner 按完整 G/H 受理交接且尚未 Get/Retain 的点。L_close 是同一 owner 绑定原 G 和 R 的关闭点。L_verify 在关闭后的受控窗口。既有 H 可以排空，旧消息不能重开准入。真实实现及原权威账本必须证明所有路径覆盖。
3. 先停止消费者新交接，再等待读者结束、物理归还和匹配消费确认，最后才允许运行全局停止。Camera STOP 仍以真实 Bridge、Media、DMA、Device 四层事实为凭。短临界区保护身份发布或状态认领，不能跨 HAL 等待或 FatFS。保护性 STOP 保留同轮去重，后台成功为独立观察。
4. 保留 ABI、Replay、IPC 布局、OLED 延时和现有栈/优先级。若端口需要独立 CM7 内部运输，审批其消息容量、结构复制成本和 RAM；不复制资格 Replay。Map 仔细核算 Flash、RW/ZI、AXI、SDRAM 和队列，按真机最慢 I/O 负载测任务栈及调度指标。缺指标时暂停，不暗调资源预算。
5. 外部控制驱动、M4 新控制协议、Media/Bridge/Storage 生产适配和真正接管均需分开授权。没有真实证据时保持两个 NOT_PROVEN，不使用 Debug 快照填充退出凭据。

配套矩阵的证据必须绑定源码 SHA、Target、固件 SHA256、板版本及优化级别。模拟、Legacy 兼容构建和新路径硬件结果分别记录。
