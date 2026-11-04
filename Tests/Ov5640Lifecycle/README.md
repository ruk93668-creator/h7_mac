# OV5640生命周期调整与软件验收

日期：2026-11-04。开发基线：main `60df1b3`。仅调整CM7 OV5640生命周期。

## 最终调用顺序

启动：`App_CameraInit/InitStream → Prepare（configure保持待机、Media_Init）→ Device_Start → CameraCapture_Start/StartStream`。

停止：`App_CameraStop → CameraCapture_Stop → CameraCapture_IsStopped → Device_Stop`。

- 正常Stream有IN_USE时，沿用Media的BUSY约定，调用者先安排消费者安全归还。
- 故障时Media可先停DCMI/DMA并进入DRAINING；即使Stop返回SYS_OK，只要IsStopped为false，Application返回BUSY，不能停传感器。
- USB仍借用图像时，由原消费者/桥接按原所有权协议归还；本轮不强制释放，不修改USB。
- Media停止失败，传感器继续保持原Device状态，不伪装已经停止。
- 本轮刚启动传感器而Media启动失败时，先通过同一停止入口恢复；恢复失败结果优先返回。
- App_CameraRun在Media已完整停止后，通过同一入口停止仍为RUNNING的传感器。

## Device状态所有权

`configure()`不写Device_t.state。Device_Start/Stop由现有Device框架执行BUSY及RUNNING/READY/ERROR提交；配置完整性使用私有标志，不是第二套Device生命周期。

官方ST初始化表含有0x3008=0x02的唤醒写入。为避免configure短暂启动输出，总线适配仅在配置期间将该写入转换为0x42软件待机；其余配置、像素格式、PCLK和行帧周期保持原值，末尾确认待机。Device_Start才实际写0x02。未修改官方组件源码。该变化需要实板重新验证待机写表、起流及停流。

## 修改文件

| 文件 | 原因 |
|---|---|
| CM7/Device/Src/ov5640_device.c | 绑定Start/Stop回调；移除configure状态赋值与启动；配置阶段保持待机 |
| CM7/Device/Inc/ov5640_device.h | 明确configure只配置并保持待机 |
| CM7/Application/app_camera.c/.h | 通过Device生命周期启停；新增安全停止入口和启动失败清理 |
| CM7/Media/Src/camera_capture.c、Inc/camera_capture.h | 仅新增IsStopped只读查询；区分NONE/IDLE与DRAINING，不改采集算法 |
| Tests/CameraAppInit/app_test.c | 补充新生命周期及只读查询的桩，保留原13项断言 |
| Tests/Ov5640Lifecycle/ | 新增真实模块集成测试、编译记录、范围审计及本报告 |

## 实际执行

```powershell
python Tests/Ov5640Lifecycle/run_tests.py
python Tests/CameraAppInit/run_tests.py
python Tests/CameraContextRefactor/run_tests.py
python Tests/CameraStreamState/run_tests.py
python Tests/DcmiReplaceTarget/run_tests.py
python Tests/UvcBridge/run_tests.py
python Tests/VideoConfig/run_tests.py
D:/32/UV4/UV4.exe -r MDK-ARM/h7_mac.uvprojx -t h7_mac_CM7 -o Tests/Ov5640Lifecycle/h7_mac_CM7_rebuild.log
D:/32/UV4/UV4.exe -r MDK-ARM/h7_mac.uvprojx -t h7_mac_CM4 -o Tests/Ov5640Lifecycle/h7_mac_CM4_rebuild.log
```

编译器：ARMCC5 V5.06 update 7 build 960。隔离程序实际运行于Unicorn Cortex-M模拟器，直接编译正式Device、OV5640 Device、官方组件、Media和Application；只模拟SCCB寄存器、时间、PRIMASK和DCMI/DMA硬件。不是复制修改后的驱动逻辑。

| 套件 | PASS / 总数 | FAIL |
|---|---:|---:|
| 新增OV5640Lifecycle | 32/32 | 0 |
| CameraAppInit | 13/13 | 0 |
| CameraContextRefactor | 72/72 | 0 |
| CameraStreamState | 75/75 | 0 |
| UvcBridge | 35/35 | 0 |
| VideoConfig | 28/28 | 0 |
| DcmiReplaceTarget（历史问题） | 88/133 | 45 |

