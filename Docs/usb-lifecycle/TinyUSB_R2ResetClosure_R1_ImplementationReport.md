# TinyUSB R2-ResetClosure-R1 — 实施报告

**VERIFIED（源码/完整AXF模型）**：修复原生队列满时的Reset事件发布等待及提前清DCD引用；ENUMDNE后合法SETUP可以保存并在END后处理，无需主机重发。**NOT_TESTED（实板）**：未烧录、未合并main。**UNRESOLVED/BLOCKED**：恢复后活动EP0 IN被新SETUP抢占的旧ISR停止证明仍不足，保留受控故障门禁，未宣称全部EP0恢复完成。

## 基线与独立分支

VERIFIED：基线 `f4dada109a9302a5eab66bcad8e2d7ad82a16738`，开始时本地/远端一致；从该提交新建 `codex/usb-tinyusb-r2-resetclosure-r1`。工作区 `D:/32/STM32H7/h7_mac_lifecycle`。没有修改main、原P1分支或原工作区文件。PDF及两项原有未跟踪Keil/RTE文件保留，不纳入提交。

## 根因与修复

| 问题 | 原行为/证据 | 本轮行为 | 结论 |
| -- | -- | -- | -- |
| END满队列 | reset_poll持spin锁→void dcd_event_bus_reset→queue_event→FreeRTOS无限等待 | result-bearing `dcd_event_reset_try`，Task send timeout=0；发送结果返回DCD | VERIFIED |
| 发布前退休 | 基线完整AXF在满队列send入口的source已为0 | 分离hardware prepare与`reset_retire_sources`；只在END被接收且硬件资格仍有效时清引用/pending | VERIFIED |
| START满队列 | void接口无法证明送达，可能锁存事件丢失故障 | 先一次性建立generation fence；非阻塞尝试START，DCD按原Task重试 | VERIFIED |
| 同Task无空位 | pending时原Task不消费队列 | 原队列peek/receive每轮最多8个已确定过期事件；不运行或丢弃未知回调 | VERIFIED；是软件预算，不是硬件超时 |
| 非可退休事件占满 | 不能证明可丢弃或执行Backend回调 | 保留队列和旧资源，END发布阶段50ms或累计期限失败；fault后停止清理队列 | VERIFIED（负向模型）；不承诺该组合正常恢复 |
| 重复START/END | 仅依靠队列内容可能重复Class Reset | 非零generation的START只作metadata；END按started/done generation最多执行一次Class Reset | VERIFIED；legacy generation0保留原行为 |
| 新SETUP被丢弃 | pending分支丢弃全部SETUP | rx_flushed且ENUMDNE后，保存最新合法RX/DONE对及原8字节setup_buffer；END后原队列提交 | VERIFIED（模型），硬件组合论证INFERRED |
| 序号与重试 | 单靠8bit tag可能在队列压力中回绕 | 每个合法SETUP_RX先推进原生32bit SETUP序号；重试不重复分配，末个序号可使用，耗尽拒绝 | VERIFIED |
| PIO W1C所有权 | Reset端点IRQ可能先清OUT SETUP/XFRC | OUT IRQ只处理EPDISD；SETUP/XFRC及条件性v3.10a合成状态由唯一FIFO pop路径退休 | VERIFIED（源码/模型） |
| Class/closure抢占 | 旧回调结束后可能覆盖新Reset fence | 回调前后复核身份/资格；回调在锁外，最终Core状态只在同generation且无pending/fault时提交 | VERIFIED（选定抢占点） |

## 实际基线对照

VERIFIED：加载保留的基线完整AXF，SHA256 `28b17dae3c265752db075c2c93b6648d33414c14e6694685cd1d5c6433132c9b`，在真实ARM `xQueueGenericSend`入口观察：队列32/32、timeout=`0xFFFFFFFF`、BASEPRI=`0x50`、旧source已清零。记录 `reports/r2-reset-r1-baseline-probe.json`。

