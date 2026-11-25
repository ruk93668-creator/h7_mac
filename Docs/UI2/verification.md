# UI2 软件验证与实板交接

设计基线为 `c56f956`，UI1 验收为 `2e77ef9`。页面模型、Renderer 与 Application 集成分别提交于 `4081e3a`、`2714302`、`b638eea`。本阶段保持正式镜像的四行 IPC 监控。

| 文件 | 实施职责 |
|---|---|
| `oled_ui_page.c/.h` | 9 页只读表、最多 5 项、分页面保存游标及窗口、事件分发 |
| `oled_ui_menu.c/.h` | 128×32 标题和三行菜单、滚动和反色高亮 |
| `app_oled.c` | 编译期开关、主循环调试邮箱、IDLE 事务内切换 Renderer |
| `MDK-ARM/h7_mac.uvprojx` | 只在 CM4 Target 登记两个源文件 |
| `Tests/OledMonitor/` | 同一 ARMCC/Unicorn runner 增加 UI2 模式，原期望保持 |

状态转换见 [页面设计](page-design.md)。页面逻辑不接触硬件。App 在原 Timer 的 IDLE 事务中备份旧帧并渲染、提交。菜单事件在忙期间仍可更新逻辑状态。软件用例证明现有 512 字节帧在各页传输期间保持原样，以及渲染或提交失败后恢复旧帧。未新增生产 Framebuffer、Timer、IRQ 或 IPC 协议。

| 编译及验证 | O0 | O2 | 总断言 |
|---|---:|---:|---:|
| 默认构建，原 69 个 OLED 用例 | 69 PASS | 69 PASS | 60480 |
| 菜单构建，原 69 个及新增 7 个 | 76 PASS | 76 PASS | 61078 |

新增用例 69–75 覆盖页面表关系与非法 ID、连续输入与游标恢复、空页和单项、屏边与跨行高亮、两个忙状态的互斥、下一帧显示与返回监控，以及三种失败后的 512 字节回滚和重试。结果 JSON 保留逐例优化档、断言数和失败位置。

三套 Keil ARMCC 5.06u7 build 960 均完整重建，0 错误、0 警告。Flash 使用 Code+RO+RW，静态 RAM 使用 RW+ZI。

| 构建 | Code | RO | RW | ZI | Flash | 静态 RAM |
|---|---:|---:|---:|---:|---:|---:|
| UI1 基线 | 26390 | 1438 | 936 | 3576 | 28764 | 4512 |
| UI2 正式默认 | 26390 | 1438 | 936 | 3576 | 28764 (+0) | 4512 (+0) |
| UI2 临时菜单 | 28090 | 1902 | 936 | 3616 | 30928 (+2164) | 4552 (+40) |

运行 `python Tests/OledMonitor/run_tests.py` 或加 `--ui2`。需要 Unicorn 和 pyelftools。菜单构建在隔离副本的 CM4 C/C++ Define 加 `OLED_UI2_TEST_INPUT=1`。正式工程不设置此宏。

## 实板步骤

本机测试工程 `D:\32\STM32H7\h7_mac_ui\UI2-board\MDK-ARM\h7_mac.uvprojx`。选择 `h7_mac_CM4`，只下载这个工程的 CM4 镜像。启动先显示原四行。在 Watch 展开 `g_oled_ui2_debug`，先写 `event`，最后更新 `request`，继续运行并等待 `ack=request`、`result=0`。一条确认后再发下一条。

| event | 动作 |
|---:|---|
| 1 | 从监控打开 SYSTEM MENU |
| 2 | 下一项，选择超出三行可见区域时滚动 |
| 3 | 上一项 |
| 4 | 进入选中项，或执行 Back 条目 |
| 5 | 回父页，在根页原地停留 |

建议顺序为 1、4、2、4、5、5，核对从监控进入 SYSTEM、CAMERA、Format 静态页，再逐级返回且游标恢复。回到 SYSTEM 后反复发 2，确认可见窗口滚动，首尾钳位，再用 5 回到四行监控。忙期 Framebuffer 所有权已有软件证据；实板可在 `WAIT_CMD` 与 `WAIT_DATA` 时采样 512 字节比对、观察完成计数。耗时可用独立 DWT 或总线测量。实板菜单、刷新和耗时均为 NOT_RUN，等待现场证据及审核。
