# 阶段05D：CM4 OLED实时监控验收

## 结论

软件验收通过，实板待验收。独立分支 `codex/stage05-oled-monitor`，不合并main。
开发基线 `de8a096`，包含05C-2 `6e7690e` 与后续纯注释整理；IPC V3布局和运行期行为保持不变。

## 实际修改

| 文件 | 修改原因 |
|---|---|
| `CM4/Application/oled_ui.c/.h` | 新增6×8 ASCII、四行排版、数字格式化、坐标裁剪与IPC状态界面 |
| `CM4/Application/app_oled.c/.h` | 使用完整IPC本地副本；沿用100ms Timer；IDLE绘图；512B失败回滚；公开本地监控诊断 |
| `CM4/Device/Src/oled_device.c` | 仅移除A5全亮测试命令及其宏，保留A4显存显示和四页异步状态机 |
| `MDK-ARM/h7_mac.uvprojx` | 仅给CM4 Application组加入oled_ui.c，Include Path沿用现有CM4/Application |
| `Tests/OledMonitor/` | 独立ARM专项、回归归档、正式编译日志、结果、界面预览与范围审计 |
| `Docs/Design/IPC_OLED_Monitor_Stage05D.md` | 调用链、状态含义、资源与上板步骤 |

未修改CM7生产代码、IPC协议/心跳、共享内存/MPU、HSEM、时钟、BSP_IIC、Camera、DCMI/DMA、Media、USB及MJPEG。
已有video_config、Keil本地配置、旧日志和未跟踪目录保留。工程文件只提交新增UI条目，用户其他XML差异不提交。

## 调用链

```text
Framework Initcall APPLICATION
  → App_Init → App_OledInit → 创建并启动唯一100ms Timer

CM4主循环
  → Framework_Run → Timer回调App_OledRefresh
      → 检查OLED_REFRESH_IDLE，否则跳过
      → IPC_Service_GetApplicationStatus（完整本地副本）
      → 备份旧512B Framebuffer
      → OledUi_Render → Device Ops clear/draw_pixel
      → Device Ops refresh → CMD0
  → App_Run → App_OledRun → Device Ops process
      → DATA0 → CMD1 → DATA1 → CMD2 → DATA2 → CMD3 → DATA3 → IDLE
```

CM4 IPC Timer仍独立运行，不受OLED Timer或IIC忙影响。应用不读取g_ipc，也不调用BSP/HAL。
刷新提交成功后，直到四页全部结束都不重绘；设备刷新非IDLE直接跳过，连IPC取样也不执行。
绘图失败或refresh提交失败且未借用缓冲区时恢复旧512B；BUSY只计跳过，不覆盖最近真实错误。
原驱动保证refresh提交失败不会进入发送状态。本轮没有新增IIC自动恢复；底层FAULT/无Abort完成回调等原有行为仍由BSP负责。

## 界面与数据语义

- 6×8字符格（5列字形+1列留白，底行留白），可打印ASCII 32～126，共95字形，4行，每行最多21字符。
- 正常显示Camera分辨率、Y8/RGB565/YUY2/MJPG、实测采集/USB milli-FPS的一位小数、DROP及Control状态。
- FPS取一位小数，不四舍五入；≥100 FPS显示`>99`。DROP完整支持uint32最大值；不是固定5位截断。
- STARTING显示`IPC INITIALIZING`；超时显示`M7 OFFLINE`；协议错误显示`IPC PROTOCOL ERROR`。
- valid=0显示INVALID/--；valid=1但fresh=0显示STALE；peer非ONLINE不显示旧FPS或旧丢帧数。
- Camera/USB state=0显示STOP，state=2显示HALT（停止中）。USB state=1但实测完成FPS为0显示WAIT，不能仅凭任务启动声称主机收流。
- FPS是最近统计窗口的真实完成速率，IPC来源新鲜不等于每一毫秒重新测量，更不等于目标FPS或Windows端实测。
- 当前V3没有主机Alt/连接状态字段，不额外推断Windows实时连接；USB WAIT也可能只是暂无完成帧。
- 系统故障由新鲜Control ERROR/失败、Camera失败、USB失败分级显示；历史last_fault不自动等同当前故障。
- 示例预览来自仿真输入，不是实板FPS或OLED照片。