本轮相关验证255/255通过；连同历史DCMI套件共执行388项，343 PASS、45 FAIL，不能宣称全量回归全绿。

### 历史DCMI失败的基线对照

在tmp/ov5640_lifecycle_baseline使用git archive提取未修改main HEAD的BSP、HAL、头文件与原测试；仅将Python依赖搜索路径指向现有本机Unicorn库，不改断言。基线同样88/133通过、45失败，失败编号与行号逐项完全相同，见DcmiReplaceTarget_baseline_comparison.json。首个失败为dcmi_test.c:147仍断言DMA_FIFOMODE_DISABLE，正式基线已启用FIFO。该历史断言不属于本轮生命周期调整，未修改DCMI、测试断言或结果计数。

## 生命周期专项逐项结果

| ID | 场景 | 实际结果 |
|---:|---|---|
| 0 | Y8配置保持待机 | PASS |
| 1 | RGB565配置保持待机 | PASS |
| 2 | YUV422配置保持待机 | PASS |
| 3 | Device正常启停 | PASS |
| 4 | 未配置拒绝启动 | PASS |
| 5 | RUNNING拒绝重配置 | PASS |
| 6 | 非法配置及BUSY拒绝 | PASS |
| 7 | 错误ID拒绝配置 | PASS |
| 8 | 配置IO失败后重试 | PASS |
| 9 | 启动IO错误由框架进入ERROR | PASS |
| 10 | 停止IO错误不虚假READY | PASS |
| 11 | Stream启停顺序 | PASS |
| 12 | Snapshot启停顺序 | PASS |
| 13 | 重复Stream不重复启动传感器 | PASS |
| 14 | 交叉启动不停止现有采集 | PASS |
| 15 | Snapshot持有帧阻止停止 | PASS |
| 16 | Stream IN_USE阻止停止 | PASS |
| 17 | 故障DRAINING等待归还 | PASS |
| 18 | DCMI停止失败后重试 | PASS |
| 19 | Media启动失败安全回滚 | PASS |
| 20 | 回滚停止失败优先报告 | PASS |
| 21 | Media故障自动安全停止传感器 | PASS |
| 22 | DRAINING释放后自动停止传感器 | PASS |
| 23 | 配置总线初始化失败不启动 | PASS |
| 24 | 传感器启动失败不启动DMA | PASS |
| 25 | 传感器停止失败保留ERROR | PASS |
| 26 | 未初始化Stop拒绝 | PASS |
| 27 | ID读取IO错误 | PASS |
| 28 | 待机写入失败不能标记配置成功 | PASS |
| 29 | 停止后重启不重复配置 | PASS |
| 30 | Snapshot释放后自动停止传感器 | PASS |
| 31 | Start及Stop回调重入返回BUSY | PASS |

所有专项用例额外检查：未完整停止Media时传感器停止次数为0；传感器未启动时DCMI启动次数为0。

## 编译与范围

- CM7完整重编：0 Error、0 Warning。Code=56170、RO=2650、RW=780、ZI=30220字节。
- CM4完整重编：0 Error、0 Warning。Code=15722、RO=914、RW=636、ZI=2580字节。
- 使用当前工作区已有Keil优化设置构建，没有提交用户的Keil配置变更。
- Device通用框架、官方OV5640组件、BSP Camera/DCMI及桥接源码SHA保持不变，见scope_audit.json。CM4、HAL/LL与USB模块未作本轮源码修改。

## 边界与待实板验证

- 配置、Start、Stop仍要求由CM7正常业务任务串行调用；不在ISR中执行软件SCCB，不新增跨任务锁。
- 继承现有Device框架语义：Start/Stop回调失败进入ERROR；本轮未新增ERROR状态自动恢复或强制回到READY。
- 配置失败保留Device原状态，但私有配置有效标志为false，禁止启动半配置传感器，允许在READY下重新configure。
- 软件已验证调用顺序和所有权约束；未实际测量DVP/PCLK停流、待机配置效果、首次完整帧或Windows视频恢复。
- 暂未解决上述45项历史DCMI测试失败。
