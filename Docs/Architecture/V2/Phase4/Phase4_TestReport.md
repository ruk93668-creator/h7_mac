# Phase4软件测试与回归

77个独立软件ID，O0/O2各执行一次，154/154 PASS；不是原72+新增32的集合计数。TestMatrix.csv逐项列举，ScenarioMapping.csv按原32项与原72关联提供子契约映射；所有原集成门禁保持NOT_RUN/BLOCKED，两个总体NOT_PROVEN未变。

测试真实链接Camera Module、Managed Lifecycle、Supervisor、Composition、Graph、Resource、Ov5640_ValidateConfig及CameraCapture_ValidateStreamConfig生产函数。物理Sensor/Device/DCMI/USB/SD及RTOS运输使用可控端口。旧硬件依赖通过测试专用Native_UnboundTrap满足链接；任何执行都会BKPT失败，不提供伪造硬件成功值，真实验证器未被stub或源码摘抄替代。

| 集合 | 结果 | 范围 |
|---|---|---|
| Phase4 |154/154|77 ID×O0/O2 |
| 负对照 |10/10有效|5变异×O0/O2，须真实断言失败，编译/模拟器错误不算 |
| Phase3原专项 |158/158|原79 ID×O0/O2，原断言未改 |
| Phase2原Resource |112/112|原56 ID×O0/O2 |
| Phase1 |250/250|原125 ID×O0/O2 |
| C2.1.1 Replay |648/648|原集合O0/O2含长期循环 |
| C2 ABI |2/2|原52B/56B布局probe，O0/O2 |
| C1 / C1.1 |419/419、24/24|原O0入口，含旧消息轨迹比较 |

负对照去掉Sensor veto、停止退出证明、当前运行事实、单次Native提交、完整回复身份。变异只写output/V2Phase4/negative，不修改生产库或原断言。ID72注入伪造成功停止flags但仍持有资源，真实Gate阻止假退出；ID73证明历史启动快照不是当前仍运行。ID71用真实Composition/EM驱动模块全生命周期，Mock sink不是Phase5 USB实现。

初始证据保留：Native首次链接缺25个依赖，后用陷阱提供纯验证链接。新Case0误假设CameraConfig_t为8B，ARMCC5实测6B、EnumInt=0，修正为精确6B/format偏移4；新Case42误把未绑定ticket错误码写为Protocol，按既有MR实现修正为InvalidArg，另加强已绑定冲突必须Protocol。最初Composition夹具用Capture直接输出Frame，原Typed Graph正确拒绝，修正为Sensor→Capture→Raw Provider→Mock Sink，未改Graph。它们不是历史断言被降低或生产回归被掩盖。

一次移除新夹具冗余条件的组合操作被自动审批拒绝，未执行；条件原样保留。旧测试源码/历史FAIL/output未改。未重跑完整旧B1/B2/B3，不声称消除B1 37/39或Consumer42/44历史差异。

真实CameraTask/FreeRTOS竞争、G→M、D/T发布、USB ACK消费、真实DMA/Abort、V1上电/图像/FPS、Device ERROR恢复为NOT_RUN。软件通过不代表实板通过。入口run_tests.py、run_negative.py、run_execution.py、run_resource.py、run_regression.py、run_protocol.py、run_build.py位于Tests/V2Phase4，实际命令/退出码/trace/hash在Evidence；使用现有ARMCC5/UV4及Unicorn2.1.4/pyelftools0.33。
