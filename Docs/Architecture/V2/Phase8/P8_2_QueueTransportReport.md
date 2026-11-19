# P8.2 运输契约

| 模块 | Request | Completion | Consumed ACK |
| --- | --- | --- | --- |
| Camera | ControlTask → CameraTask，LCRequest | CameraTask → ControlTask，VCameraReply | ControlTask → CameraTask，LCCompletion |
| USB | ControlTask → UsbTask，LCRequest | UsbTask → ControlTask，VUSBReply | ControlTask → UsbTask，LCCompletion |
| Storage | ControlTask → StorageTask，LCRequest | StorageTask → ControlTask，VStorageReply | ControlTask → StorageTask，LCCompletion |

九条均是 xQueueCreateStatic，深度 2。ARMCC5 Map 实测单项为 24、32、28 B。每组载荷存储 48+64+56=168 B，三组合计 504 B。每组路由和内核队列控制结构 232 B，合计 696 B。所有消息按值复制，旧 52B/56B 通道和 C2.1.1 Replay 均未参与此运输。

- Send/Receive 均为零等待。满返回 NO_RESOURCE，没有部分提交。Endpoint 客户端和任务端只重试原身份或冻结的结果。
- RuntimeEvent_Post 展开为原 FreeRTOS eSetBits，可能合并，不能用来计数消息。先提交队列再发送唤醒。唤醒失败也不撤销入队。各原有任务的 20/100 ms 周期继续兜底。
- 匹配 Completion 固定后才允许提交 Consumed ACK。模块端从真实队列取到一致值才退休本次握手。先入队和任务端实际消费分别取证。旧请求不能触发新执行，旧回复和错误 ACK 不覆盖当前事务。
- 只有原 Attach 注册的角色、已收到 BOOT、非 ISR、未屏蔽 PRIMASK/BASEPRI 才能访问队列。真实 FreeRTOS 拷贝和 Notify 的测试见 P8_2_TestReport.md。

Control Service 和 CSExecutionEndpoint 都在同一 ControlTask，直接串行调用同所有者端口。两者有独立的 Context 和身份确认，跨任务只使用表中九条队列。因此没有额外三条 Control→Execution 队列。
