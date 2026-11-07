# CM4 OLED系统监控（Stage05D）

## 模块职责

- `oled_ui.c/.h`：静态ASCII字库、6×8绘图、四行排版、FPS/整数格式化、坐标与文本裁剪。
- `app_oled.c/.h`：取得IPC Service完整副本、100ms Timer、绘图/发送时序、错误计数和512B失败回滚。
- `oled_device.c/.h`：原SSD1306 Device Ops和四页异步IIC刷新，仅删除A5强制全亮测试。
- `IPC_Service_GetApplicationStatus()`：已有V3本地视图，提供valid_mask、fresh_mask、peer_state；应用不读取g_ipc。

## 一帧的所有权

```text
OLED IDLE
  → 读取一份IPC本地快照
  → 备份旧Framebuffer（512B）
  → UI clear + draw_pixel
  → refresh提交CMD0成功
  → 冻结Framebuffer，Process推进四页
  → DATA3成功 → IDLE
```

IDLE之前不取样、不清屏、不绘图。提交成功后备份不参与发送，设备始终读取原Framebuffer。
绘图失败或提交失败时恢复备份；BUSY跳过等下一周期，不阻塞主循环，也不覆盖last_error。
发送中真实IO/TIMEOUT错误由原Device/BSP交付，应用计数上报；下一周期重试刷新，没有新增IIC底层自动恢复机制。

## 显示规则

每行最多21字符，4行。正常仿真示例：

```text
CAM 320x240 MJPG
CAP 10.0 USB 9.5
DROP 12
M7 OK CTRL RUN
```

以上为排版示例。实际使用CM7发布的width/height/format、camera_fps_milli、usb_fps_milli、dropped_frames和Control状态。
Y8、RGB565、YUY2、MJPG均有正式映射；Camera/USB停止显示STOP，停止中显示HALT。
USB任务已启动但完成FPS为0显示WAIT，不能声称Windows正在收流。FPS是最近测量窗口，不是目标FPS，也不是主机端独立测量。
valid=0显示INVALID/--，fresh=0显示STALE；peer非ONLINE优先显示初始化/超时/协议错误，并隐藏旧FPS。
FPS≥100时显示>99避免挤占第二路字段；uint32计数完整显示。新画面先全清再绘图，长文本变短不会残留。

## Watch与计数含义

CM4：

| 表达式 | 含义 |
|---|---|
| `g_oled_monitor_debug.submitted_frames` | 已成功提交第一页命令的整屏刷新次数 |
| `g_oled_monitor_debug.completed_frames` | DATA3成功完成的整屏次数，失败帧不计入 |
| `g_oled_monitor_debug.busy_skips` | 上一帧未完成或IIC提交BUSY时跳过次数 |
| `g_oled_monitor_debug.io_errors` | IIC提交/完成真实错误计数，不含BUSY |
| `g_oled_monitor_debug.render_errors` | 设备绘图失败次数 |
| `g_oled_monitor_debug.ipc_errors` | 取得本地IPC视图的接口错误次数 |
| `g_oled_monitor_debug.last_error` | 最近真实错误，后续成功不自动清除 |
| `g_oled_monitor_debug.peer_state/valid_mask/fresh_mask` | 最近界面采用的通信及来源状态 |
| `g_ipc_application_debug` | 最近IPC Timer生成的完整本地业务视图 |
| `g_ipc_runtime_debug` | 本核观察到的心跳/读取/超时诊断 |

CM7：g_app_status_debug是IPC任务真实汇总；g_ipc_task_debug.stack_free_words是历史最小剩余栈Word；g_ipc_runtime_debug.max_step_gap_ms是实际处理最大间隔。
同名符号属于各自ELF，不要混用核或旧构建。调试器逐字段读取不保证所有Watch字段同时更新。

## 实板步骤（待执行）

1. 使用本分支当前Keil工程的h7_mac_CM7、h7_mac_CM4配套V3固件，CM7视频配置沿用用户本地版本；烧录后完整断电冷启动。
2. 确认CM4 init_error=0，IPC两端ONLINE，OLED不再强制全亮；核对分辨率、格式、两路实际FPS、DROP及Control。
3. 开/关主机视频，核对USB完成FPS由真实统计更新；没有完成帧时显示WAIT。没有USB主机连接状态字段，不扩展协议作推断。
4. 在可控调试环境暂停CM7并让CM4继续运行，观察超时后显示M7 OFFLINE且不显示旧FPS；恢复CM7，观察心跳进展及界面恢复。单核热复位不在此验收范围。
5. 用已有正常停止/启动命令检查Camera、USB STOP/HALT/RUN对应显示，不直接在Watch改共享字段伪造业务状态。
6. MJPEG与OLED并行至少5分钟。记录起止camera_fps_milli、usb_fps_milli、captured_frames、dropped_frames、usb_completed_frames；计数增量按uint32无符号计算。
7. 记录CM7 IPC max_step_gap_ms、scheduling_delays及g_ipc_task_debug.stack_free_words；观察OLED submitted/completed/io_errors、乱码、花屏、撕裂、IIC锁死。无实测值时填“未验证”。

建议记录表：

| 项目 | 开始 | 5分钟后 | 结论 |
|---|---|---|---|
| Camera/USB实测FPS | 待测 | 待测 | 未验证 |
| 采集/USB完成帧数 | 待测 | 待测 | 未验证 |
| DROP及5分钟增量 | 待测 | 待测 | 未验证 |
| IPC最大间隔/调度延迟 | 待测 | 待测 | 未验证 |
| IPC栈余量Word | 待测 | 待测 | 未验证 |
| OLED完成数/错误数 | 待测 | 待测 | 未验证 |
| 停止/失联/恢复界面 | 待测 | 待测 | 未验证 |

## 边界与风险

本阶段不修改CM7数据路径、共享协议、时钟、启动或Reset流程。512B备份+48B诊断使CM4 ZI增加560B，字库475B在Flash。
字体和刷新时序已做ARM软件仿真；IIC电气、真实显示方向、5分钟性能及LINK/NRST供电问题不由仿真证明。
CM4主栈仍为启动文件定义的1024B，UI行缓冲88B加上调用链和中断嵌套的实际余量待实板检查。
UI绘制在CM4主循环中同步执行，最多4×21×48个像素调用；100ms是触发周期，实际整屏刷新受IIC及主循环耗时影响。

软件证据见Tests/OledMonitor/verification_report.md、results.json、regression_results.json、build_results.json及两核完整重编日志。

## 用户实板反馈与主线合并

- 用户已反馈“OLED显示正常”，确认基本显示可用；这不替代下述五分钟性能、失联恢复和栈余量验收。
- 用户反馈视频运行数分钟后延迟增大，关闭并重新打开播放器会重置延迟；低延迟ffplay参数未消除增长。根因尚未确定，用户决定留待后续排查。
- 用户已授权将05D及其05B/05C IPC依赖合入main。此合并不包含视频延迟修复，不将延迟问题记为已解决。
- 合并前main与origin/main均为442cd4be953340ce5ce0a033fb053a13e367f047，来源分支提交b06717783e70a277220a3f3da37f424ab56fcf4e。
- 本次在暂存用户配置修改后的干净候选上重新执行软件回归及两核完整重编；实际结果见Tests/OledMonitor/main_merge_verification.json。
- 用户随后明确要求不恢复个人修改并取消备份；本次暂存的个人视频配置、IPC超时、Keil设置及旧日志修改不恢复、不上传，清除本轮专属stash和备份，使用主线已提交版本。未跟踪文件保留。
