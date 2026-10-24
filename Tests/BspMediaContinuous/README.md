# BSP ↔ Media连续采集联合验收

## 当前修改
- 正式源码只修改CM7/BSP/bsp_dcmi.c，基线见baseline.json。
- StartContinuous仅在参数、状态、硬件空闲检查通过并占用控制权后，关闭事件接收和DCMI中断源，禁用DCMI、清除硬件/NVIC挂起，再清空四个旧事件字段及HAL错误。
- 无效、BUSY和未恢复错误的Start都在清理前返回，旧事件不消费。
- DMA确认停止后将CT置M0，新一轮从M0开始。
- 从接管、配置DBM直到提交RUNNING和CAPTURE，全程保持原临界区；失败出口及成功出口恢复原PRIMASK。当前HAL DBM启动配置无等待循环，不在此临界区等待Tick。
- Stop保持原样，不清理已锁存事件，恢复硬件后旧ERROR仍可读取。
- TryPair在调用Media前补充TC挂起检查，覆盖IN_USE分支直接丢帧、不调用ReplaceTarget时的漏检。
- 旧目标地址重叠保护所在ReplaceTarget函数与基线完全一致；Media、Application、Device、HAL、Scatter、公开头文件未改。

## 真联合测试方式
- joint_test.c直接包含正式bsp_dcmi.c、camera_capture.c及framework_init.c；不复制核心算法。
- 链接正式stm32h7xx_hal_dcmi.c、stm32h7xx_hal_dma.c、stm32h7xx_hal_dma_ex.c、stm32h7xx_hal_gpio.c。
- ARMCC5生成ARM代码，Unicorn执行；仅替换NVIC、时间、PRIMASK、故障注入包装，正常HAL操作调用原HAL函数。
- 通过DMA1_Stream0_IRQHandler→HAL_DMA_IRQHandler和DCMI_IRQHandler→HAL_DCMI_IRQHandler触发正式回调，经过TryPair→Media OnFrameComplete→Rotate→BSP ReplaceTarget→HAL ChangeMemory。
- 寄存器模型提供W1C、MISR=RISR&IER、TC/CT切换及NDTR重装；普通IRQ在PRIMASK=1时排队，恢复后执行。
- 模拟DMA在目标首尾写入测试标记用于检查归属和持有不被覆盖，不模拟真实像素、DVP电气或总线周期。

## 实际命令与编译器
```powershell
python Tests/BspMediaContinuous/run_tests.py
python Tests/CameraStreamState/run_tests.py
python Tests/CameraContextRefactor/run_tests.py
python Tests/DcmiFramePair/run_tests.py
python Tests/DcmiReplaceTarget/run_tests.py
python Tests/CameraFormats/Camera/run_tests.py
```
- ARM Compiler 5.06 update 7 (build 960)。详细编译/链接命令见commands.json；HAL/BSP/Media联合测试编译链接无Error、Warning。

## 结果
|套件|结果|说明|
|---|---:|---|
|BSP + Media联合功能|96 / 96|Y8/RGB565/YUV422，32组功能场景|
|极端延迟风险反例|12 / 12成功复现|4组×3格式；不能解释为配对安全通过|
|CameraStreamState|75 / 75|FAIL 0|
|CameraContextRefactor|72 / 72|FAIL 0|
|DcmiFramePair|82 / 82|FAIL 0|
|DcmiReplaceTarget|133 / 133|FAIL 0|
|CameraFormats/Camera|78 / 78|FAIL 0|

- 功能回归合计536项通过；另有12项风险复现。联合程序总计108个期望判定成立、22614次断言，但必须区分功能验证与风险复现。
- 联合测试覆盖：注册callback/context；M0/M1、TC先/FRAME先；M0AR/M1AR、CT、NDTR、池状态与映射；Get/Release；READY回收30帧；IN_USE持有100帧；正常Stop BUSY；替换失败与故障Stop；Stop失败重试；DRAINING释放及重启；Snapshot完成/释放/超时；旧ERROR在Stop后保留及新Start清除；首帧IRQ延后至RUNNING。
- 初始before_results.json记录启动原子性/清理契约未满足时84个用例被DBM入口断言标记失败，不表示84个独立缺陷。
- 仅修复旧事件和启动临界区后，visible_tc_before_results.json中3个IN_USE下可见TC延迟用例仍失败；补充TryPair检查后全部期望成立。

## 现有配对能证明什么
- TC回调观察到某个DMA目标完成，FRAME回调观察到FRAME标志；pending允许二者任一先到。
- CT检查只能确认读取时完成目标不是当前活动目标。
- 新增TC挂起检查能拒绝“旧pending仍在、又有尚未服务TC”的可见延迟，且不再由是否调用地址替换决定是否执行。
- ReplaceTarget前后检查仍能发现可观察的CT切换、NDTR异常、TC挂起及错误；未改其32 Word门限。

