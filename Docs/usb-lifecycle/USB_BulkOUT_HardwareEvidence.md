# P1-C C0 — Bulk OUT Hardware Evidence

基线：03a5cb1793e35ec23728f3a6ebbdc132277ed597；分支 codex/usb-device-lifecycle-manager。

VERIFIED（本地文件）：RM0399 Rev5、3535页，SHA256 `61d07b67deb291aed8a5cf8019c94add0a1065318542e57b3287d2694b14bd60`。原PDF仅只读引用，未改、未移动、未跟踪。以下页码均为印刷页码；USB2_OTG_FS 是当前设备控制器，BSP `bsp_usb.c:504` 的 dma_enable=DISABLE，因此审计为 Slave/PIO；不能套用 Buffer DMA 完成条件。

## 手册核验与操作顺序

| 条件 | RM0399 Rev5依据 | 正确顺序、完成条件、失败语义 |
| --- | --- | --- |
| Global OUT NAK 请求 | §60.14.41 p2827–2828；DCTL.SGONAK bit9、CGONAK bit10、GONSTS bit3 | VERIFIED：请求前须确认旧GONAKEFF已清，且软件全局owner空闲；请求写SGONAK不是完成。不能读SGONAK命令位推测拥有者 |
| NAK有效屏障 | §60.15.6 p2909；§60.14.6 p2788，GINTSTS.GONAKEFF bit7（HAL名BOUTNAKEFF） | VERIFIED：请求后仍可能收到有效OUT。硬件向RX队列写NAK pattern，应用pop该pattern后才产生GONAKEFF；必须保留RX消费者进展，不能屏蔽RX后仅轮询有效位 |
| 单个非零OUT Disable | §60.15.6 p2909–2910；§60.14.65 p2851–2853，DOEPCTL EPENA31/EPDIS30/SNAK27 | VERIFIED：全局NAK有效后，仅在目标EPENA已使能时置EPDIS及SNAK；等待目标DOEPINT.EPDISD，并核验EPENA/EPDIS清零。已经空闲的目标要独立证明历史RX及事件退休，不盲写EPDIS |
| Disable事件 | §60.14.62 p2848–2849；DOEPINT.EPDISD bit1、XFRC bit0，W1C | VERIFIED：EPDISD是禁用请求完成；XFRC是编程传输完成，不等价于取消。事件必须在W1C前按原身份登记。EPENA=0可能由不同原因产生，不能单独作为Buffer释放证明 |
| 解除Global OUT NAK | §60.15.6 p2909–2910；DCTL.CGONAK | VERIFIED：原owner确认目标disabled及RX/事件退休后解除，使其他非禁用OUT恢复。GONAKEFF不是W1C清除，应走CGONAK；不得解除其他owner的全局屏障 |
| EP0例外 | §60.14.61 p2846–2847，§60.14.41 p2828 | VERIFIED：不能用EPDIS禁用控制OUT0；即使Global OUT NAK有效，SETUP仍ACK并可入FIFO，不能把屏障当作全RX队列不会再变化 |
| 单端点与全局Reset停止区别 | §60.15.6 p2910–2911 | VERIFIED：手册的Reset型全OUT停止另有全局RxFIFO Flush流程及小于10ms等约束；不能摘取其中RXFFLSH作为取消EP2的本地操作，不在本轮执行该全局序列 |

UNRESOLVED：上述硬件顺序有手册依据，但原HAL下唯一owner、队列屏障归属、PIO引用和迟到事件退休的完整软件证明尚未闭合，C1不得启用。NOT_TESTED：实际芯片时序、GONAKEFF/EPDISD最坏延迟及板级行为。

## 破坏性读取与寄存器所有权表

