# UI3–UI6 实板准入交付

**软件状态：READY_FOR_BOARD_ACCEPTANCE。硬件：NOT_RUN。** 起点为已验收的 UI2 提交 `bf6cf9f`。UI3 输入为 `1cd5566`，UI4 动画弹窗为 `b3f9cc8`，UI5 状态页为 `1103cf2`。UI6 提交包含长稳回归、可选计时与交付证据。正式版本仍以四行 IPC 监控开机。

## 页面与数据

```text
MONITOR
└─ SYSTEM MENU
   ├─ Camera
   │  ├─ Status
   │  ├─ Format
   │  ├─ USB
   │  └─ Back
   ├─ Network          ETHERNET N/A 与 Back
   ├─ Storage
   │  ├─ Info         SD / RECORDING N/A
   │  └─ Back
   ├─ Diagnostics
   └─ Back            返回 MONITOR
```

[UI3 输入事件](ui3.md)、[UI4 视觉机制](ui4.md)、[UI5 IPC 来源与缺失矩阵](ui5.md)。UI1 与 UI2 的证据和实现提交仍保留。正式菜单与调试菜单使用同一状态机、当前 IPC Service 本地快照、UI1 绘图和原 100 ms 刷新事务。U7 及软件注入经过 8 项队列。App 在 IDLE 使用原 512 B 备份、绘制、回滚和提交。动画不阻塞，刷新期间不增加画面写入。BSP、Device、Framework、公共 IPC、CM7 源码未变。Keil CM7 Target 的字节内容与基线一致。

## 验证结果

| ARMCC/Unicorn | O0 | O2 | 断言合计 |
|---|---:|---:|---:|
| UI1–UI6 综合回归，模拟事件入口开启 | 82 PASS | 82 PASS | 457364 |
| 关闭输入入口的原 OLED 回归 | 69 PASS | 69 PASS | 60480 |
| 综合中的长稳用例 | 6000 帧 PASS | 6000 帧 PASS | 396016 |

[综合逐例记录](results.json)。用例 0–76 保留 UI1 图元、IPC 监控、UI2 页面以及刷新与回滚。76 同时覆盖 UI3 反向、溢出、动作优先和旧页丢弃。77–78 覆盖 UI4 动画回绕、中间帧、滚动、长文本、弹窗复制、确认、取消和超时。79–80 覆盖 UI5 有效、过期、失联、读错、故障及缺失来源。81 在两个优化档各跨 Tick 回绕运行 6000 个 100 ms 周期，轮换页面与 IPC 状态，在忙期投递输入，检查每页实际发送数据和整帧快照。该用例首轮耗尽模拟器指令预算，扩大预算后按原 6000 帧长度和断言通过。宿主运行时间约 10.55 / 8.50 s，不能换算为 MCU 耗时。

## 构建与资源

Keil ARMCC 5.06u7 build 960 完整重建，正式、调试均 **0 Error、0 Warning**。保持原 Cortex-M4 启动文件、CM4 Application include path、优化设置和 CM4 scatter。正式构建没有测试邮箱或 DWT 符号；调试副本在同一 CM4 Target 定义 `OLED_UI2_TEST_INPUT=1,OLED_UI6_TIMING=1`。构建日志见 [正式](keil-release.log.txt) 与 [调试](keil-debug.log.txt)。

| 镜像 | Code | RO | RW | ZI | Flash (Code+RO+RW) | 静态 RAM (RW+ZI) |
|---|---:|---:|---:|---:|---:|---:|
| UI2 正式 | 26390 | 1438 | 936 | 3576 | 28764 | 4512 |
| UI6 正式 | 32154 | 2010 | 952 | 3696 | 35116 (+6352) | 4648 (+136) |
| UI6 调试 | 33002 | 2010 | 956 | 3748 | 35968 (+7204) | 4704 (+192) |

队列容量 **8 项**，MAP 状态大小为队列 16 B、页面 28 B、动画 16 B、单弹窗 44 B、输入解码 16 B。原 512 B 回滚备份保留。启动配置保留 1024 B 栈。Keil 调用图的已知路径深度为正式 512 B、调试 520 B，刷新回调自身分别用 248 / 256 B。链接器存在未追踪的间接调用，不能把已知深度当成全系统栈水位；含中断叠加的峰值需上板测量。

