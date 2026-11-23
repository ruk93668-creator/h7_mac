# CM4 OLED UI1 验证记录

GitHub API 和 Git 远端引用核对的 main 基线为 `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`。开始前本地与远端均没有 `codex/cm4-oled-ui`。原工作区有 CM7 摄像头及 Keil 改动，并有未跟踪的架构文档、测试和输出。UI1 在独立工作树 `D:\32\STM32H7\h7_mac_oled_ui` 从已核实基线建立，未暂存原工作区文件。

| 文件 | 内容 |
|---|---|
| CM4/Application/oled_ui_draw.c/.h | 基本图元、裁剪、反色、位图和公共 API 契约 |
| CM4/Application/oled_ui.c | 复用原 ASCII 字库，新增有界字符和字符串接口，四行排版逻辑保留 |
| MDK-ARM/h7_mac.uvprojx | 仅 CM4 Target 登记新源文件 |
| Tests/OledMonitor/monitor_test.c、ui1_cases.inc、run_tests.py | 增加 8 个用例，保留原 61 个用例及其预期 |
| Docs/UI1/ | 验收记录、Keil 完整构建日志及仿真结果 |

## 接口与所有权

`DrawLine` 包含端点，沿主轴按最近像素取整，只遍历最多 128 个可见步进。反向端点生成同一像素集合。
`DrawRect` 和 `FillRect` 使用左上角与像素数，裁剪边不会新增边框。
`DrawRoundRect` 的半径最大为 `(min(width,height)-1)/2`，半径零等同矩形。圆角边框取圆角区域内与外部四邻接的像素。
`InvertArea` 对可见交集逐点翻转，使用原 SSD1306 页布局读取原位。
`DrawBitmap` 使用逐行对齐、MSB 在左的单色图，0 清除、1 点亮，写入前按 stride 及 height 检查有效长度。源数据应独立于 Framebuffer。
`DrawChar`、`DrawString` 使用原 5×7 字形及空白行列组成 6×8 字格。0x20–0x7E 可打印，其余显示问号。字符串接受实际可读字节数并在 NUL 处停止，跳过屏幕左侧内容后只画可见字格。
坐标为 int16_t，尺寸为 uint16_t。所有输出像素都在 x=0…127、y=0…31 内。区域扫描最多 4096 个像素。

仅允许在 CM4 应用主循环、设备 READY 且刷新 IDLE 时绘图。NULL 或短位图返回 INVALID_ARG，忙返回 BUSY，设备未 READY 返回 INVALID_STATE，缺少必要 Ops 返回 NOT_SUPPORTED。有效的零尺寸或完全屏外操作返回 OK。像素 Ops 返回失败时短路透传，单个 API 不提供事务回滚。

Device 仍拥有现有 512 字节 Framebuffer。新接口通过 Device draw_pixel 写入，反色只额外读取帧内目标位。App_OledRefresh 仍负责取 IPC 快照、备份、渲染、提交和失败回滚。提交后由 App_OledRun 轮询传输，刷新状态回到 IDLE 才可再次写入。未增加帧缓冲、动态分配、刷新 Timer 或 HAL 调用。Device、BSP、Framework、IPC 协议和 CM7 Target 均保持原有实现。

