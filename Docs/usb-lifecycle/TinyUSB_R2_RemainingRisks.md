# TinyUSB R2 剩余风险

**R2总体BLOCKED；当前交付为可完整链接的隔离实验固件和已验证软件部分，不允许宣称生产就绪。**

1. **UNRESOLVED/BLOCKED：活动Bus Reset。** 真实AXF观察到EPENA仍设置时锁存fault，未达到活动Bulk Reset后重新配置要求。RM0399 Rev5 60.15.6 p2903给出Reset后EP0初始化；60.14.55/61的USBAEP说明复位清活动标记，不能据此推导EPENA/PIO/RX和旧事件都退休。p2909–2911 Reset型OUT停止还涉及全局NAK/共享RX及EP0 OUT禁止EPDIS边界。需要在原DCD事件机制中证明这些事实后才允许Core/Class reset，不直接删检查。
2. **UNRESOLVED/BLOCKED：Disconnect正常恢复。** 当前活动PIO端点Disconnect保留资源并进入fault，阻止Core重用Class Buffer；未证明正常断开后无需复位即可重新枚举。已有未活动事件回调不能代替活动停止证明。
3. **UNRESOLVED/BLOCKED：活动close_all及SET_CONFIGURATION(0)。** 已退休/空组现在可安全关闭；void接口不能表达活动组PENDING，目前仍拒绝。需要最小DCD结果/进度接口以及Core既有Task的请求延后资格；旧控制请求被新SETUP抢占时不得发送旧Status。不能在本轮宣称完整解决。
4. **UNRESOLVED/BLOCKED：DeInit失败顺序。** 正常取消成功后Stop/Restart已验证；失败路径仍可能先关IRQ或pullup，需要先证明资源静止或保留进度能力。未添加软件重启、强清busy、清fault或Watchdog假恢复。
5. **UNRESOLVED/BLOCKED：EP0 active IN与PIO ZLP。** 不能用普通Bulk禁用处理OUT0；未形成合法异步EP0抢占资格。ES0445 2.24.5未修复；ZLP模型观察不是勘误验收。
6. **NOT_TESTED：实板时基/调度/IRQ。** 已在端口启用并检查DWT，CPU时钟要求480MHz；SysTick提供WFI期间的异步期限，DWT仍检查停走、频率变化与同步等待。真正硬件频率、IRQ优先级、FreeRTOS抢占和Task饥饿需后续验收。当前模型直接提供调度器已准备输入，不等于跑完真实启动。
7. **VERIFIED（隔离限制）：只有RAM LUN。** 原RAM 16x512B，未格式化FAT、不接SD、无StorageTask/Camera/UVC完整移植，不从RAM测试推导SD DMA安全。实验USB身份1209:0001为非唯一内部测试PID，不能分发/销售/制造；没有正式独占产品PID。
8. **NOT_TESTED：Host/HS/DMA/其他MCU。** 维持R1B边界；旧Host忽略dwc2_core_deinit结果仍未修复，Host未启用。本轮无上游版本升级，无旧UDLM回迁。

VERIFIED：原R1两项FAIL保留；原R1B/MSC/等待回归通过，无删除旧断言。完整固件0错误0警告并不关闭以上功能门禁。后续范围仍应沿TinyUSB DCD/Core/MSC现有事件与Task最小扩展推进，不能重新创建应用生命周期框架。

结论：保存已完成代码、完整AXF/MAP/HEX、失败及未完成证据，普通提交推送研究分支后停止，等待集中人工审核；不自动烧录、不合并main。