## 不能证明什么：已复现的反例
- IRQ长期屏蔽时，TC/FRAME是挂起位，不是累计计数；多次硬件事件可合并。CT只保留活动目标的奇偶状态。
- 测试构造FRAME来自参考帧1，而随后多个DMA完成合并后TC代表帧3；服务顺序无论FRAME先还是TC先，现有状态仍可能接受并将帧3缓冲区交给消费者。参考帧号仅在测试模型记录，正式代码看不到。
- FREE反例reference_epochs为TC=3、FRAME=1、交付标记=3；IN_USE反例TC=4、FRAME=2，持有帧未被覆盖，但丢帧统计只增加1，不能反映实际跨过的所有完成。
- 给ISR增加软件序号仍无法恢复在IRQ服务前已经合并的事件。本轮没有凭空引入固定帧周期、VSYNC协议或定时恢复机制。
- 因此不能保证任意中断延迟下TC和FRAME来自同一物理帧，也不能凭这些模拟结果宣布连续采集的真实帧时序验收完成。

## 正式Target编译
- CM7：0 Error、0 Warning（增量构建）。
- CM4：0 Error、0 Warning（增量构建）。
- 命令及日志：cm7_keil.log、cm4_keil.log，使用UV4.exe -b正式h7_mac.uvprojx和对应Target。

## 测试兼容调整
- Tests/DcmiFramePair/dcmi_test.c的纯配对用例补充显式通知桩；正式TryPair已有callback调用，旧用例未绑定会访问空指针。该桩不模拟Media，本轮真正Media集成由新联合套件覆盖。
- 其余既有测试只重跑，未调整断言来掩盖正式行为。

## 下一步实板验收
1. 保持默认Application启动方式不变；在独立可审查的实板验收入口启用Stream。
2. 记录DMA TC、DCMI FRAME、CT、NDTR、配对/替换结果、最大IRQ延迟和回调耗时；用GPIO或DWT观察，不在ISR阻塞打印。
3. 在真实PCLK、帧率、SDRAM仲裁及其他IRQ负载下，验证事件是否一一对应、32 Word余量是否足够、是否出现TC/FRAME合并。
4. 验证M0/M1交替、消费持有/释放、READY丢帧、故障Stop→DRAINING→Release→重启，以及图像内容、行对齐与帧连续性。
5. 如需保证超过一帧的IRQ延迟仍可靠，应先设计可观测的帧边界/超时拒绝方案或约束最坏IRQ延迟；不能仅以双pending+CT宣称安全。

本轮未连接实板，未执行图像采集实板验证，未commit/push。

