# Repository Cleanup 依赖审计

审计基线为 `4a5db4e`。候选通过全仓跟踪路径、头文件引用、符号引用、Initcall、Keil Target、测试入口及构建文件搜索定位。矩阵记录待归档文件的直接调用和共享依赖。函数指针表沿声明和调用方核对，文本搜索无结果只作为线索。

1. **C1 队列可归档。** `CM7/Application/app_messages.c` 的 C1 Init/Send/Receive 在基线产品树中只有实现、声明和自动 Initcall；真实测试调用来自 C1、C1.1、C2.1.1 与 BASEPRI。52B、五通道及帧 Return/ACK 仍走原生产代码。
2. **C1/C2 协议不能整体归档。** `CM7/ApplicationV2/Modules/USB/usb_consumer.c` 与 `Modules/Storage/storage_consumer.c` 调用原 Tx 和 `App_QualificationNextGeneration`。路径为 Consumer → `app_camera_lifecycle_replay.c` → `app_camera_lifecycle.c` 的校验和等待事务；generation 位于 `app_camera_qualification.c`。移除这些编译单元会破坏 V2 的链接与迟到消息拒绝边界。
3. **旧 Camera 适配须隔离后再归档。** `CM7/Application/app_camera.c` 保存当前相机业务和 R2 每轮停止事实，另有旧资格适配回调与 `App_CameraQualificationAccess`。不能用移除整个编译单元来剥离后者。历史适配器和停止测试还依赖这些入口。唯一普通 STOP 推进与保护性 STOP 不在本轮改动。
4. **本地 CM4 与已提交工程不同。** 当前用户的 `.uvprojx` 将三份 C1/C2 C 文件也登记在本地 CM4 Target；Git 基线没有这三条 CM4 登记。移动原路径会破坏用户工程。当前允许范围仅改 CM7 登记，所以保留原路径和工程字节。
5. **RTE 候选保留。** `MDK-ARM/RTE/_qwe/RTE_Components.h` 具有 Keil 生成头标记。当前两 Target 未见直接路径，但外部 RTE 生成关系未闭合，按 UNRESOLVED 保留。

矩阵列出了全部已识别的归档候选、仍活动的混合文件和 RTE 候选。未对第三方和底层驱动安排删除。
