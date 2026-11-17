# Phase4 ARMCC5预算

C99、Cortex-M7.fp.sp、ARMCC5 5.06u7 build960，O0/O2真实编译、链接、执行；Native测试使用split_sections。原工程EnumInt=0与独立测试一致，未更改设置。

| 对象 | ARM bytes |
|---|---:|
| VCamera（内含唯一LCContext） |336|
| VCameraEndpoint（Camera端） |124|
| VCameraClient（Supervisor端，无Camera指针） |88|
| VCameraOps |48|
| VCameraTransportOps |36|
| VCameraStatus |64|
| VCameraResult |40|
| VCameraReply（本地值） |32|
| VCameraCapabilities |24|
| VCameraExitEvidence |32|
| 原CameraConfig_t |6|
| 原Phase3 LCCompletion |28|

offsetof为ARM运行probe：VCamera.lifecycle44、format156、native212；Result.observation20；Reply.delivery_error28；Client.request24；Endpoint.ack84；CoreMediaFormat56B。原CameraConfig_t.format偏移4。未修改任何原声明或52B/56B消息。

Camera+Endpoint+Client需548B caller存储，另计既有MR Context/三方向运输队列和配置临时对象。当前无生产全局实例，新库RW/ZI=0；不是集成不需要RAM。帧/SDRAM增量0，不创建Pool或物理refs。测试trace、小型Mock帧、原生Device对象不计新库RAM；独立链接保留原s_sensor52B，未执行硬件，真实Target已有同一Device不得再重复登记。

## O0

新Camera三C单元RO4238B，RW/ZI=0。所选测试路径SP最大下降1356B；Maximum Stack Usage =       1392 bytes + Unknown(Cycles, Untraceable Function Pointers)。包含测试调用者及Composition/EM路径，不是任务栈或WCET。

- VCamera_Translate：自身栈32B，已知深度76B，回调另计。
- VCamera_Process：自身栈24B，已知深度64B，回调另计。
- VCamera_FinishRound：自身栈40B，已知深度176B，回调另计。
- VCameraEndpoint_RunRound：自身栈16B，已知深度192B，回调另计。
## O2

新Camera三C单元RO3374B，RW/ZI=0。所选测试路径SP最大下降1332B；Maximum Stack Usage =       1352 bytes + Unknown(Cycles, Untraceable Function Pointers)。包含测试调用者及Composition/EM路径，不是任务栈或WCET。

- VCamera_Translate：自身栈24B，已知深度52B，回调另计。
- VCamera_Process：自身栈24B，已知深度56B，回调另计。
- VCamera_FinishRound：自身栈40B，已知深度184B，回调另计。
- VCameraEndpoint_RunRound：自身栈32B，已知深度216B，回调另计。

静态调用图含Unknown(Cycles,Untraceable Function Pointers)，不是有限WCET证明。运输/证明/提交不得等待其他任务；旧Sensor初始化/SCCB/Delay耗时不能称非阻塞，需要真实owner预算与集成批准。每轮最多一条执行请求，成功提交的Native操作不重复，归还每轮继续推进。真实任务响应、ISR嵌套、高水位、FPS均NOT_RUN，未改任务栈/优先级/MPU/链接脚本。

Keil隔离全量Rebuild：CM7 Code137690 RO3114 RW1340 ZI100932；CM4 Code26350 RO1438 RW936 ZI3576，均0 Error/0 Warning。V2尚未登记原Target，该镜像是兼容构建；新库由独立ARM目标验证。实际AXF/MAP/HTM/HEX与SHA256保留output/V2Phase4，输入项目hash与日志随Evidence提交。
