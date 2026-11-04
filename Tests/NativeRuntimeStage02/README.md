# Native Runtime 阶段02验收

基线：`4d463ba`；分支：`codex/cm7-native-runtime-stage02`。正式源码已完成Camera→Media→跨任务租借→USB UVC接入。**本轮只有软件验证，未验证实板视频或实际25 FPS。**

## 执行环境与命令

- Windows、Python 3.11。
- ARM Compiler 5.06 update 7 build 960，`D:/32/ARM/ARMCC/Bin`。
- Keil `D:/32/UV4/UV4.exe`。
- Unicorn/pyelftools使用工程已有`Tests/OledAsyncAcceptance/python_deps`。

在工程根目录运行：

```powershell
python Tests/NativeRuntimeStage02/run_all.py
python Tests/NativeRuntimeStage02/run_regression.py
python Tests/NativeRuntimeStage02/run_build.py
python Tests/NativeRuntimeStage02/audit.py
python Tests/NativeRuntimeStage02/compare_baseline.py
```

`run_all.py`按专项顺序执行，失败立即停止，逐例名称和实际结果保存在各`*_results.json`，编译器调用保存在`*_commands.json`。不修改旧测试。

`run_regression.py`执行原有测试，复制本次结果到当前目录，并恢复原有版本化测试报告。若某旧夹具无法编译，记录未执行数，不能使用旧JSON假装执行成功。

`run_build.py`从HEAD工程配置生成同目录临时工程，补入本阶段消息文件，避免覆盖本地IDE设置。完整构建TEST、UVC、CM4；TEST仅增加`APP_VIDEO_TEST_MODE=1`。实际命令、返回码和Error/Warning见`build_results.json`及三份`*_rebuild.log`。AXF/Map快照保存在忽略的`tmp/native_runtime_stage02/`，不提交构建产物。

`audit.py`需要先构建，以实际AXF核对配置、任务栈、队列、向量和Map对应符号。它是阶段02审计，不是任意后续版本都应直接通过的通用CI检查。`compare_baseline.py`检查本轮保存的4d463ba历史对照证据，不重新执行基线。原始基线重复日志保留本地忽略目录`baseline/`，仓库保留摘要和逐项失败凭据，避免重复归档。

## 新增专项

|专项|数量|结果|
|---|---:|---|
|02A Media事件与统计|10|10 PASS|
|BSP异常通知边界|7|7 PASS|
|02B真实Core/UVC事件通路|11|11 PASS|
|02C纯C租借协议及队列满/旧凭据|28|28 PASS|
|02D真实内核、Device、Media、USB任务集成|33|33 PASS|
|TEST独立控制面|8|8 PASS|
|合计|97|97 PASS、0 FAIL|

另外，源码/依赖/链接审计38/38 PASS。三种正式完整重编均0 Error、0 Warning。

02C第27项运行1000个租借周期。02D覆盖真实Device ERROR、BOOT、静态任务/五个队列失败注入、采集故障DRAINING、在途停止、Abort失败、Reset/Disconnect/Alt0、重复旧凭据、队列满、SOF合并和测量计数。

02D第32项使用33ms模拟采集源，模拟1000ms得到31次采集完成、15次EOF、15次丢帧，EOF速率变量为15000，即15 FPS。这证明测量没有直接采用配置值25；**不是实板FPS**。同一真实EOF只在原Payload末包确认后累加。

## 历史回归

当前实际执行1044项：658 PASS、386 FAIL；未执行145项。独立4d463ba快照执行1178项：792 PASS、386 FAIL；原Legacy 11项未执行。

可执行旧用例的失败集合完全一致，详情见`baseline_comparison.json`。386项包括228项旧UVC配置夹具、2项旧512B上限、3项Initcall自动启动断言、45项旧DCMI FIFO断言、108项同类BSP/Media FIFO断言。

本阶段新增夹具不兼容：VideoConfig28、旧Bridge35、Native01 56、Native01Usb15，共134项；均未执行，不计PASS。旧单线程桥接入口已移除，旧夹具还直接操作其私有变量，或未链接本阶段依赖。

原断言和测试源码未改，`historical_assertion_audit.json`记录检查结果。**没有宣称完整历史回归通过。**

## 模拟边界

- 02A/02C为真实生产函数配合控制桩；不依赖STM32外设。
- 02B使用真实BSP/Platform/Core/UVC/Payload，HAL硬件行为受控。
- 02D/TEST执行真实FreeRTOS内核、静态队列、Device Model、Media和USB软件路径。只有OV5640物理行为、DMA数据源及硬件寄存器行为使用桩。
- 首个SVC/Port调度启动被显式跳过，并补入相同内核初始化状态；测试手工挂起/恢复并选择任务。ISR身份和优先级条件可控，没有模拟真正PSP/PendSV异常切栈或NVIC时间竞争。
- 没有在正式固件产生模拟帧；测试源没有加入Keil生产Group。

## 实板待执行

1. 使用UVC配置完整重编并下载CM7，核对CM4固件及双核启动；不使用TEST构建的AXF取流。
2. 冷启动观察`init_error`及三个任务`boot_seen`，Control应经过Camera ACK再启动USB。观察任务栈高水位和`g_rtos_debug.idle_iterations`。
3. Windows先列设备和支持格式，再用实际设备名称取流：

```powershell
ffmpeg -list_devices true -f dshow -i dummy
ffmpeg -list_options true -f dshow -i video="实际设备名称"
ffplay -f dshow -video_size 160x120 -framerate 25 -pixel_format yuyv422 -i video="实际设备名称"
```

4. 观察30秒以上的采集/丢帧、EOF累计、`measured_fps_milli`、SOF间隔和桥接取得/归还计数；同时用主机帧时间戳检查Windows实际收帧率。不得仅记录配置25；正式测量期间不要用长时间断点暂停CPU。
5. 关闭取流Alt0、拔插USB、Reset；确认没有旧Return释放新帧。运行期取得数减归还数应为0或1，停止完成后应相等。
6. 从Application业务任务上下文调用`ControlTask_RequestStop()`；返回SYS_OK仅表示请求入队，需等Control IDLE和任务停止ACK。完全停止后调用`ControlTask_RequestStart()`验证新epoch。
7. 检查Abort/硬件停止失败时保持LOANED或等待归还，Control可ERROR/超时但不能虚假IDLE；故障解除后显式再请求Stop。

本轮实板枚举、真实图像、Windows显示、实际FPS、真实SOF抖动、SDRAM压力、长期稳定性和恢复均标记**未验证**。审计和修改清单见`Docs/Architecture/CM7_FreeRTOS_Native_Runtime_阶段02实施与验收.md`。
