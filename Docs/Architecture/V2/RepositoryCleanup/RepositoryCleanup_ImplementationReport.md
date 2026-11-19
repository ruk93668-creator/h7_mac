# Repository Cleanup 1.0 实施报告

从架构分支 P8.2 的最新提交 `4a5db4e` 创建 `codex/repository-cleanup-legacy-lifecycle`。附件中的 `2f91592` 是该提交的直接父提交。架构分支和 main 的远端分别核对为 `4a5db4e` 与 `f0ce45f`。

- `354f14e` 扫描全部 60 个 V2 C/H，整理其中 27 个文件的中文契约注释及大括号。保留正式类型、公开身份字段、ABI 和生产逻辑。ST 官方及第三方文件不纳入整理。
- `519de52` 将三条 C1 队列从产品 `app_messages.c/.h` 分离到 `Tests/LegacyLifecycle`。保留五条 52B 队列、历史 56B 协议以及原断言。
- 清理可交付部分为 V2 规范整改和 C1 旧队列归档。C1/C2 共用的协议、Replay、generation 及混合 Camera 适配暂留。

基线有 3,484 个 Git 跟踪文件，其中 1,503 个 C/H。实现提交增加 3 个测试专用 C/H，变为 3,487 个跟踪文件、1,506 个 C/H。审计文档另计。真正删除文件数为 0。产品行为差异限于不再自动创建三条已无产品调用的 C1 队列。

进一步归档需要先确认共享服务保留方式和 CM4 用户 Target 的处理权限，再独立拆分旧 Camera 资格适配并复验。P8.3、Grant → Frame Lease 和 Stable Admission Closure 不在本轮验收范围，两项证明仍为 NOT_PROVEN。

## 共享代次接口续审

从 `db02062` 继续。新建 `CM7/Application/app_camera_generation.h`，V2 USB、Storage 只包含共享代次声明。旧资格头转为包含共享声明，原函数签名、实现、符号和 Keil 文件路径保持。此轮没有移除产品源、测试或历史证据。

旧 Camera 资格服务整体归档仍待审批。自动审批拦截了在同一操作中删除资格头、剥离 `app_camera.c/h` 并批量改接历史测试的尝试。该尝试没有写入任何文件。