参考项目 [OLED_UI Software Driver](https://github.com/bdth-7777777/OLED_UI/blob/main/OLED_UI_Core/HAL/OLED_UI_Core/Driver/Software_Driver/OLED.c) 用于核对逐点线段、区域反色、位图分布和圆角限幅。实现沿用本工程的布局、设备接口和 GBK 中文注释风格。

## 可复核软件证据

运行 `python Tests/OledMonitor/run_tests.py`。环境需要 ARMCC 5.06u7 build 960、Unicorn 2.1.4、pyelftools 0.33。运行输出写入 `output/OledMonitor`。记录中的用例 ID 同 C 源码分支，实际失败时实际结果会包括断言行号或仿真错误。

| 用例 | 预期 | 实际（O0/O2） |
|---|---|---|
| 61 全屏边界 | 四角、四边及十字正确，比较全部 4096 点 | 通过 / 通过 |
| 62 交叉与极值线段 | 对角交叉、反向擦除、陡斜线、单点正确 | 通过 / 通过 |
| 63 矩形及异常尺寸 | 极值尺寸铺满后正确裁剪，零尺寸及屏外不写 | 通过 / 通过 |
| 64 跨三页反色 | 仅翻转区域内的位，二次反色恢复原画面 | 通过 / 通过 |
| 65 圆角 | 极大半径限幅，逐像素匹配 7×7 轮廓 | 通过 / 通过 |
| 66 Bitmap | 短长度及 NULL 拒绝，含像素的两页与预期相符 | 通过 / 通过 |
| 67 ASCII | 空格、波浪号、输入上下界、无 NUL 缓冲和边缘裁剪正确 | 通过 / 通过 |
| 68 所有权 | 所有公开绘图接口在 WAIT_CMD、WAIT_DATA 下返回 BUSY，原帧逐字节不变 | 通过 / 通过 |
| 46、50、51 异步刷新 | 全部页发送期间保持帧不变，更新等待旧帧完成 | 通过 / 通过 |
| 47、48、53 回滚 | 提交 BUSY/IO 或像素绘制失败后，原 512 字节全部恢复 | 通过 / 通过 |
| 0–42 IPC 显示 | ASCII、默认四行、失联、陈旧、错误与恢复符合原预期 | 通过 / 通过 |

69 个用例 × 两种优化，138 PASS、0 FAIL，执行 60480 条断言。仿真使用生产 Device 像素与渲染代码，BSP I²C 传输结果由测试注入。

## Keil 及资源

工程为 `MDK-ARM/h7_mac.uvprojx`，选择 `h7_mac_CM4`。基线与 UI1 均以 `UV4 -j0 -r ... -t h7_mac_CM4` 完整重建，0 Error、0 Warning。

| Keil Program Size | 基线 | UI1 | 增量 |
|---|---:|---:|---:|
| Code | 26302 | 26390 | +88 |
| RO-data | 1438 | 1438 | 0 |
| RW-data | 936 | 936 | 0 |
| ZI-data | 3576 | 3576 | 0 |
| Flash 逻辑大小（Code＋RO＋RW） | 28676 | 28764 | +88 |
| 静态 RAM（RW＋ZI） | 4512 | 4512 | 0 |

默认产品只运行原监控界面。Keil 链接器会移除未被默认界面调用的公开图元，以上是默认镜像增量。新增 API 在仿真 ELF 中实际链接并执行。未来页面调用新增图元后的镜像占用与最坏绘图时间尚未测量。

## 实板验收准备

CM4 Target 使用相对路径、ARMCC5、原 CM4 启动文件和 `h7_mac_CM4_custom.sct`。CM4 Flash 起始为 0x08100000。工程保留原 CMSIS-DAP 配置及 STM32H7 Flash 算法。使用当前工作树 Target 的 Download 和 Debug，或选取当前工作树生成的 HEX。仓库旧 `MDK-ARM/program_cm4.ini` 含主仓库绝对路径，直接执行会指向另一工作树，不能用于本次验收。

- **NOT_RUN** 烧录本工作树的 CM4 镜像并连接 CM4 调试器，确认可启动、可断点、可观察 `g_oled_monitor_debug`。
- **NOT_RUN** CM7 正常运行时核对四行内容与 IPC 快照，检查失联、恢复、0 FPS 和故障显示。
- **NOT_RUN** 在临时调试构建中，从 `OledUi_Render` 的 clear 成功路径调用图元测试代码，保留 `App_OledRefresh` 现有提交和回滚。确认边框、对角线、圆角、跨页反色、位图和边缘文本的显示。
- **NOT_RUN** 轮询观察 WAIT_CMD、WAIT_DATA 及回到 IDLE，核对发送期间 512 字节不变，验证 I²C 错误后能提交下一帧。
- **NOT_RUN** 运行中测量图形绘制耗时和刷新周期，检查闪烁及局部错位。

软件证据提交独立审核。UI1 最终验收待审。
