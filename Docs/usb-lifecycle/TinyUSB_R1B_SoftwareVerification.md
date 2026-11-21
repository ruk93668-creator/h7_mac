# TinyUSB R1B 软件验证

**VERIFIED（ARM 软件）：33项/313断言通过；原MSC 12项/1679断言通过；9项等待边界检查全部返回。NOT_TESTED：硬件。**

基线 `5ba9e0e6444bf6134e7fdb3d4cb89b3b2d69e129`。ARM Compiler 5.06 update7 build960，Cortex-M7/Thumb，armlink；Unicorn执行实际修改后 ARM 对象。所有命令、源码SHA256、返回值、寄存器写入、破坏性RX读取、失败原因及AXF路径在下列JSON中，构建产物保留在 ignored `output/TinyUSBReplacement/`。

## 最终结果

| 入口 | 结果 | 证明边界 |
| --- | --- | --- |
| `python Tests/TinyUSBReplacement/run_r1b_tests.py` | VERIFIED：33/33、313断言，exit0 | 真实DWC2 + USBD + MSC + RAM，MMIO硬件模型 |
| `python Tests/TinyUSBReplacement/run_r1b_wait_tests.py` | VERIFIED：9/9观察符合预期，exit0 | 包含原6挂死反例的有界返回；非9个取消成功 |
| `python Tests/TinyUSBReplacement/run_stack_tests.py` | VERIFIED：12/12、1679断言，exit0 | 原Core/MSC回归，Fake DCD；不能作为硬件取消证明 |
| `python Tests/TinyUSBReplacement/compile_probe.py` | VERIFIED：18对象编译0 Error、57 Warning，exit1 | RAM/SD只读/UVC各6个组件；不是完整固件链接 |
| `python Tests/TinyUSBReplacement/audit_upstream.py` | VERIFIED：52文件上游Git blob一致，exit0 | 累计9文件本地差异；本轮生产改8文件 |
| 原 `run_r1_tests.py` | VERIFIED：0/2，原始失败保留 | 一项旧fixture未映射DWT导致非法读，一项仍要求R1立即FAILED；未修改旧断言 |

证据：[R1B](../../Tests/TinyUSBReplacement/reports/r1b.json)、[等待检查](../../Tests/TinyUSBReplacement/reports/r1b-waits.json)、[MSC](../../Tests/TinyUSBReplacement/reports/stack.json)、[组件编译](../../Tests/TinyUSBReplacement/reports/compile.json)、[旧R1失败](../../Tests/TinyUSBReplacement/reports/r1.json)、[源码保留清单](../../Tests/TinyUSBReplacement/reports/R1B_receipt.json)。

## 重点断言

VERIFIED：Bulk IN、OUT均通过真实接收/发送提交，随后PENDING保持BUSY和原source，硬件NAK/Disable/Flush事件被实际ISR消费后SUCCESS才归还。正常XFRC先完成、取消先发、重复取消、10轮实际tud_deinit/tud_rhport_init、旧generation回调和迟到raw事件均覆盖。

VERIFIED：真实CBW→MSC BOT Reset SETUP→Core→DCD路径分别覆盖Data IN/OUT。PENDING时无Status；退休后才Status并重新收CBW。补充的IN Disable缺失和OUT NAK缺失在50ms deadline后FAILED，旧source仍保留、不Status、不接受新传输。新SETUP取代旧Reset不能收到旧Status。

VERIFIED：缺NAK、缺EPDISD、TX Flush不完成、外部Global NAK、NAK解除不完成、USB Reset/Disconnect、未处理Reset、Core Reset失败、DWT不可用/停走/回绕均有观察。旧RX在真实pop/copy路径被丢弃，不覆盖旧buffer；其他OUT在lease释放后实际收到PIO数据。共享TX Flush期间EP0 PIO暂停，完成后通过实际IN IRQ续传。

VERIFIED：旧同步等待的OUT两个反例现在由未资格化的legacy OUT入口立即拒绝；这不是完成OUT取消。新异步OUT入口缺NAK/EPDISD由R1B专项独立验证。IN缺NAK/EPDISD、TX/RX Flush、AHBIDL/Core Reset均有返回；不是由模拟器强杀得到PASS。9项中的ZLP项只是记录未修改的combined CNAK/EPENA顺序，**不代表勘误已修复**。

INFERRED：模型寄存器转移符合已记录的H747 PIO硬件假设；它不能代替硅片时序、总线行为、调度和实板停止证明。模型仅改变MMIO，不写生产取消状态、proof、buffer引用或直接Mock DCD成功。共享旧UDLM测试目录仅复用通用MMIO harness，不导入旧UDLM生产代码。

## 故障证据和编译限制

VERIFIED：未删除原测试。receipt逐字节比对12份旧C/Python测试源码与基线；仅audit工具扩充补丁审计。报告每次覆盖前按SHA保存`*-history-*.json`，所有旧无返回、DWT映射错误、初版NAK/EP0模型缺口、共享TX复测失败、组件61警告等证据保留；未将旧断言改为PASS。

VERIFIED：基线组件编译也是57 Warning；最终消除了中间新增的4项隐式DCD查询警告，余下57项为既有FreeRTOS tick/mutex接口和enum范围警告。组件exit1属预设warning门禁，不能写成零警告构建通过。完整RAM MSC/SD只读/UVC固件NOT_BUILT：旧FreeRTOS集成缺口未越权修改，也未启用新的生产USB路径。

NOT_TESTED：硬件正常取消、物理拔插、Windows、USBPcap、SD写入、UVC活跃流、PIO ZLP勘误、FreeRTOS运行时IRQ优先级/临界区。没有烧录。
