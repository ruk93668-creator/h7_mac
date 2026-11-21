# TinyUSB R2-ResetClosure — Reset 硬件契约映射

**VERIFIED（资料）**：使用本地 RM0399 Rev5 / October 2026 / 3535 页，SHA256 `61d07b67deb291aed8a5cf8019c94add0a1065318542e57b3287d2694b14bd60`。原文件只读、不纳入 Git。正文提取和完整性记录：`output/MSC24/udlm-rm0399/pages2747-2924.txt`、`validation.json`。页码为手册印刷页，与 PDF 页号一致。

**VERIFIED（适用实例）**：RM60.3/60.4.1 pp2746–2747，表538/图794：USB2 OTG_HS / OTG_HS2 的别名 OTG_FS，集成 FS PHY。采用本芯片同一章节的 device/slave 规定，没有借用其他 STM32 系列时序。实验源码为 USB2_OTG_FS、FS48、Slave PIO、480 MHz CM7；不是 USB DMA。

## 寄存器顺序与失败门禁

| 硬件要求 | RM0399 依据 | 本轮映射、前置/完成与失败条件 | 状态 |
| -- | -- | -- | -- |
| USBRST 与 ENUMDNE | 60.6.2 p2752，Powered state；60.14.6 GINTSTS pp2785–2789 | USBRST 表示检测 Reset；ENUMDNE 表示 Reset 信号结束进入 Default。仅提供时窗边界，不自动退休 Buffer。raw ENUMDNE 在 RX Flush 前/后和完成观察时均检查，过早出现锁存 reason20 | VERIFIED（正文/源码），实板 NOT_TESTED |
| Reset IN 停止 | 60.15.6 p2918 Transfer Stop Programming for IN；60.14.55/56 pp2840–2843 | 先 fence AHB/PIO producer；仅 EPENA=1 时发 EPDIS；匹配 EPDISD 在 W1C 前记入 in_done，且 EPENA=0；之后 TX flush。缺事件 reason24，禁止仅凭 EPENA=0 | VERIFIED（RM/模型） |
| Reset OUT Enable/RX Flush | 60.15.6 pp2910–2911 Transfer Stop Programming for OUT | 全 OUT EPENA=1（含 OUT0，但不给它 EPDIS）；AHBIDL=1；RMW RXFFLSH；清零才算完成；单轮 2ms 软件门限，小于10ms；最多一次重试。AHB不空闲21，RX超时22，窗口结束20 | VERIFIED（RM/模型）；2ms 是软件策略，非硅片已测时间 |
| 全局 NAK marker | 60.15.6 p2909 Setting global OUT NAK；p2910 | SGONAK；唯一 IRQ pop GRXSTSP 中 Global NAK marker 后，再观察 GINTSTS.GONAKEFF；owner 独占；未到23，外部占用拒绝，CGONAK 仅本事务 proof 后发 | VERIFIED（源码/模型） |
| 非零 OUT Disable | 60.15.6 pp2910–2911；60.14.61/62 pp2846–2850 | GONAKEFF 后发 EPDIS/SNAK；每个请求对应 EPDISD/W1C，EPENA=0；out_done 与 out_wait 匹配。缺证据25；残留 RX/非空 AHB 也不能前进 | VERIFIED（模型） |
| EP0 OUT 例外 | 60.14.61 pp2846–2847；60.15.6 pp2906–2908 | OUT0 EPDIS 不可写；NAK/STALL 不挡 SETUP。保持 SETUP receiver，不使用普通 OUT 取消流程。旧数据只能由 fenced PIO 消费/丢弃；fresh SETUP_RX+DONE 资格保护新请求 | VERIFIED（RM/源码）；组合安全论证 INFERRED |
| TX FIFO Flush | 60.14.5 p2784；60.15.6 p2918 | 所有目标 IN 禁用完成、PIO refs=0、AHBIDL，GRSTCTL 无其他命令；TXFNUM=0x10 为全 TX，TXFFLSH 清零才继续；共享命令冲突35，超时26 | VERIFIED（RM/模型） |
| RX FIFO Flush/消费者 | 60.14.5 p2784；60.14.8/9 pp2792–2793；60.15.6 p2910 | 仅在 Reset 全组 fence 与 Reset 信号窗口中使用共享 RXFFLSH。Flush 期间 RXFLVL consumer 暂停；Task 从不读 GRXSTSP。完成后旧数据/SETUP/状态在唯一消费者中丢弃 | VERIFIED（源码/模型）；不是单 EP 取消 |
| Flush 时钟 | 60.14.5 p2784 | RM 规定 self-clear 使用较慢 PHY/AHB 的8个时钟；本实现在清零观察后额外等待至少1us DWT间隔，仍有超时。固定 FS48/HCLK240MHz 足够；不把固定指令预算称硬件时间 | VERIFIED（源码）；物理精度 NOT_TESTED |
| NAK 解除 | 60.14.41/45 pp2827–2828/2832 DCTL/DAINT；60.15.6 p2910 | TX 完成后 CGONAK；GONSTS 与 GONAKEFF 均撤销才放 owner；未撤销27。不直接软件清 GONSTS 伪造证明 | VERIFIED（模型） |
| EP0 初始化 | 60.15.6 p2903 | proof 后地址0、FS64 MPS、STUPCNT3、EP0 IRQ masks；保留已分配 RX/TX 布局，避免新 SETUP 写 FIFO 时重划。冷启动无旧引用路径才首次配置 FIFO | VERIFIED（源码/模型） |

