# UI5 只读系统状态页

页面路径为 SYSTEM → Camera → Status / Format / USB，SYSTEM → Network，SYSTEM → Storage → Info，SYSTEM → Diagnostics。诊断复用 UI2 的 About 编号，USB 追加编号，页面状态扩展至 28 字节。根仍为四行监控。

| 展示 | 允许使用的数据 | 缺失或受限 |
|---|---|---|
| Camera | 当前本核 IPC 快照的 camera_state、camera_result、width、height、format、camera_fps_milli、dropped_frames | 未知尺寸和格式显示 N/A |
| USB | usb_state、usb_result、usb_fps_milli、usb_completed_frames | 显示的 SOURCE 是有效且新鲜的 Camera 源格式，不是 USB 协商格式。协商参数未暴露 |
| Network | 无已有 CM4 只读 Ethernet 状态 | ETHERNET N/A |
| Storage | 无已有 CM4 只读 SD、挂载、录像或文件状态 | SD / RECORDING N/A |
| Diagnostics | CM7 peer_state、IPC valid/fresh/read_result 与当前 CM4 OLED completed/io_errors/render_errors | 诊断掩码是本地快照，不代表其他业务支线 |

状态优先级为对端未在线、读取错误、无有效数据、数据过期、源错误、有效值。BUSY 可保留符合新鲜度的缓存。IPC Service、共享协议及业务控制保持原状。

O0/O2 合计 162 PASS、0 FAIL、61348 条断言。测试覆盖有效数值、过期、无数据、失联、IPC读取错误、USB故障、缺失接口及诊断数据。实板 NOT_RUN。