## 实际软件测试

工具：ARM Compiler 5.06 update7 build960，Unicorn 2.1.4，真实C源码直接编译。没有复制修改后的驱动逻辑。
测试只模拟IIC收发完成、IPC视图读取与时间；字体、绘图、Device、Initcall、Timer、Event、Runtime和OLED四页驱动均使用工程源码。
测试无NVIC电气仿真，无法证明实物屏幕、IIC电气/时钟、NRST行为或视频性能。

| 组 | 实际执行 | PASS | FAIL |
|---|---:|---:|---:|
| IpcV2_run_tests | 512 | 512 | 0 |
| IpcRuntime_run_tests | 260 | 260 | 0 |
| IpcRuntime_run_task_tests | 20 | 20 | 0 |
| IpcStatus_run_tests | 448 | 448 | 0 |
| IpcStatus_run_kernel_tests | 41 | 41 | 0 |
| OledAsyncAcceptance_run_tests | 50 | 50 | 0 |
| OLED监控61场景×O0/O2 | 122 | 122 | 0 |
| 合计（执行次数，非唯一测试编号数） | 1453 | 1453 | 0 |

原IPC测试共1281次；原OLED异步50次；新专项122次。原测试源码与断言未修改，原报告保留，本次结果另存本目录。
专项共9076次断言。原始结果在results.json、regression_results.json和各组JSON；完整命令在commands.json及regression_results.json。
归档器首次处理中文文件名失败，已修复UTF-8路径解码后重新完整执行；没有降低任何测试断言。

实际入口（工程根目录）：

```powershell
python Tests/OledMonitor/run_tests.py
python Tests/OledMonitor/run_regressions.py
python Tests/OledMonitor/run_build.py
```

专项逐场景结果（每个场景O0、O2均执行；输入及期望对应monitor_test.c分支中的断言）：

| 编号 | 场景 | O0 | O2 | 两轮断言数 |
|---|---|---|---|---:|
| 0 | A字形与第六列留白 | PASS | PASS | 30 |
| 1 | 全部95个ASCII字形 | PASS | PASS | 1342 |
| 2 | 右下角坐标裁剪 | PASS | PASS | 16 |
| 3 | 非ASCII替换问号 | PASS | PASS | 24 |
| 4 | 空参数拒绝 | PASS | PASS | 16 |
| 5 | 刷新BUSY禁止绘图 | PASS | PASS | 1036 |
| 6 | 缺失绘图Ops | PASS | PASS | 12 |
| 7 | 清屏失败 | PASS | PASS | 14 |
| 8 | 像素失败 | PASS | PASS | 12 |
| 9 | 长画面变短无残留 | PASS | PASS | 528 |
| 10 | Y8格式 | PASS | PASS | 14 |
| 11 | RGB565格式 | PASS | PASS | 14 |
| 12 | YUY2格式 | PASS | PASS | 14 |
| 13 | JPEG格式 | PASS | PASS | 14 |
| 14 | 零FPS | PASS | PASS | 2 |
| 15 | 不足1FPS | PASS | PASS | 2 |
| 16 | FPS一位小数 | PASS | PASS | 2 |
| 17 | FPS大值 | PASS | PASS | 2 |
| 18 | 正常四行排版 | PASS | PASS | 20 |
| 19 | USB启动但无完成FPS | PASS | PASS | 14 |
| 20 | Camera与USB停止 | PASS | PASS | 14 |
| 21 | Camera与USB停止中 | PASS | PASS | 14 |
| 22 | IPC初始化 | PASS | PASS | 14 |
| 23 | CM7失联 | PASS | PASS | 18 |
| 24 | 启动超时 | PASS | PASS | 18 |
| 25 | 协议错误 | PASS | PASS | 18 |
| 26 | 未知通信状态 | PASS | PASS | 14 |
| 27 | 读取错误 | PASS | PASS | 14 |
| 28 | 短暂BUSY缓存 | PASS | PASS | 20 |
| 29 | 所有来源无效 | PASS | PASS | 14 |
| 30 | Camera无效 | PASS | PASS | 14 |
| 31 | USB无效 | PASS | PASS | 14 |
| 32 | Control无效 | PASS | PASS | 14 |
| 33 | Camera陈旧 | PASS | PASS | 16 |
| 34 | USB陈旧 | PASS | PASS | 14 |
| 35 | Control陈旧 | PASS | PASS | 14 |
| 36 | 全部陈旧 | PASS | PASS | 16 |
| 37 | Camera故障 | PASS | PASS | 14 |
| 38 | USB故障 | PASS | PASS | 14 |
| 39 | Control故障 | PASS | PASS | 14 |
| 40 | 真实IPC年龄499/500ms与Tick回绕 | PASS | PASS | 24 |
| 41 | 计数最大值 | PASS | PASS | 16 |
| 42 | INT32_MIN错误码 | PASS | PASS | 2 |
| 43 | 重复初始化不创建Timer | PASS | PASS | 20 |
| 44 | 初始化前轮询 | PASS | PASS | 2 |
| 45 | 99/100ms启动 | PASS | PASS | 22 |
| 46 | 异步发送期间不改Framebuffer | PASS | PASS | 40 |
| 47 | 提交BUSY恢复原Framebuffer | PASS | PASS | 1046 |
| 48 | 提交IO错误与重试 | PASS | PASS | 1046 |
| 49 | 完成IO错误与恢复 | PASS | PASS | 30 |
| 50 | 四页命令数据顺序和字节一致 | PASS | PASS | 1130 |
| 51 | 画面变化等旧帧完成后更新 | PASS | PASS | 1138 |
| 52 | IPC接口失败不发送 | PASS | PASS | 18 |
| 53 | 绘图失败恢复原Framebuffer | PASS | PASS | 1042 |
| 54 | Timer对象池满 | PASS | PASS | 36 |
| 55 | 缺失设备 | PASS | PASS | 2 |
| 56 | 设备未READY | PASS | PASS | 4 |
| 57 | 缺失Ops不建Timer | PASS | PASS | 4 |
| 58 | 底部裁剪 | PASS | PASS | 4 |
| 59 | 极大坐标裁剪 | PASS | PASS | 4 |
| 60 | A4保留且A5已删除 | PASS | PASS | 16 |

