# Repository Cleanup 依赖审计

审计基线为 `4a5db4e`。候选通过全仓跟踪路径、头文件引用、符号引用、Initcall、Keil Target、测试入口及构建文件搜索定位。矩阵记录待归档文件的直接调用和共享依赖。函数指针表沿声明和调用方核对，文本搜索无结果只作为线索。

1. **C1 队列可归档。** `CM7/Application/app_messages.c` 的 C1 Init/Send/Receive 在基线产品树中只有实现、声明和自动 Initcall；真实测试调用来自 C1、C1.1、C2.1.1 与 BASEPRI。52B、五通道及帧 Return/ACK 仍走原生产代码。
2. **C1/C2 协议不能整体归档。** `CM7/ApplicationV2/Modules/USB/usb_consumer.c` 与 `Modules/Storage/storage_consumer.c` 调用原 Tx 和 `App_QualificationNextGeneration`。路径为 Consumer → `app_camera_lifecycle_replay.c` → `app_camera_lifecycle.c` 的校验和等待事务；generation 位于 `app_camera_qualification.c`。移除这些编译单元会破坏 V2 的链接与迟到消息拒绝边界。
3. **旧 Camera 适配须隔离后再归档。** `CM7/Application/app_camera.c` 保存当前相机业务和 R2 每轮停止事实，另有旧资格适配回调与 `App_CameraQualificationAccess`。不能用移除整个编译单元来剥离后者。历史适配器和停止测试还依赖这些入口。唯一普通 STOP 推进与保护性 STOP 不在本轮改动。
4. **本地 CM4 与已提交工程不同。** 当前用户的 `.uvprojx` 将三份 C1/C2 C 文件也登记在本地 CM4 Target；Git 基线没有这三条 CM4 登记。移动原路径会破坏用户工程。当前允许范围仅改 CM7 登记，所以保留原路径和工程字节。
5. **RTE 候选保留。** `MDK-ARM/RTE/_qwe/RTE_Components.h` 具有 Keil 生成头标记。当前两 Target 未见直接路径，但外部 RTE 生成关系未闭合，按 UNRESOLVED 保留。

矩阵列出了全部已识别的归档候选、仍活动的混合文件和 RTE 候选。未对第三方和底层驱动安排删除。

## 旧 Camera 资格服务的链接证据与拆分门禁

ARMCC5 CM7 Map 显示，`App_CameraQualificationAccess`、`App_CameraQualificationPrepare` 和 `App_QualificationProcess` 在链接时已被移除。`App_QualificationNextGeneration` 仍由两个 V2 消费者链接，`App_CameraStartResource` 仍由 Legacy `App_CameraHandleCommand` 调用。不能据此删除整个 `app_camera_qualification.c` 或 `app_camera.c`。

`app_camera.c` 的 `s_camera` 仍有 988 B，旧资格 Context 与 STOP 结果字段仍占结构空间。当前可审查的下一步限于以下三个独立差异。

1. 将保持原语义的资格服务和历史 Camera 适配单独纳入 `Tests/LegacyLifecycle`，先证明原 C2.2A 与 R2 断言及用例 ID 全部保持。现有生产编译单元先保留。
2. 单独移除仅旧资格调用的 Camera Context 字段与公开入口，保留 `App_CameraStartResource`、轮次停止事实、Bridge 和 Device 的现有推进。对比普通 B1 与归档 C2.2A/R2 两套路径，分别核对首错、ACK/FAULT、真实停止与 Bridge 排空。
3. 共享代次实现需保留可编译的生产路径，再审查 CM7 工程登记。用户本地 CM4 额外引用旧文件路径，所以删除或改名会破坏这份配置。必须获得针对具体差异的单独批准，才能改动相关路径或配置。

未来清理以实际 Map 确认旧服务不再编译、共享符号仍链接，并复验 CM4 HEX、历史用例和用户 Keil 哈希。保留依据未闭合的 RTE 候选。
