# M0 功能冻结矩阵

PASS 的范围仅限对应证据。当前候选没有新增任何实板实测。

| 功能 | 状态 | 证据和界限 |
|---|---|---|
| 原四行 IPC 监控 | PASS | UI1 历史实板记录及候选 O0/O2 默认回归。本候选上板复核 NOT_RUN |
| UI1 线、矩形、圆角、反色、位图与 ASCII | PASS | 原 UI1 验收和当前用例 0–68 |
| UI2 菜单、分页、滚动、回退 | PASS | UI2 用户总体验收及候选用例 69–75 |
| UI3 U7 解码、按压去抖、事件队列 | PASS（软件） | 用例 76。候选的 PCB 方向、快转和 LSE 检查 NOT_RUN |
| UI4 动画、标题过渡、文本滚动、弹窗 | PASS（软件） | 用例 77–78、86–88。局部切换队列缺陷已单独整改 |
| UI5 Camera、USB、Diagnostics | PASS（模型与渲染软件） | 79–81。使用现有 IPC 快照，不假定业务侧总是在线 |
| Network、Storage 实际状态 | BLOCKED | 当前 IPC 没有来源，显示 `N/A`，现有缺口保留 |
| UI6 集成、回滚及长稳 | PASS（软件） | 81 在 O0/O2 各模拟 6000 帧，见回归记录。候选长稳实板 NOT_RUN |
| 菜单 50 ms 调度及忙期处理 | PASS（软件） | 82 验证单工、旧帧不被修改和待请求处理 |
| 实际完成帧率达到 20 FPS | NOT_VERIFIED | 仅有旧 UI1 实板时序作为背景，当前硬件 NOT_RUN |
| OLED_UI 常用组件 | PASS（列出范围） | List/Tiles、六光标、四字号、窗口、编辑器、位图和滚动，86–87、36 帧模拟证据 |
| OLED_UI 完整功能和视觉等价 | NEEDS_FIX | 旧页逐级渐隐、任意回调和辅助 API 等仍有差异。亮度、休眠及姿态数据受冻结接口限制 |
| System Monitor 菜单、注入模型及诚实的空值显示 | PASS（软件） | 83–85，含截断、低栈、会话与过期 |
| System Monitor 真实 CM7 任务栈、Heap、CPU 发布 | BLOCKED | 无已审核的发布通路，公共 ABI 未变。当前 FreeRTOS 关闭动态分配和运行时统计 |
| 候选实板验收 | NOT_VERIFIED | 所有本候选实板项目为 NOT_RUN |

历史证据见 [UI1](../UI1/hardware-supplement.md)、[UI2](../UI2/acceptance.md)。完整差异见 [参考对照](../UI36/oled-ui-upstream.md) 和 [System Monitor 通路](../UI36/system-monitor-design.md)。