## 证明的组成与边界

- VERIFIED：`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:486` 保存当前阶段、等待/已观察端点掩码、NAK/RX事实、Reset generation、DWT/RTOS tick、首次失败阶段和原始 GINT/GRST/DCTL。它只是 DCD 私有 Reset 进度，不提供通用资源管理器。
- VERIFIED：`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1786` 在 EPDISD/XFRC W1C 前识别 Reset 期间事件，不向旧 Class 报送完成。EP0 新 SETUP 使用原生32位序号、DCD tag及fresh RX/DONE 门控；旧 Status completion 沿原 Transfer generation 被拒绝。
- VERIFIED：`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:1511` 是唯一 GRXSTSP pop/PIO copy 入口；Reset pending 不写旧 Buffer。`ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c:128` 在 pending 不再读取旧 IN source。完成 Reset 前不清 source/ff/rx_left/Class。
- INFERRED：USB Transfer 的协议资格因 Reset 撤销，与 hardware/PIO 静止是两件事。本实现把 generation fence 与硬件事件/FIFO proof 分开，只有合取成立才运行原生 Bus Reset/Class Reset。
- INFERRED：OUT0 的例外通过 PIO 写引用关闭、共享 FIFO 退休和fresh SETUP资格组合证明；绝不声称 EPENA=0，不能把该证明用于 DMA。
- VERIFIED：失败保持 pending/fault 和旧指针；IRQ 可继续保留取证/必要事件能力，但本轮不提供自动 Reconcile 或清 fault 重启。

## 明确未证明项

NOT_TESTED：实际 USBRST→ENUMDNE 时间和调度；core何时完成每次 EPDISD/Flush；在真实总线上的共享 FIFO/EP0竞态；精确 IRQ/PIO 访问延迟；ES0445 Rev7 2.24.5 PIO ZLP。REV_ID=0x2003 仍仅定位 U/V组，不区分 U 和 V。

UNRESOLVED：非此固定 FS RAM MSC 配置的 FIFO 重分配、USB DMA/ISO/其他 PHY/动态时钟和复合设备证明。本轮对这些条件不提供正常恢复承诺。Reset 与正在进行的旧取消/组关闭冲突仍受控失败，不伪造合并成功。

实板只有通过上述门禁后才能改变 NOT_TESTED；ARM 模型的显式事件响应不是硅片证明。
