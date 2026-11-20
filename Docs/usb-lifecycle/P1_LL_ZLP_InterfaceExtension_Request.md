# P1-FINAL：最小LL/EP0接口扩展申请（未获批准，不实施）

BLOCKED：本任务不授权修改ST LL/Core。当前USB_EPStartXfer（Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:743，PIO启动:818）把CNAK/EPENA合并写入；BSP Transmit经HAL必经该路径，后置回调不能回滚已发出的ZLP。BSP_USB_Stop:1125和CloseEp的EP0分支又经HAL_PCD_EP_Abort→USB_EPStopXfer对OUT0发EPDIS，不能作为EP0静止证明。

VERIFIED（官方原文）：ES0445 Rev7 §2.24.5 p44的Device/Slave序列要求先SNAK且不CNAK，等待至少20 AHB周期，置EPENA，再等15 AHB周期，最后CNAK并解除SNAK；适用于软件编程的IN ZLP。§2.24.2限定Buffer DMA，不能拿其DMA等待规则套入当前PIO。来源：https://www.st.com/resource/en/errata_sheet/es0445-stm32h745xig-stm32h755xi-stm32h747xig-stm32h757xi-device-errata-stmicroelectronics.pdf 。本轮联网取得正文为60页Rev7，非搜索索引旧Rev6。

INFERRED（最小方向，供审核）：新增一项LL Device/Slave ZLP启动原语，保持现有非ZLP/DMA/Host不变；由HAL在初始化前明确选择、整套启用，禁止某些ZLP仍走旧启动。原语接收端点及经过核验的AHB周期屏障提供者，严格分离三个命令。必须覆盖EP0 status、Core自动补ZLP和MSC/UVC调用，不仅修改公开USB_Device_Transmit。屏障证据需核对时钟比、优化、总线等待、反汇编及真实板级时序；HAL_Delay/循环迭代次数不能当AHB时间证明。

INFERRED：EP0另需HAL PCD control-event身份边界和合法controller-wide静止原语/公开查询，保留SETUP永久接收；新SETUP必须在唯一pop路径取消旧协议资格，不能对OUT0套用通用EPDIS。Core维持官方实现，Platform门控标准请求并在Task准备后同步调用Core，仅在证明Core不会重复/错误提交status后启用；若仍需Core变化，追加单独申请，不借本次申请重写Core。

验收要求：真实ARM测试逐条记录寄存器写入顺序；不以模拟时钟数证明物理AHB间隔；旧Control Token迟到事件、双向历史状态、Reset/Disconnect、标准请求status否决和所有ZLP入口均有反例测试。任何失败保留Buffer。NOT_TESTED：目前无上述已批准原语或板级时序证明。

受阻依赖：P1-E完整EP0；C1生产自动启用；BOT Deferred Reset最终成功；UVC Alt0同步请求的Task准备；全设备Reinitialize/Resume及板级执行。可继续独立双向端点票据、资源审计、现有基线回归和硬件准备工具。该申请不是批准，也不是已完成实现。

## 供审批的具体接口边界（仅提案）

INFERRED：拟修改文件仅 `Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_usb.h`、对应 `.c`，以及已授权HAL PCD/BSP适配和测试。先新增原语而非替换整个LL启动函数；无证据的时钟屏障提供者不能注册为可用。

```c
/* Candidate API only; not compiled or implemented in this task. */
typedef struct USB_AhbBarrierOps USB_AhbBarrierOps;
typedef struct USB_ZlpStartEvidence USB_ZlpStartEvidence;
HAL_StatusTypeDef USB_EPStartZlpSlaveQualified(
    USB_OTG_GlobalTypeDef *usb, USB_OTG_EPTypeDef *ep,
    const USB_AhbBarrierOps *barrier, USB_ZlpStartEvidence *evidence);
```

INFERRED：barrier必须在首次命令前证明适用于当前clock tree，提供“至少指定AHB周期”的合格屏障；证据带controller/session/control身份和最后实际提交阶段。未合格则零硬件副作用拒绝；部分命令后失败不得释放源，保留阶段和first_error。HAL所有Device Slave零长度IN入口统一路由，其他模式保留旧行为；管理器选择新契约时严禁失败后回退旧ZLP路径。

INFERRED：EP0原始状态查询只可提供事实，不命名为“已静止”；必须结合唯一RX消费水位、旧事件和PIO引用。controller停止还需逐次保存真实Flush返回值；如果现有公开接口不能给出完整结果，另列最小接口补充，不能用HAL_OK补齐证据。Core若最终不可免改，需再次单独审批，不在这份候选原语里暗中包含Core修改。

UNRESOLVED：AHB屏障的具体实现、资格证据及EP0完整控制协议退休契约尚未验证。此接口签名为可审核边界，不是已经获得安全性证明的代码。须人工批准后另阶段实现、反例测试和实物时序验收。
