# C2共享命令：SOF与自有masked Global NAK

VERIFIED（源码）：原OTG_FS_IRQHandler在TX FIFO owner有效时仅容许raw==SOF。C1绑定后，GONAKEFF即使mask关闭仍保持raw level；OUT Disable延迟时它可与SOF并存。把这组纯level当共享写冲突会保守故障，不能据此宣称双向正常推进已验收。

本轮只新增一条受限分发条件：Usb_OutEnabled且Usb_OutEffective确认本lease有效、非releasing、GONAKEFFM关闭，raw恰为SOF|BOUTNAKEFF且SOFM开启。仍只调用HAL_PCD_SOFCallback并W1C SOF，**不进入HAL_PCD_IRQHandler**。其他raw位、Reset、RXFLVL、EP事件、无主NAK或解除中的lease均不排除，保留原fault和HAL异常处理能力。

| 路径 | GRSTCTL写可能性 | 本轮处理 |
| --- | --- | --- |
| BSP IN Poll Flush | 当前唯一owner的定向TXFFLSH | 不改原owner/完成条件 |
| HAL IN EPDISD / ISO Incomplete | HAL可能Flush且忽略结果 | 不列为无冲突；原fault保留 |
| Reset / PCD Stop / DeInit | 全局flush/停止 | 不列为无冲突，不吞掉异常 |
| RX / EP0 Setup / Class回调 | 可能间接Close/Flush | 不进入本白名单，不能仅按中断类别推断安全 |
| SOF受限分发 | 当前MSC SOF为空，UVC只发布机会；BSP非releasing不触发NAK解除 | VERIFIED当前源码没有GRSTCTL写。已有BSP Close/Abort/Flush还受owner门控 |
| masked BOUTNAKEFF | 只是自有屏障raw level；本分支不调用HAL NAK处理器 | VERIFIED不新增命令写者 |

VERIFIED（软件）：dual.json用例20刻意延迟OUT2 Disable，在IN FIFO阶段同时注入SOF和raw NAK；检查此IRQ没有GRSTCTL写、NAK仍合法拥有，随后双方取得真实proof。用例21同轮加入Reset，确认不误排除Reset，IN/OUT源引用继续保留。未直接设置PROVEN或强清busy。

INFERRED：受限分发只应答本SOF；之后到达的其他事件保留给下一IRQ/Task原始状态复核，不假定入口快照之后不会出现新事件。NOT_TESTED：真实总线事件延迟和长期运行。此改动不是EP0/ISO/Controller全闭环，也不把其他组合放宽为成功。