| 对象 | 读取/写入副作用 | 当前实际所有者 | C1所需规则 |
| --- | --- | --- | --- |
| GRXSTSR +0x1C | VERIFIED：peek，不pop；§60.14.8–9 p2792–2793 | HAL外没有当前生产peek消费者；调试仍可能与IRQ并发看到变化 | 可用于诊断，不是不可变化的队列快照，不独立证明退休 |
| GRXSTSP +0x20 | VERIFIED：每读pop一个status；仅RXFLVL=1时合法；§60.14.10–11 p2794–2795 | HAL RXFLVL :1084 唯一生产device消费者 | 不能在BSP先读后让HAL再读；要在同一次真实pop取得证据 |
| DFIFO(0) RX数据端口 | VERIFIED：读一次消耗数据字；§60.11.2 p2764及Table547 p2775；LL USB_ReadPacket :1010–1040 | HAL在解析status后调用LL，按BCNT消费 | 单消费者、精确字节/尾字处理；不能当普通SRAM反复查看 |
| GINTSTS / GINTMSK | VERIFIED：状态读不pop；若干位W1C；RXFLVL/GONAKEFF是对应层级/效果状态 | HAL重复读flag；HAL改RXFLVLM、GONAKEFFM | 不用普通W1C清RXFLVL/GONAKEFF；全局mask只在统一owner契约内协调 |
| DCTL.SGONAK/CGONAK | VERIFIED：写命令，对所有OUT生效；命令位不是事务Token | HAL ISO OUT :1460–1464请求；任意OUT EPDISD :1146–1148解除 | 全部请求者参与同一lease；禁止BSP单方面声明唯一owner |
| DOEPCTLx/DOEPINTx | VERIFIED：控制写及W1C；普通读不pop | HAL/LL、BSP旧Abort/Close | 非零目标方向独立fence；原事件在清除前按Transfer身份落账 |
| DAINT/DAINTMSK | VERIFIED：摘要/路由，读不消费RX payload | Core/HAL/BSP | 屏蔽EP2摘要不能阻止RXFLVL按EPNUM写EP2 Buffer |
| GRSTCTL.RXFFLSH | VERIFIED：破坏所有共享RX内容 | 现有HAL/LL全局生命周期路径 | 单端点停止禁止使用；除非全接收者及EP0均有独立全局退休证明 |

VERIFIED：手册p2794/2795的Device/Host标题及布局相邻存在容易混淆的交叉排版；本报告用p2793的Device EPNUM/BCNT/PKTSTS表，并与HAL头文件位定义交叉核对。没有把Host CHNUM字段当Device EPNUM。是否为手册编辑错误标UNRESOLVED，不自行改手册。

## 真实HAL调用链与时序

VERIFIED：`CM7/BSP/bsp_usb.c:1071` OTG_FS_IRQHandler → HAL_PCD_IRQHandler。HAL `stm32h7xx_hal_pcd.c:1080–1109`：mask RXFLVL→读GRXSTSP一次→按EPNUM定位OUT_ep→USB_ReadPacket→更新xfer_buff/xfer_count→unmask。数据copy发生在BSP DataOut回调之前；在回调处拒绝旧身份已经太晚，不能逆转先前写入。

VERIFIED：HAL :1125–1128先W1C XFRC再调用私有PCD_EP_OutXfrComplete_int（:2194–2296）。PIO分支转BSP HAL_PCD_DataOutStageCallback（bsp_usb.c:1183）→Platform Usb_DataOut（usb_device.c:1527）清busy→Core DataOut。回调参数只有endpoint及当前指针，没有旧Transfer身份。

VERIFIED：HAL :1144–1164 对任意OUT EPDISD：若BOUTNAKEFF已置位则发CGONAK；仅is_iso_incomplete才发ISO取消回调，Bulk没有普通cancel-complete回调，然后W1C EPDISD。HAL :1413–1425的BOUTNAKEFF分支会mask GONAKEFFM，并对所有is_iso_incomplete端点Abort；:1448–1470会自行请求全局NAK。

VERIFIED：RXFLVL在HAL USBRST(:1332)之前处理；Reset不能当成先排空RX的证明。BSP Reset/Disconnect现有回调不会给OUT提供新C1的FIFO屏障。HAL内NAK、RX和W1C操作都没有可用的普通Bulk取消Token通知。

```mermaid
sequenceDiagram
    participant T as Task / future StopAttempt
    participant B as Future BSP NAK owner
    participant H as HAL IRQ RX consumer
    participant U as USB hardware
    T->>B: Fence new target Receive (not implemented)
    B->>U: SGONAK, only with exclusive lease
    U-->>H: Earlier OUT data / NAK marker / possible EP0 SETUP
    H->>H: Pop exactly once, copy using original identity
    U-->>B: GONAKEFF after marker pop
    B->>U: Target EPDIS + SNAK
    U-->>B: Target EPDISD, EPENA/EPDIS clear
    B->>B: Prove PIO zero, old RX and events retired
    B->>U: CGONAK by same owner
    B-->>T: Commit only after all proofs
    Note over B,H: Current HAL bypasses this ownership contract; C1 blocked
```

此图为INFERRED实施契约，不是已执行的生产时序。源码片段、手册页校验及原字节哈希（完整页文仅保留于ignored output）见 `Tests/UsbBulkOut/reports/source_evidence.json`；真实HAL/LL软件分支证据见hal_evidence.json。
