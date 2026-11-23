# 菜单动画 20 FPS 调度目标

在 UI6 `f8f9629` 基础上，同一个 OLED Timer 每 50 ms 提供一次菜单刷新机会，原四行监控仍以 100 ms 限频。保持原 IPC 快照、备份、渲染、回滚和异步提交。忙时跳过该拍，不积压、不改写正在发送的 Framebuffer。动画时长仍按毫秒，未调整 I²C 配置。

O0/O2 综合回归 166 PASS、0 FAIL、457420 条断言。新增用例 82 验证单 Timer、50 ms 菜单、100 ms 监控、传输忙时逐字节不变及下一拍恢复。关闭输入入口的回归 138 PASS、0 FAIL。两版 Keil ARMCC5 完整重建均 0 错误、0 警告。相对上一正式版，Flash +52 B，静态 RAM +8 B。

| 镜像 | 本机 Keil 工程目录 | HEX SHA256 |
|---|---|---|
| 正式 | `D:\32\STM32H7\h7_mac_ui\UI36-20fps\MDK-ARM` | `0c46dfcc277db0ed25ddf9e4d87256387e2add5cc3df1317f5434362472f3d9a` |
| DWT 调试 | `D:\32\STM32H7\h7_mac_ui\UI36-20fps-debug\MDK-ARM` | `249ecaeb837d5f4ad400aac08ef90bf1b57abedb6b6054e57530727f371e7d52` |

两版 HEX 均为 `h7_mac_CM4/h7_mac_CM4.hex`，仅覆盖 CM4 Flash。使用当前 Keil 工程的 `h7_mac_CM4` Target。实板帧率 NOT_RUN，20 FPS 是调度上限。用调试镜像在菜单连续运行至少 10 秒，记录 `completed_frames` 的增量和实际观测秒数，核对 `g_oled_ui_timing` 的绘制和整帧时间。如果两者合计跨过 50 ms，可能错过下一拍。首次黑屏记录不变。