正式工作树在 `D:\32\STM32H7\h7_mac_oled_ui`。以下构建从该分支的干净配置生成，独立于用户原有 Keil 设置。两套工程均可直接用 Keil 打开，选择 `h7_mac_CM4`；旧 `program_cm4.ini` 可能指向另一工作树，请用当前 Target 的 Download 或明确指定下面的 HEX。

| 构建 | 项目路径 | HEX 相对工程目录 | SHA256 与 Flash 范围 |
|---|---|---|---|
| 正式 | `D:\32\STM32H7\h7_mac_ui\UI36-candidate\MDK-ARM\h7_mac.uvprojx` | `h7_mac_CM4/h7_mac_CM4.hex` | `8fa7ead59a394d8287c334bdf9b79ab05667d4c208f8cece540a78f65c8350d4`，0x08100000–0x0810859F |
| 调试 | `D:\32\STM32H7\h7_mac_ui\UI36-debug\MDK-ARM\h7_mac.uvprojx` | `h7_mac_CM4/h7_mac_CM4.hex` | `61045f4c658d2dd12d45a68b34a197ec5d29fc597bbed29a5b3a8518a49cb414`，0x08100000–0x081088EF |

均核对 HEX 校验和、CM4 Flash 范围、ARM AXF 和 MAP。产物与本地构建日志不混入生产提交。

## 最终实板清单

以下实板项目均 **NOT_RUN**。操作只烧 CM4。若选择一次连续验收并需要 DWT 时间证据，烧调试镜像，结束后可切回正式镜像。整个功能路径在调试镜像中也支持用实体 U7，正常使用不必写测试邮箱。

1. 冷启动确认 OLED 显示原四行，检查 `g_oled_monitor_debug.completed_frames` 持续增长且错误不增加。保留首次黑屏的 UI1 历史记录与用户操作归因。
2. 短按 U7 进入 SYSTEM。验证正反转、首尾边界、快速来回、短按选择、长按约 700 ms 返回。实物方向如与期望反向，在基于同一源码的 CM4 Define 设置 `OLED_UI3_REVERSE=1` 复核。若 `g_oled_ui2_u7_result=SYS_ERR_NOT_SUPPORTED`，核对 LSE，保留监控并用调试邮箱验收菜单，不关闭 LSE。
3. 验证超过三项时滚动、选中带移动、标题过渡、返回后游标恢复。进只读详情短按打开弹窗，分别确认、取消和等超时。Network 或 Storage 可观察长说明滚动。
4. 检查 Camera、USB 合法值与业务状态，Network、Storage 的 N/A，Diagnostics 的 CM7、掩码及 OLED 统计。如探针支持，独立暂停 CM7 而保持 CM4 运行，确认 OFFLINE 和恢复后重新显示新鲜数据。不要把无字段的 N/A 判成故障。
5. 连续运行并记录错误与完成计数，观察闪烁、错页或卡顿。需要核查 Framebuffer 所有权时，在同一帧处于 WAIT_CMD 和 WAIT_DATA 时取 512 B 快照比较；断点观察和性能采样分开。建议在首轮板测中包含至少 30 分钟正常运行。
6. 调试镜像 Watch 展开 `g_oled_ui_timing`。确认 `dwt_ok=1` 和实际 `clock_hz`，持续切页、滚动和弹窗，不停核运行后采样 `render_cycles/max` 及 `refresh_cycles/max`（字段使用 `*_max_cycles`）。用 `1000 × cycles / clock_hz` 得毫秒。渲染时间包含状态模型、页面和弹窗；整帧从第一次提交前到四页完成。最大值为该次运行观察到的峰值，不是理论上界。实板绘图、峰值、整帧耗时与栈高水位尚无测量结果。

实物编码器绝对方向、极快旋转下限、潜在 LSE 冲突、真实业务缺失字段以及实板稳定性均留在最终审核范围。未经审核不标记 HARDWARE_PASS。
