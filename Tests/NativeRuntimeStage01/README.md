# CM7 Native Runtime 阶段01软件验收

基线：`8d3741a74692ec7f16a78884d86ecfcf222260d3`。开发分支：`codex/cm7-native-runtime-stage01`。

本次是 TEST 控制面和任务启动基础，不是 Native 相机视频链路验收。没有执行实板验证。

## 执行方式

在工程根目录执行，ARM Compiler 5.06 update 7 build 960；Python 3.11、Unicorn、pyelftools 使用现有测试依赖目录。

```powershell
python Tests/NativeRuntimeStage01/run_tests.py
python Tests/NativeRuntimeStage01/run_usb_control.py
python Tests/NativeRuntimeStage01/audit.py
```

编译与链接的逐条命令、退出码在 `commands.json`、`usb_commands.json`。模拟测试不改生产文件。`audit.py` 需要先完成正式 CM7/CM4 重编，并读取 CM7 AXF 检查实际向量及链接符号。

正式构建使用当前工程文件的任务改动，加在干净基线工程配置上，临时文件放在原 MDK-ARM 目录以保持相对路径；保留用户工作区已有的优化级别和调试设置，不把这些设置加入本次提交：

```powershell
Start-Process D:/32/UV4/UV4.exe -WindowStyle Hidden -Wait -ArgumentList '-r','MDK-ARM/h7_mac_native_stage01_check.uvprojx','-t','h7_mac_CM7','-o','Tests/NativeRuntimeStage01/h7_mac_CM7_rebuild.log'
Start-Process D:/32/UV4/UV4.exe -WindowStyle Hidden -Wait -ArgumentList '-r','MDK-ARM/h7_mac_native_stage01_check.uvprojx','-t','h7_mac_CM4','-o','Tests/NativeRuntimeStage01/h7_mac_CM4_rebuild.log'
```

临时工程文件在验证后移除；同一已提交配置可直接用正式 `h7_mac.uvprojx` 重编。构建日志是本次记录，不代表任意后续版本。

## 结果

|测试组|执行|PASS|FAIL|未执行|
|---|---:|---:|---:|---:|
|Native任务、事件、启动失败注入|56|56|0|0|
|真实USB Core与任务适配集成|15|15|0|0|
|原USB Core|204|201|3|0|
|原EP0 IRQ竞态|4|4|0|0|
|原VideoConfig|28|28|0|0|
|原CameraAppInit|13|13|0|0|
|原CameraContextRefactor|72|72|0|0|
|原CameraStreamState|75|75|0|0|
|原FreeRtosP0 Legacy专项|0|0|0|11|
|合计（不含静态审计）|467|464|3|11|

源码/链接审计另计：15/15。原测试源码、断言及其版本化报告均保持原样；本次运行结果副本保存在本目录。没有宣布历史全量回归通过。

### 三项历史断言冲突

|原编号|首个失败|根因|处理|
|---|---|---|---|
|109|core_test.c:125，start_count 实际0、预期1|要求Initcall阶段自动连接USB|保留失败；新任务测试覆盖BOOT后启动|
|110|core_test.c:125，同上|测试产品名称前先调用相同自动启动夹具|保留失败；新USB集成测试覆盖任务启动后的GET_DESCRIPTOR|
|201|core_test.c:313，start_count 实际0、预期1|明确断言APPLICATION Initcall启动USB|保留失败，不放宽旧断言|

在独立导出的原 `8d3741a` 上，USB Core 原204项为204 PASS。最初基线导出缺少旧报告依赖，runner在测试后读取历史JSON时退出；补齐基线原报告后重跑成功。测试未作修改。

原 FreeRtosP0 仍调用 `Platform_RtosStart(DummyRun)`、观察 Legacy 私有状态，编译8个错误；现接口为无业务回调的 `Platform_RtosStart(void)`。11项均未进入执行。独立 Native 测试覆盖新接口，不能算作原11项通过。

### 完整重编

|Target|Error|Warning|Code|RO|RW|ZI|
|---|---:|---:|---:|---:|---:|---:|
|CM7 TEST Native|0|0|62986|2662|804|36828|
|CM4|0|0|20350|914|636|2580|

本阶段显式拒绝UVC启动配置，没有声称CM7 UVC业务可运行。日志提供于本目录。

## 测试覆盖与模拟边界

- 三个真实静态任务创建；第1/2/3次创建失败；全部6个Initcall等级失败即停止；失败不开放BOOT，不启动USB。
- BOOT只能由Control发布；早期事件合并，BOOT位不能由公共接口伪造；任务只能等待自己的通知。
- 真实内核阻塞列表、通知和Tick唤醒；任务入口分别验证Camera无限等待、Usb 20ms、Control 1000ms。
- 普通C接口和ISR接口分离；优先级4拒绝，优先级6允许；USB状态提示只唤醒UsbTask。
- 真实BSP、Platform和官方Core执行设备描述符、地址、配置、Reset后EP0、Disconnect、错误STALL及下一请求恢复。
- 所有逐例结果含在 `results.json`、`usb_results.json` 和各历史回归结果副本；失败注入只在测试程序或模拟器hook中实施。

**这些测试不是完整Cortex-M异常调度仿真。** Unicorn跳过首个SVC/Port调度器启动，并显式恢复该Port正常会建立的BASEPRI和优先级校验状态，手动选择真实内核上下文。阻塞调用缺少真实PendSV切栈时，夹具补回TCB通知的WAITING状态，真实阻塞列表/通知代码仍执行。ISR身份由测试值控制，不能证明真实NVIC抢占、PSP保存、FPU上下文、WFI唤醒或栈余量。

实板仍需验证启动、USB TEST枚举、SVC/PendSV/SysTick/TIM6、长期阻塞与唤醒、中断优先级和任务栈高水位。没有Windows枚举成功或相机/UVC视频成功结论。
