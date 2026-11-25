# CM4 OLED 架构审核

- `app_oled.c` 经 Device 注册表取得 SSD1306 设备。原 Framework Timer 每 50 ms 调度菜单，监控首页每 100 ms 绘制。忙时只保留一次菜单更新请求。输入在主循环消费，页面切换会丢弃属于旧上下文的事件。
- App 在 IDLE 时保存 512 B 回滚副本，绘制或提交失败时恢复。提交成功后 Device 持有唯一的传输 Framebuffer，轮询推进四组页命令和数据。最后一页成功后才累计完成帧。回滚副本从不用于传输。
- `Application/UI` 管理页面状态、输入解码、菜单、工具、动画、绘图及只读显示模型。`BSP/bsp_u7` 独占 GPIO 访问。PI6、PC15、PI9、上拉和 LSE 保护沿用开发支线。设备驱动、I²C2、Framework 和启动路径沿用主线。
- Tools 保留 List、Tiles、六种光标、三种运动曲线、滚动、过渡、窗口、整数及定点小数编辑、开关、局部列表、图形、Bitmap 与动画图库。编辑只提交到本地选项数组。Font 单独处理字模与裁剪。
- Fonts、图标和动画资源可从 [资源来源记录](../../UI36/oled-ui-upstream.md) 追溯。参考 OLED_UI 版本 `8f02069f890e5ac074b41100fb0651887c821ce8`，保留 [Apache-2.0 文本](../../UI36/OLED_UI.Apache-2.0.txt)。
- 每帧 4 组命令和数据，载荷为 `4 × (4 + 129) = 532` 字节。每笔再含一个地址字节，至少 4860 个 SCL 脉冲，另外计 START、STOP、设备与异步调度时间。UI1 历史整帧 49.56 ms 含观察和调度。该读数不能作为本候选的总线速率或连续完成帧率。完成间隔可从 `g_oled_completed_interval_ms` 观察，整帧计数使用 `g_oled_monitor_debug.completed_frames`。