## 联合逐项判定
|编号|用例|判定|
|---|---|---|
|1|注册及原子启动 / Y8|PASS|
|2|注册及原子启动 / RGB565|PASS|
|3|注册及原子启动 / YUV422|PASS|
|4|M0 TC→FRAME / Y8|PASS|
|5|M0 TC→FRAME / RGB565|PASS|
|6|M0 TC→FRAME / YUV422|PASS|
|7|M0 FRAME→TC / Y8|PASS|
|8|M0 FRAME→TC / RGB565|PASS|
|9|M0 FRAME→TC / YUV422|PASS|
|10|M1 TC→FRAME / Y8|PASS|
|11|M1 TC→FRAME / RGB565|PASS|
|12|M1 TC→FRAME / YUV422|PASS|
|13|M1 FRAME→TC / Y8|PASS|
|14|M1 FRAME→TC / RGB565|PASS|
|15|M1 FRAME→TC / YUV422|PASS|
|16|多帧Get/Release / Y8|PASS|
|17|多帧Get/Release / RGB565|PASS|
|18|多帧Get/Release / YUV422|PASS|
|19|READY回收30帧 / Y8|PASS|
|20|READY回收30帧 / RGB565|PASS|
|21|READY回收30帧 / YUV422|PASS|
|22|IN_USE持有100帧 / Y8|PASS|
|23|IN_USE持有100帧 / RGB565|PASS|
|24|IN_USE持有100帧 / YUV422|PASS|
|25|正常Stop等待IN_USE / Y8|PASS|
|26|正常Stop等待IN_USE / RGB565|PASS|
|27|正常Stop等待IN_USE / YUV422|PASS|
|28|替换HAL失败 / Y8|PASS|
|29|替换HAL失败 / RGB565|PASS|
|30|替换HAL失败 / YUV422|PASS|
|31|替换期间CT变化 / Y8|PASS|
|32|替换期间CT变化 / RGB565|PASS|
|33|替换期间CT变化 / YUV422|PASS|
|34|故障Stop及DRAINING / Y8|PASS|
|35|故障Stop及DRAINING / RGB565|PASS|
|36|故障Stop及DRAINING / YUV422|PASS|
|37|Stop失败重试与DRAINING / Y8|PASS|
|38|Stop失败重试与DRAINING / RGB565|PASS|
|39|Stop失败重试与DRAINING / YUV422|PASS|
|40|Stop保留ERROR且重启不误消费 / Y8|PASS|
|41|Stop保留ERROR且重启不误消费 / RGB565|PASS|
|42|Stop保留ERROR且重启不误消费 / YUV422|PASS|
|43|非法Start保留旧事件 / Y8|PASS|
|44|非法Start保留旧事件 / RGB565|PASS|
|45|非法Start保留旧事件 / YUV422|PASS|
|46|BUSY Start保留旧事件 / Y8|PASS|
|47|BUSY Start保留旧事件 / RGB565|PASS|
|48|BUSY Start保留旧事件 / YUV422|PASS|
|49|新Start清事件HAL错误与挂起 / Y8|PASS|
|50|新Start清事件HAL错误与挂起 / RGB565|PASS|
|51|新Start清事件HAL错误与挂起 / YUV422|PASS|
|52|启动首帧IRQ延后到RUNNING / Y8|PASS|
|53|启动首帧IRQ延后到RUNNING / RGB565|PASS|
|54|启动首帧IRQ延后到RUNNING / YUV422|PASS|
|55|DBM启动失败交付新错误 / Y8|PASS|
|56|DBM启动失败交付新错误 / RGB565|PASS|
|57|DBM启动失败交付新错误 / YUV422|PASS|
|58|Snapshot释放后Stream启动 / Y8|PASS|
|59|Snapshot释放后Stream启动 / RGB565|PASS|
|60|Snapshot释放后Stream启动 / YUV422|PASS|
|61|Snapshot超时恢复 / Y8|PASS|
|62|Snapshot超时恢复 / RGB565|PASS|
|63|Snapshot超时恢复 / YUV422|PASS|
|64|空闲Stop保留旧错误 / Y8|PASS|
|65|空闲Stop保留旧错误 / RGB565|PASS|
|66|空闲Stop保留旧错误 / YUV422|PASS|
|67|多轮停止重启 / Y8|PASS|
|68|多轮停止重启 / RGB565|PASS|
|69|多轮停止重启 / YUV422|PASS|
|70|PRIMASK延后真实IRQ / Y8|PASS|
|71|PRIMASK延后真实IRQ / RGB565|PASS|
|72|PRIMASK延后真实IRQ / YUV422|PASS|
|73|NDTR门限错过 / Y8|PASS|
|74|NDTR门限错过 / RGB565|PASS|
|75|NDTR门限错过 / YUV422|PASS|
|76|重复TC / Y8|PASS|
|77|重复TC / RGB565|PASS|
|78|重复TC / YUV422|PASS|
|79|重复FRAME / Y8|PASS|
|80|重复FRAME / RGB565|PASS|
|81|重复FRAME / YUV422|PASS|
|82|可见跨目标延迟拒绝 / Y8|PASS|
|83|可见跨目标延迟拒绝 / RGB565|PASS|
|84|可见跨目标延迟拒绝 / YUV422|PASS|
|85|风险反例FRAME先服务跨帧合并 / Y8|风险复现（非安全通过）|
|86|风险反例FRAME先服务跨帧合并 / RGB565|风险复现（非安全通过）|
|87|风险反例FRAME先服务跨帧合并 / YUV422|风险复现（非安全通过）|
|88|风险反例TC先服务跨帧合并 / Y8|风险复现（非安全通过）|
|89|风险反例TC先服务跨帧合并 / RGB565|风险复现（非安全通过）|
|90|风险反例TC先服务跨帧合并 / YUV422|风险复现（非安全通过）|
|91|DRAINING接口门控 / Y8|PASS|
|92|DRAINING接口门控 / RGB565|PASS|
|93|DRAINING接口门控 / YUV422|PASS|
|94|Media候选旧目标重叠拒绝 / Y8|PASS|
|95|Media候选旧目标重叠拒绝 / RGB565|PASS|
|96|Media候选旧目标重叠拒绝 / YUV422|PASS|
|97|风险反例FREE下跨帧FRAME先服务 / Y8|风险复现（非安全通过）|
|98|风险反例FREE下跨帧FRAME先服务 / RGB565|风险复现（非安全通过）|
|99|风险反例FREE下跨帧FRAME先服务 / YUV422|风险复现（非安全通过）|
|100|风险反例FREE下跨帧TC先服务 / Y8|风险复现（非安全通过）|
|101|风险反例FREE下跨帧TC先服务 / RGB565|风险复现（非安全通过）|
|102|风险反例FREE下跨帧TC先服务 / YUV422|风险复现（非安全通过）|
|103|IN_USE下TC挂起且CT绕回仍拒绝 / Y8|PASS|
|104|IN_USE下TC挂起且CT绕回仍拒绝 / RGB565|PASS|
|105|IN_USE下TC挂起且CT绕回仍拒绝 / YUV422|PASS|
|106|FREE下TC挂起且CT绕回仍拒绝 / Y8|PASS|
|107|FREE下TC挂起且CT绕回仍拒绝 / RGB565|PASS|
|108|FREE下TC挂起且CT绕回仍拒绝 / YUV422|PASS|
