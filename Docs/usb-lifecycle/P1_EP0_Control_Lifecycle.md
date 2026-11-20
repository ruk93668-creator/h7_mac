# P1-E：EP0控制事务门禁与接口申请

BLOCKED：P1-E完整实现未完成；不是一次“调整busy标志”即可修复。独立扩展申请见P1_LL_ZLP_InterfaceExtension_Request.md；没有修改ST LL/Core，也没有把CPU循环或Delay当AHB时序证明。

## 已核验调用链与风险

VERIFIED（9d51起点）：usb_device.c Usb_Setup:1444先InvalidateControl，再对历史EP0 busy尝试Close/Open，之后同步Core Setup。BSP CloseEp EP0分支调用HAL_PCD_EP_Abort→LL USB_EPStopXfer，对active OUT0发EPDIS；RM0399 Rev5 §60.14.61 p2846–2847不允许此用法。新SETUP是协议抢占，不自动证明旧RX/PIO/状态包已退休。

VERIFIED：现有Deferred Status只在当前Class Setup登记特定0x21请求、严格Token匹配；Core普通标准请求没有同一套Task Prepare门控。Core同步DeInit被忽略的路径不能靠返回BUSY等待Task，USBD_CtlSendStatus的返回值也不能作为硬件提交/主机完成证明。需要Platform在进入Core前决定保持、否决或提交，且不得重复调用Core完成同一请求。

VERIFIED：真实ARM门禁探针Tests/UsbLifecycleFinal/reports/gate.json记录EP0及非零IN ZLP当前写序；active OUT0通用Abort会提交EPDIS且超时时保留原指针，inactive对照不发该写。5项观察满足预期是“受阻证据成立”，不是5项ZLP修复通过。实物是否产生坏包及AHB实际周期均NOT_TESTED。

INFERRED：目标ControlTransaction={controller,session,setup_seq,token,stage,source_ledger,rx_ledger,status_submission,status_completion}。SETUP永久缓冲独立于普通DATA/STATUS Buffer；旧Token失效只取消继续协议资格，旧PIO引用留给匹配的真实消费者退休。每次新SETUP须在唯一RX消费者处建立顺序水位，W1C前归属、同Session的最终状态只能交付一次。

INFERRED：Reset/Disconnect先门控新提交并失效旧Control资格，再由Task收集端点/消费者/EP0票据；不能在ISR等待。在所有旧资源退休且Controller重新初始化证明成立前，不给新Session发Ready。SET_ADDRESS/SET_CONFIGURATION、UVC Probe/Commit、BOT Deferred Status都必须保持官方Core语义，不能把命令解析搬进随意异步队列或改全体Core状态值。

BLOCKED：尚无获批ZLP全入口原语、合法OUT0静止证明、标准请求Deferred Prepare票据及旧状态包跨事务退休闭环。自动启用C1、整机重枚举与复位恢复因此不推进；保持明确故障，不伪造Status成功。