## 正式构建与资源

使用当前实际MDK-ARM/h7_mac.uvprojx，UV4 -j0 -r全量重编；没有生成新工程或修改用户视频配置。

| Target | Error | Warning | 结果 |
|---|---:|---:|---|
| h7_mac_CM7 | 0 | 0 | PASS |
| h7_mac_CM4 | 0 | 0 | PASS |

CM4：Code=26302，RO-data=1438，RW-data=936，ZI-data=3576 B。
相对05C-2同Target日志：Code +2232 B，RO +476 B，RW不变，ZI +560 B。
CM4 Map：字库475B Flash；s_frame_backup 512B，地址0x10000284；g_oled_monitor_debug 48B，地址0x10000484。
没有动态内存、额外RTOS任务或第二个OLED Timer；行缓冲88B在栈上；CM4启动文件Stack_Size仍为0x400（1024B），本轮没有扩大栈，完整调用链及中断嵌套余量需实板观察。
共享对象仍为0x38000000/320B，UNINIT窗口仍为4KiB。
源码及工程在重编前后哈希一致，详见build_results.json。地址仅适用于本次构建，下次链接可能变化。

## Keil Watch与实板验收

按Docs/Design/IPC_OLED_Monitor_Stage05D.md执行配套V3烧录与完整断电冷启动。
CM4 Watch：`g_oled_monitor_debug`、`g_ipc_application_debug`、`g_ipc_runtime_debug`；需要查看设备时用`'app_oled.c'::s_app_oled`（Keil模块限定语法依调试器支持，不能求值时在App_OledRun处查看局部作用域）。
CM7 Watch：`g_app_status_debug`、`g_ipc_task_debug`、`g_ipc_runtime_debug`；CM7用错ELF/核时不要拿同名CM4诊断作比较。

未完成的实板项目：实际OLED字形/方向、连续刷新无撕裂、停止/恢复/失联界面、MJPEG+OLED≥5分钟、实际FPS及DROP增量、IPC最大调度间隔和IPC任务栈余量。
软件PASS不代表上述硬件项目通过。保留原有NRST/LINK供电及单核热复位限制，不在05D中更改。
