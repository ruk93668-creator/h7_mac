# OLED_UI 适配与 System Monitor 候选验证

本轮从 `57eb588` 开始，参考 SHA `8f02069f890e5ac074b41100fb0651887c821ce8`。适配候选可供实板验证。**实板实际 20 FPS 为 NOT_RUN，CM7 诊断发布为 BLOCKED，完整视觉等价仍有对照矩阵列出的差异。**

SYSTEM MENU 新增 System Monitor（Tasks / Memory / CPU / Runtime）和 UI Tools。UI Tools 自带交互设置：列表或 Tiles、六光标、运动曲线、字高、弹窗形状、整数时长、定点小数滚动速度、主题、帧率、文字窗、位图/GIF、空页、局部列表、空窗、形状和 About。旋转改变选择或草稿值，短按选择/确认，长按取消/返回。全部参数留在 CM4 UI，未写 CM7。UI1 原监控和异步事务保留。上游来源、许可、组件差异与硬件限制见 [功能对照矩阵](oled-ui-upstream.md)。

System Monitor 当前由 IPC 真实状态显示 CM7 链路和本地 CM4 Tick。CM4 uptime 为毫秒计数换算，随 32 位 Tick 回绕。在未发布诊断时 Tasks、Heap 和 CPU 显示 NOT PUBLISHED / N/A，CM7 uptime 显示 N/A。注入测试验证固定容量 8、稳定 ID 与会话、名字、状态/优先级、字节单位栈、高水位告警、历史峰值、退出重建、截断、失联、过期，以及 Heap 和 CPU 的有效性。没有 CM7 采样生产代码，因此没有新增采集开销，也尚不能衡量未来采集对 Camera/USB/Storage 的影响。待审核的独立快照通路见 [数据通路设计](system-monitor-design.md)。

## 20 FPS 分析

现有 BSP 的 120 MHz I²C 内核时钟和 TIMINGR `0x307075B1` 对应 PRESC=3、SCLL=177、SCLH=117。忽略滤波、同步、上升沿、起停和调度时，SCL 周期约 9.87 µs（约 101.35 kHz）。现有 Device 每帧发四页，每页命令事务为地址+4 字节，数据为地址+129 字节，两者各字节含 ACK，共 4860 个 SCL 周期，理想预算约 **47.95 ms**，只剩约 **2.05 ms** 给实际附加成本与绘图。此前 UI1 R2 测得 **49.56 ms**，包含软件推进和当时的检查开销，不能作为纯总线时长，也不能作为本版实测。

菜单由原 Timer 每 50 ms 提供请求。若旧帧仍在发送，只保留**一个**待绘制请求，合并中间状态。最后一页成功后，主循环通过同一 `App_OledRefresh` 尝试最新帧，避免因末页刚好越过截止点而白等一拍。两次菜单尝试间隔仍至少 50 ms，监控维持 100 ms。所有写入继续经过原 IDLE 检查和备份回滚，Device/BSP 未修改。

调试版 `g_oled_ui_timing` 有 `render_cycles`、`refresh_cycles`、`command_cycles[4]`、`data_cycles[4]`、`completed_interval_cycles`、`defer_ms` 和峰值；页面段为主循环观察到的阶段差，含传输、推进、轮询和启动下一段的开销。逻辑分析仪抓 SCL/SDA 才能分离纯总线耗时。实板连续动画至少 60 秒并记录前后完成数及实际秒数，验收 `Δcompleted_frames / Δt >= 20`，错误为零、无长期积压，并核对采样的 512 B 帧没有污染。若不足，先用上述证据区分 CPU、总线或调度，脏页/区刷新或 Fast-mode I²C 需独立审核后才可改冻结层。

## 可复核证据

[完整逐例 O0/O2 软件结果](compat-results.json)；82 验证截止点被占用时的旧帧一致性与完成后重试，83–85 验证 System Monitor 模型，86–87 验证字模、数值编辑和运动。运行 `python Tests/OledMonitor/run_tests.py --ui2` 后可用 `python Tests/OledMonitor/render_compat.py output/OledMenu/compat-frames.bin previews` 重放真实仿真 Framebuffer。GIF 播放节拍是说明性的，根据用例实际采样时刻在总览标注，不构成实板帧率数据。

[36 帧总览](previews/frame-sheet.png) · [光标](previews/cursor.gif) · [Tiles](previews/tiles.gif) · [长文本](previews/marquee.gif) · [弹窗与小数](previews/editor.gif) · [整数](previews/integer.gif) · [布尔开关](previews/toggle.gif) · [局部窗口](previews/viewport.gif) · [Gallery](previews/gallery.gif)。

| Keil ARMCC5 完整重建 | Code | RO | RW | ZI | Flash | 静态 RAM |
|---|---:|---:|---:|---:|---:|---:|
| [CM4 正式](compat-keil-cm4.log.txt) | 39534 | 19990 | 976 | 3776 | 60500 (+25332) | 4752 (+96) |
| [CM4 调试](compat-keil-debug.log.txt) | 40546 | 19990 | 992 | 3864 | 61528 (+25508) | 4856 (+152) |
| [CM7 原配置](compat-keil-cm7.log.txt) | 137690 | 3114 | 1340 | 100932 | 142144 | 102272 |

均为 0 Error/0 Warning。CM4 增量与 `57eb588` 同类构建比较。CM7 Target 与基线逐字节一致，无生产改动。Keil 已知栈路径深度 668 B 正式、676 B 调试；原配置 1024 B，调用图含未追踪间接调用，不能据此保证实板峰值。字模为主要 Flash 增量；正式适配状态 64 B，计时结构 72 B 仅调试启用。原 512 B 备份仍在。

| 构建 | 本机 Keil 项目目录 | CM4 HEX SHA256 |
|---|---|---|
| 正式 | `D:\32\STM32H7\h7_mac_ui\OLED-compat\MDK-ARM` | `9c15a902182ebe4c0ed31d154b5191d584ff7c2488dfccf698c2550ae8e4bcd1` |
| DWT 调试 | `D:\32\STM32H7\h7_mac_ui\OLED-compat-debug\MDK-ARM` | `2297e087b9b5d00effac3bb48d39b9680f415f24d1c63c4cfc7143ab74f41f2a` |

选择 `h7_mac_CM4`，只烧当前目录 `h7_mac_CM4/h7_mac_CM4.hex`。两版均验证 HEX 校验和与 CM4 Flash 起址 0x08100000。CM7 仅为编译验证，不需烧录。板测沿用 [UI6 清单](acceptance.md)，新增 System Monitor 的 N/A 与在线/离线检查，以及 UI Tools 全部模式和持续动画测量。首次黑屏历史不改写。
