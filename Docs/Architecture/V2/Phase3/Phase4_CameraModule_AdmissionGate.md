# Phase 4 Camera Module接入门禁（本轮未实施）

Phase3交付仅独立Execution软件；本文件不是Phase4授权。两个Resource NOT_PROVEN、原72与新增32集成/硬件场景NOT_RUN继续保留，不相加成104个已验收项目。

1. Camera owner真实Task上下文绑定LCContext，Supervisor owner与模块Task之间用合法运输；不得把测试的顺序actor切换当成真实跨任务同步。busy不作为锁。Registry/Resolver/Graph scratch仍归单装配owner串行使用。
2. 模块持有唯一生命周期，真实媒体/硬件状态仍归原CameraCapture/Device。can_begin核验配置、资格及旧资源退休；prove对具体Deactivate/Cleanup确认排空，不用Lifecycle INACTIVE替代Media、Bridge、DMA停止事实。
3. 普通Camera停止仍仅由App_CameraFinishRound协调；Ops begin只登记停止意图，LC_Complete使用完整身份匹配的真实结果。不得增加第二个CameraCapture_Stop入口。保护性停止及同轮记账安全保持。
4. 部分START失败后保留真实M/旧错误并继续Return处理。没有安全退出证据时报告ERROR或未完成，不把begin失败假装无副作用、不清Resource/Qualification。Device READY、Media完全停止、Bridge已归还、文件关闭分别核验。
5. 新的LCRequest/Completion是本地24B/28B值，不能直接作为修改旧52B/56B消息的许可。需审核原运输所有权、完整执行身份、可靠重试/消费发布和静态容量，再绑定；不平行新建资格Replay。
6. Composition Candidate必须来自已验证的真实配置；模块适配器绑定不可变配置和执行session。候选计算、Lifecycle请求受理、真实配置成功、实际FPS分别报告。当前没有运行时Apply功能。
7. 独立软件只演示可靠端口契约。后续需要真实RTOS队列满/通知合并/延迟、完整STOP/重启、DMA未完成/Abort失败、Frame Return与ACK消费、故障排空、实际任务栈与WCET等软件联合/实板门禁。
8. 若Resource Backend查询缺失、D/T双端发布或G→M/稳定关闭仍无法证明，保持阻塞并提交最小ADAPT方案，不能以本层完成状态覆盖缺口。本轮未修改Resource、Legacy、HAL、Keil或M4 OLED延时。
