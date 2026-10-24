# CameraCapture上下文统一重构验收

基线：GitHub/main与本地HEAD均为`85037d3432b1f332736f2f7581c9c0b19066edd6`，开始时工作区干净。本轮未提交、未推送。

## 结构变化

```text
CameraCapture_t / s_capture
├── config、mode、state、last_result、initialized
├── snapshot: CameraSnapshot_t
│   └── framebuffer、frame_capacity、frame_bytes、valid_bytes、start_tick、timeout_ms
└── stream: CameraStream_t
    └── buffers[3]、dma_m0_index、dma_m1_index、consumer_index、next_sequence、dropped_frames、frame_bytes
```

- 完整定义CameraSnapshot_t及CameraStream_t后，再定义CameraCapture_t。
- 移除独立s_camera_stream；启动和回调使用`&s_capture.stream`。
- Snapshot成员统一使用`s_capture.snapshot.*`。
- 保留CameraCapture_StartStream、CameraStream_Rotate及BSP回调适配。

## 必要的一致性修复

|路径|最终mode / state|说明|
|---|---|---|
|Init成功|NONE / IDLE|明确初始化模式|
|Snapshot启动|SNAPSHOT / CAPTURING|禁止与Stream同时占用|
|Snapshot完成|SNAPSHOT / READY|公开GetFrame保留Snapshot行为|
|Snapshot释放|NONE / IDLE|清除旧模式，允许后续启动|
|Stream启动前|STREAM / CAPTURING|调用BSP前提交Media占用状态|
|Stream启动失败|STREAM / ERROR|保留模式，要求Stop恢复；拒绝再次启动|
|Stream内部初始化失败|NONE / IDLE|尚未调用BSP，不占用模式|
|Stop失败|原模式 / ERROR|保留首次错误及恢复门控|
|Stop成功|NONE / IDLE|恢复后允许后续启动|

- Process只对Snapshot接受单帧FRAME完成及100ms超时；Stream仍处理BSP错误事件并经原Stop路径恢复，避免误用Snapshot计时。
- 不改BSP、Device、Application、公开头文件及Scatter；不新增公开接口。
- CameraStream_Init、FindReplaceable、Rotate、GetFrame、ReleaseFrame五个函数与基线逐字比较一致，轮换算法及临界区未改变。
- 未改变BSP硬件操作顺序，未自动开始连续采集。

## 测试文件调整

- `Tests/CameraStreamState/stream_test.c`：适配当前BspDcmiTarget_t、CameraStream_Rotate和两参数地址替换接口；补齐BSP桩。
- `Tests/CameraStreamState/run_tests.py`：改为比较本次执行前后源文件哈希，保留历史source_baseline.json不覆写，避免把授权重构误判为测试改源码。
- `Tests/CameraFormats/Camera/camera_test.c`：更新Snapshot路径、补充Stream接口桩；新增stm32h7xx.h测试桩入口。
- `Tests/CameraFormats/Camera/run_tests.py`：历史Y8用例显式选择Y8，三格式用例按用例选择开机配置；只修改模拟器加载镜像，不改正式Application默认YUV422配置。
- `Tests/CameraCleanup/Camera/camera_test.c`、`Tests/CameraFunctionLayout/Camera/camera_test.c`：仅更新Snapshot成员路径；本轮未运行这两个历史阶段套件，不计入通过数量。
- `Tests/CameraContextRefactor/`：新增当前正式Media的24组×3格式生命周期测试、运行器、命令、结果、构建日志与源码审计。
- Tests中的before/diagnostics历史快照保留，不当作当前正式源码。

## 实际执行命令及结果

ARMCC `5.06 update 7 (build 960)`，Unicorn执行ARM机器码。构建命令详见各测试目录commands.json。

|命令|PASS / 总数|断言|验证对象|
|---|---:|---:|---|
|`python Tests/CameraStreamState/run_tests.py`|75 / 75|934605|当前正式三缓冲算法、所有权、PRIMASK、随机模型|
|`python Tests/CameraSingleFrame/Camera/run_tests.py`|82 / 82|1218|历史诊断Media/Application副本及当前部分底层；不是当前Media重构通过证明|
|`python Tests/CameraFormats/Camera/run_tests.py`|78 / 78|1384|当前正式BSP/Device/Media/Application三格式及Snapshot回归|
|`python Tests/DcmiReplaceTarget/run_tests.py`|119 / 119|7392|当前正式DCMI BSP及HAL，不受对象结构修改影响|
|`python Tests/CameraContextRefactor/run_tests.py`|72 / 72|1095|当前正式Media的嵌套上下文及生命周期|

总计426项通过，其中344项直接覆盖当前源码相关路径，82项为历史诊断回归，不能混同。

新增72项验证Snapshot释放后模式切换、Stop后模式切换、Stream启动失败、Stop失败及重试、首次错误保留、Stream不使用Snapshot超时、错误事件恢复、嵌套回调上下文、Snapshot Get/Release、回绕计时及PRIMASK保持。

## 正式工程编译

```powershell
D:\32\UV4\UV4.exe -b D:\32\STM32H7\h7_mac\MDK-ARM\h7_mac.uvprojx -t h7_mac_CM7 -o D:\32\STM32H7\h7_mac\Tests\CameraContextRefactor\cm7_keil.log
D:\32\UV4\UV4.exe -b D:\32\STM32H7\h7_mac\MDK-ARM\h7_mac.uvprojx -t h7_mac_CM4 -o D:\32\STM32H7\h7_mac\Tests\CameraContextRefactor\cm4_keil.log
```

- CM7增量构建：0 Error、2 Warning。CameraStream_GetFrame、CameraStream_ReleaseFrame尚未引用。
- CM4增量构建：0 Error、0 Warning。
- 未执行实板操作，模拟不验证物理DVP时序、DMA切换窗口及真实图像。

## 旧引用审计和遗留风险

- 正式CM7/CM4/Common中不再存在s_camera_stream及Snapshot旧平铺成员引用。
- 公开接口未改变；内部类型布局变化，Keil Watch需将`s_capture.frame_bytes`等改为`s_capture.snapshot.frame_bytes`，流字段使用`s_capture.stream`。
- Stream的公开取帧/释放路径尚未接入，保留原有状态，没有为消除Warning而扩大开发范围。
- 模拟生命周期套件以可控BSP返回值验证Media行为。Snapshot完成后BSP硬件可能仍处使能状态，真实Snapshot→Stream切换仍可能需要显式Stop；本轮未在Release内新增硬件操作。若BSP拒绝启动，Media保留STREAM/ERROR并经Stop恢复。
- Stop后保留stream字段作为原有上下文，下一次StartStream通过CameraStream_Init重新初始化；不在本轮修改缓冲区轮换/消费者策略。
- BSP既有32 Word切换门限与时序风险保持原状；本轮不是连续采集端到端实板验收。
