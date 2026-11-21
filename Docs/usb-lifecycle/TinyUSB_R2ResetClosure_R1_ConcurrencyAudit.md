# TinyUSB R2-ResetClosure-R1 — 事件与并发审核

**VERIFIED（源码/完整AXF选定交错）**：使用同一个原生USBD队列和原USB Task；以下是增量事件发布契约，不是新的应用生命周期管理器。

## 发布顺序

```text
USBRST IRQ
  -> generation fence（一次，立即撤销旧控制/完成资格）
  -> START try-send（失败保留并重试）
原USB Task / 原DCD IRQ
  -> 原硬件停止与FIFO proof
  -> hardware prepare（旧Buffer/Class仍保留）
  -> END try-send timeout=0
       false -> pending / source / Class / IRQ保留
       true  -> 复核generation、raw Reset/Disconnect和fault
                -> DCD旧引用退休、pending清除
原USB Task FIFO消费
  -> START metadata不执行Class Reset
  -> END同generation恰好一次Class Reset（锁外）
  -> 最新合法SETUP（原序号/原tag检查）
```

VERIFIED：接收END不等于Class Reset已经完成；队列每次最多处理配置预算的事件，旧消息较多时需后续Task轮次。Core的`reset_retiring`继续阻止新提交，直到END的Class回调和同generation提交完成。原Task不等待自己释放空位。

## 锁和推进边界

| 路径 | 临界区内容 | 锁外内容/抢占后行为 | 状态 |
| -- | -- | -- | -- |
| Reset try publication | generation、资格字段、原生零等待queue send | event hook在锁外；若hook期间Reset使proof失效，DCD不清旧引用 | VERIFIED |
| DCD final commit | 资格复核、仅已接收END的引用退休 | 失败时原IRQ保留；未全局屏蔽IRQ等待queue/FIFO | VERIFIED |
| pending queue drain | 原Kernel自己的peek/receive短锁，每轮最多8项 | 不执行Class/Backend函数；不能退休的队首保留并最终有界失败 | VERIFIED |
| END Class Reset | 锁内预留本generation的callback资格，回调后再次检查 | 回调无BASEPRI；新Reset可抢占，旧scope已证明静止，旧最终提交不能覆盖新fence | VERIFIED（RAM MSC） |
| usbd_poll_closure | 回调前/后重新检查Reset/pending/fault及旧关闭资格 | 旧组proof尚未完成时Reset保持故障；proof完成后旧回调收尾，新Reset独立推进 | VERIFIED（指定交错） |
| native event loop | 每轮重新检查DCD pending | Reset在入口检查之后抢占，也不会继续执行旧队列里的Backend回调 | VERIFIED |
| 原config重新打开/Status | 保留原控制序号检查和短锁提交 | RAM MSC有界open流程保留；不能泛化任意新Class的回调耗时 | VERIFIED（源码）；板级延迟NOT_TESTED |

VERIFIED：`usbd_spin_lock`使用当前FreeRTOS BASEPRI保护，USB IRQ编码优先级0x50。ARM测试观察Task发送timeout=0，队列发布时短锁为0x50，Class Reset回调为0。IRQ内没有等待队列空位，也没有同步轮询硬件完成。真实中断延迟/完整FreeRTOS调度仍NOT_TESTED。

## 唯一接收者与W1C

- VERIFIED：`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1838`在Reset中对PIO OUT只记录并清EPDISD，不能先清SETUP/XFRC。
- VERIFIED：`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1540`每个状态只读一次GRXSTSP；SETUP_DONE消费者清SETUP/STPKTRX，RX_COMPLETE消费者清XFRC及条件性v3.10a合成标志。Global NAK/RX_DATA/SETUP_RX不能替其他状态清事件。
- VERIFIED：没有OUT buffer被arm时，合成完成仍需退休标志，但不能设置`hardware_completed`或给Class发完成。这是原PIO事件所有权修正，不是ES0445 ZLP时序workaround。
- INFERRED：peek与随后receive之间只有同一Task能移除队首；ISR只能在尾部追加，因此不需要第二队列或长外层锁。当前END/回调等不可丢弃项不被清理。

## ENUMDNE后的SETUP资格

VERIFIED（RM）：RM0399 Rev5 60.6.2 p2752以ENUMDNE标识Reset信号结束；60.14.61 pp2846–2847禁止disable OUT0且SETUP不受NAK/STALL阻挡；60.14.8/9 pp2792–2793的GRXSTSP是破坏性pop。60.15.6 p2910的共享RX Flush重试只适用于此前已批准的Reset窗口，不能为新SETUP随意再次Flush。

INFERRED（组合契约）：只有RX Flush已完成且ENUMDNE已观察到的新EP0 SETUP_RX/DONE对可保留。其8字节写入原DCD setup_buffer，非旧Class/OUT接收Buffer；此后不再RX Flush。END前不执行请求，END后进入原生队列，使用原32bit序号和DCD tag资格。多个SETUP按原协议最新者优先；队列压力不造成旧tag回绕复活。

VERIFIED（AXF）：同一IRQ快照的ENUM+SETUP、旧EP0 IN的Reset禁用仍在推进时到达SETUP、满队列延后发布、连续256次tag回绕都已覆盖。成功场景只发送一次GET_DESCRIPTOR或SET_CONFIGURATION，不以主机重试掩盖丢弃。

UNRESOLVED/BLOCKED：在Reset已经结束、一个新的EP0 IN正活动时，另一个SETUP抢占它仍触发旧ISR停止门禁；本轮测试明确观察fault和原source保留，未扩大为EP0 Stop新实现。实际硅片FIFO/握手事件时序NOT_TESTED，不能以软件模型代替。