INFERRED（死锁因果）：消费者Task在持锁情况下等待自己消费队列以释放空位，不能构成正常完成路径。探针刻意在阻塞调用入口暂停，**没有**把不具备真实调度环境的模型声称为“实板死锁复现”。新完整AXF实测满队列调用返回false、保留旧source/pending、同Task清理过期项后重试成功。

## 源码定位与兼容范围

- VERIFIED：`ThirdParty/tinyusb/src/device/usbd.c:1703` 原生Reset非阻塞发布；`ThirdParty/tinyusb/src/osal/osal_freertos.h:280` 的Task路径使用`xQueueSendToBack(...,0)`，ISR复用原FromISR路径。
- VERIFIED：`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1396` 推进和重试；`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1291`只准备硬件；`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1319`仅在接收END后清DCD引用。
- VERIFIED：`ThirdParty/tinyusb/src/device/usbd.c:1756`只消费过期XFER/旧SETUP/START/SOF/挂起恢复事件；保留FUNC_CALL、unplug和当前END，fault后冻结队列。
- VERIFIED：`ThirdParty/tinyusb/src/device/usbd.c:1731`与`ThirdParty/tinyusb/src/device/usbd.c:1742`使用原生资格和队列；`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1540`是唯一破坏性GRXSTSP消费者，Task不新增RX pop。
- VERIFIED：生产代码变更限于TinyUSB `dcd.h`、`usbd.c`、`osal_freertos.h`、`osal_none.h`、`dcd_dwc2.c`。未改MSC/BOT/SCSI、HAL/LL/ST Core、BSP/Platform/Task、SD/FatFS、CM4/MPU/Scatter。独立实验Keil项目仅由构建工具刷新输出路径。
- VERIFIED：没有新Task、第二个队列、动态分配、Lifecycle Manager、StopAttempt或Resource Ledger。原UsbTask、DCD事件/USBD队列/MSC继续承担各自职责。
- VERIFIED：FreeRTOS和OS_NONE的USBD普通event发送也改为非阻塞；`usbd_defer_func`失败锁存Core fault、保留Class/IO；`tud_task_stop`满队列返回false。其他OS没有新增非阻塞契约，专用try接口拒绝，不宣称跨OS验证。
- VERIFIED：等候边界独立DCD夹具只补了链接依赖，9项原断言未改；fail-only测试适配器在最终等待测试AXF中被链接器剔除，不参与任何成功路径。生产完整AXF未编译该适配器。

## 构建、产物与范围

VERIFIED：ARM Compiler 5.06 update7 build960，完整RAM MSC Keil重编 **0 Error、0 Warning**。Code=53558、RO=1106、RW=348、ZI=25852字节。

- AXF/MAP/HEX/build.log：`D:\32\STM32H7\h7_mac_lifecycle\output\TinyUSBReplacement\R2ResetClosureR1-review-9e751116e2d8`。
- AXF SHA256：`9e751116e2d85bb5159b4d4ddc99fb950c8ff1977682ecf97f1625f49bd53e09`。
- 精确文件哈希及唯一IRQ/最终符号：`Tests/TinyUSBReplacement/reports/R2ResetClosureR1_receipt.json`。
- 本轮完整生产Diff：`reports/R2ResetClosureR1_production.patch`（reports相对Tests/TinyUSBReplacement）。
- 相对固定上游的累计Diff：`reports/R2ResetClosureR1_upstream.patch`。原52个Git blob验证通过，累计本地差异12个文件；本轮只改其中5个生产代码文件。

VERIFIED（软件）：新专项29/29、261断言；R1B33/33、313；MSC12/12、1679；等待边界9/9。原Reset31为30/31，原Closure25为24/25，原R2为17/19；原断言和历史FAIL均保留，分类见软件报告。NOT_TESTED：所有板级/Windows/USBPcap时序、物理DWT/RTOS调度和ZLP勘误。普通提交/推送并核对远端SHA后停止，不自动烧录或合并。
