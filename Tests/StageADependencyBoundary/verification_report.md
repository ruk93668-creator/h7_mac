# Stage A 依赖边界整理验收报告

## 基线与范围

- 基线：48afa27a1ac941c541ac400a3a820474b25e65c3。
- 分支：codex/stage-A-dependency-boundary；未合并main。
- 初始工作区干净；构建前配置和正式输出归档在 C:\Users\29062\AppData\Local\Temp\h7_stageA_48afa27\formal_outputs_before.zip。
- 不改BOOT、任务数量/顺序、消息结构、状态机、视频参数、帧租约、UVC协议、DMA/IDMA。

## 生产修改清单

| 文件 | 原因 |
|---|---|
| CM7/Application/app_events.c | 去除BSP绑定，统一接收Media；CPU上下文检查改用Platform |
| CM7/Media/Src/camera_capture.c | 在原初始化时机代为绑定BSP，显式映射进展/错误位并保持回调上下文 |
| CM7/Platform/platform_sync.c/.h | 原样封装IPSR/PRIMASK/BASEPRI、屏蔽/恢复、DMB和IRQ优先级读取 |
| CM7/Application/app_storage.c | 原同步表达式逐项替换；移除SDRAM和AVI索引布局断言 |
| CM7/Application/app_messages.c、app_tasks.c | 仅替换CPU上下文读取接口及头文件 |
| CM7/Application/Tasks/*_task.c（五个） | 仅替换创建任务时的异常号检查及头文件 |
| CM7/Service/mjpeg_avi.c | AVI索引实际资源所有者保留原范围断言，表达式不变 |
| MDK-ARM/h7_mac.uvprojx | 仅CM7有效Platform组增加platform_sync.c，输出配置不变 |

## 行为等价证据

1. App_EventsInit原来先登记Media回调、再绑定BSP；现在由Media完成这两步，仍发生在BSP Init前。BSP失败时仍保留原先已经登记的Media接收端，不增加失败重试语义。
2. BSP进展位为bit1、错误位为bit0；Media定义相反，因此显式映射，不能直接转发数值。原YUY2帧完成通知保留，JPEG Snapshot/Continuous的BSP唤醒通过Media完整转发。
3. Platform不增加Mutex、BASEPRI修改、等待或额外DMB；Get/Disable/Restore顺序、原始PRIMASK值和原DMB位置不变。调用封装存在少量函数调用开销，不承诺周期级时序相同。
4. 八个Application文件逆替换后与基线业务代码完全一致；App_Events移除原BSP绑定并逆替换后也一致。
5. 原AVI索引范围断言原样迁移；重叠和越界配置仍编译拒绝。Application不再直接包含BSP或芯片头，也不直接使用这些CPU指令。

## 本次测试

| 验证 | 本次结果 |
|---|---|
| 新增专项 | 55/55；其中44次ARM执行、2项负向编译、9项源码等价检查 |
| 帧租借/Storage/AVI生产功能回归 | 92/92 PASS |
| FatFS生产功能回归 | 120/120 PASS |
| 实际BSP/HAL IDMA专项 | 88/88 PASS |
| IPC V2 | 512/512 PASS |
| IPC Status | 448/448 PASS |
| OLED Monitor | 122/122 PASS |
| OLED Async | 50/50 PASS |
| EP0 IRQ Race | 4/4 PASS |
| USB字符串 | 8/8 PASS |

上述有效回归为1444次软件执行全部通过；新增55项包含不同验证方式，不能全部称为运行期用例。未进行实卡写入。

## 原24组历史脚本对照

基线和本次退出状态及已执行用例结果集合相同。历史失败不修复、不删除、不降低断言。旧夹具因Application不再透传BSP类型等原因，错误信息可能变化；基线本身已不能完整编译的套件不视为阶段A有效回归。逐例保留在historical_comparison.json。

| 套件 | 基线 → 本次 |
|---|---|
| IpcV2/run_tests.py | {'total': 512, 'passed': 512, 'failed': 0}；结果集合一致 |
| IpcRuntime/run_tests.py | {'total': 260, 'passed': 212, 'failed': 48}；结果集合一致 |
| IpcRuntime/run_task_tests.py | 夹具/执行错误（两次均未形成完整有效结果）；结果集合一致 |
| IpcStatus/run_tests.py | {'total': 448, 'passed': 448, 'failed': 0}；结果集合一致 |
| IpcStatus/run_kernel_tests.py | 夹具/执行错误（两次均未形成完整有效结果）；结果集合一致 |
| OledMonitor/run_tests.py | {'total': 122, 'passed': 122, 'failed': 0, 'assertions': 9076}；结果集合一致 |
| OledAsyncAcceptance/run_tests.py | {'total': 50, 'pass': 50, 'fail': 0, 'not_run': 0, 'assertions': 11156, 'compiler': 'Product: MDK Plus 5.32\r\nComponent: ARM Compiler 5.06 update 7 (build 960)\r\nTool: armcc [4d365d]\r\nFor support see http://www.arm.com/support\r\nSoftware supplied by: ARM Limited\r\n', 'unicorn': '2.1.4'}；结果集合一致 |
| UvcPayload/run_tests.py | {'unique_cases': 62, 'executions': 124, 'passed': 122, 'failed': 2}；结果集合一致 |
| UsbCoreDecoupling/run_tests.py | {'total': 204, 'passed': 201, 'failed': 3, 'assertions': 2947}；结果集合一致 |
| Ep0IrqRace/run_race.py | {'total': 4}；结果集合一致 |
| UvcLifecycle/run_tests.py | {'total': 40, 'passed': 34, 'failed': 6}；结果集合一致 |
| UvcEp1Lifecycle/run_tests.py | {'total': 45, 'passed': 0, 'failed': 45}；结果集合一致 |
| UvcProbeCommit/run_tests.py | {'total': 89, 'passed': 0, 'failed': 89}；结果集合一致 |
| UvcProbeCommit/run_cancellation_tests.py | {'total': 39, 'passed': 0, 'failed': 39}；结果集合一致 |
| UvcStreaming/run_tests.py | {'total': 36, 'passed': 1, 'failed': 35}；结果集合一致 |
| UsbUvcDescriptors/run_tests.py | 夹具/执行错误（两次均未形成完整有效结果）；结果集合一致 |
| UsbUvcDescriptors/run_string_tests.py | {'total': 8, 'passed': 8, 'failed': 0}；结果集合一致 |
| UvcUsbCleanup/run_tests.py | {'total': 14, 'passed': 0, 'failed': 14}；结果集合一致 |
| VideoConfig/run_tests.py | 夹具/执行错误（两次均未形成完整有效结果）；结果集合一致 |
| CameraStreamState/run_tests.py | 夹具/执行错误（两次均未形成完整有效结果）；结果集合一致 |
| BspMediaContinuous/run_tests.py | 夹具/执行错误（两次均未形成完整有效结果）；结果集合一致 |
| DcmiFramePair/run_tests.py | {'total': 82, 'passed': 37, 'failed': 45, 'assertions': 5083}；结果集合一致 |
| NativeRuntimeStage01/run_tests.py | 夹具/执行错误（两次均未形成完整有效结果）；结果集合一致 |
| Ov5640Cleanup/run_tests.py | 夹具/执行错误（两次均未形成完整有效结果）；结果集合一致 |

历史已执行失败包括IPC Runtime 48、Payload 2、Core 3、Lifecycle 6、EP1 45、Probe 89、取消39、Streaming 35、Cleanup 14、DCMI配对45，共326项；另外8组脚本存在夹具/执行错误，单列不计作未执行PASS。完整原脚本包含当前配置已不兼容的预期，不宣称全仓全绿。

## 正式构建

- ARMCC5 5.06 update7。
- CM7完整重编：0 Error、0 Warning。
- CM4完整重编：0 Error、0 Warning。
- 构建前后生产源码及工程指纹不变；见build_results.json和两核日志。
- CM4 Target配置一致；输出目录、文件名、HEX、MAP/Listing和Scatter配置均保持原值；见scope_audit.json。
- 正式AXF/MAP沿用MDK-ARM/h7_mac_CM7及h7_mac_CM4原输出位置。基线CM7的CreateHexFile=0，因此没有生成CM7 HEX；CM4 HEX正常生成。未改变该设置，未增加测试固件或Keil验收Target。
- 入库构建日志仅移除行尾空格；原始日志保存在临时目录。

## 未验证与已知限制

- 未烧录或独立验证实板视频、SD录像、IPC/OLED并行及停止恢复。
- 新增平台调用及Media转发有少量调用开销，实际ISR耗时和25FPS表现待板测。
- FE根因、SD录像25FPS、全局STOP缺口均未在阶段A修复。
- 不把模拟屏蔽/通知、软件IDMA测试或已有历史实板结果作为本次实板通过。
- 不实施阶段B/C，不新增自主STOP、消费者协商或SceneManager。
