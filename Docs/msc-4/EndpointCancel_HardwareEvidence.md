# EndpointCancel 硬件证据与核验记录

日期：2026-11-19。源码基线：`a0ec0eb72f5260d9602b32a996db5f0e76417c6a`。本轮只读源码、既有快照及公开资料，不操作板卡、不修改生产代码。

VERIFIED需注明“源码”“既有实测”或“手册”；三者不能相互冒充。INFERRED为推断；UNRESOLVED为无法确认；NOT_TESTED为未验证。与[取消契约](EndpointCancel_Contract.md)、[停止生命周期](UsbStop_Lifecycle.md)、[测试计划](EndpointCancel_TestPlan.md)共同审阅。

## 1. 证据索引

| ID | 来源 | 本文使用范围 |
| --- | --- | --- |
| S1 | `Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h` | 控制器基址、寄存器结构、位位置和SDK名称 |
| S2 | `Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_usb.h:458–488` | 端点寻址宏和清中断宏 |
| S3 | `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:915 USB_EPStopXfer` | 当前IN/OUT停止指令及同步等待条件 |
| S4 | `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c` | IRQ顺序、XFRC/EPDISD处理、USBRST/ENUMDNE及Abort包装 |
| S5 | `CM7/BSP/bsp_usb.c` | PIO占用、Stop/Abort/Flush/回调、NVIC状态 |
| S6 | `CM7/Platform/usb_device.c` | endpoint busy、Token取消、回调过滤及Class释放 |
| E1 | `Tests/MscReconnect/results/msc41_ram_initial_results.json` | 已保存RAM首次枚举状态 |
| E2 | `Tests/MscReconnect/results/msc41_ram_reconnect_results.json` | 已保存重连失败状态 |
| E3 | `Tests/MscReconnect/results/ram_fault_extra_results.json` | 已保存OUT2/IN2与NVIC寄存器 |
| E4 | `Tests/MscReconnect/results/ram_capture_summary.json` | 既有USBPcap哈希、时间和首次枚举帧号 |

引用源码SHA256：

- S1：`07675267559bfc8ef162a932f937488289a45b6f007fa5476c53fbd5b6d4c199`。
- S3：`e18900b0d91d79d8eea02d60eec865ea0eada440a053354c81b709c33f5e890a`。
- S4：`f1c48624654428a73a196bcd83b7b8477a0b6cebb1a229088090982f957823fa`。

外部参照：ST的[RM0399 Rev4官方地址](https://www.st.com/resource/en/reference_manual/rm0399-stm32h745755-and-stm32h747757-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)与[ST H7官方LL源码](https://github.com/STMicroelectronics/stm32h7xx-hal-driver/blob/master/Src/stm32h7xx_ll_usb.c)。外部master仅作2026-11-19查阅的差异线索，不视为工程已使用版本，也不直接复制进生产代码。

## 2. 配置与地址：已从源码核对

VERIFIED（S5）：使用USB2 OTG FS、Full-Speed、片内PHY，`dma_enable=DISABLE`，通过CPU读写FIFO；本设计不将USB内部DMA完成语义套在该配置上。SDMMC IDMA是独立硬件所有者。

VERIFIED（S1:2294–2299，S2）：USB2基址`0x40080000`，Device区偏移`0x800`，IN端点区`0x900`、OUT端点区`0xB00`，端点间距`0x20`。下表地址属于此实例，不能直接用于USB1/其他芯片。

| 寄存器 | 地址/偏移 | 本轮核对内容与来源 |
| --- | --- | --- |
| GINTSTS / GINTMSK | `0x40080014 / 18` | S1/S4：原始事件与全局使能必须分别记录 |
| GRXSTSR / GRXSTSP | `0x4008001C / 20` | S4 IRQ通过GRXSTSP弹出RX状态；诊断不得当普通无副作用寄存器读取 |
| DCFG | `0x40080800` | S1:28368，DAD为bits10:4；旧软件地址不能替代此值 |
| DCTL / DSTS | `0x40080804 / 08` | S1，控制请求/NAK状态、设备状态分开解释 |
| DIEPMSK / DOEPMSK | `0x40080810 / 14` | S1:28626、28686，EPDM bit1 |
| DAINT / DAINTMSK | `0x40080818 / 1C` | S1:28920，IN位n、OUT位16+n |
| DIEPEMPMSK | `0x40080834` | S4/S5：PIO续传与TX FIFO空事件有关，不能遗漏CPU继续访问buffer的路径 |
| DIEPCTLn / DIEPINTn | `base+0x900+0x20*n / +8` | S1/S2；IN2为`0x40080940 / 48` |
| DOEPCTLn / DOEPINTn | `base+0xB00+0x20*n / +8` | S1/S2；OUT2为`0x40080B40 / 48` |

关键位核验：

| 位 | SDK位置 | VERIFIED（源码）的含义/使用 | 不能直接证明 |
| --- | --- | --- | --- |
| EPENA | IN/OUT CTL bit31，S1:29349/29638 | endpoint enable；S3轮询该位清除 | CPU不再续传、回调已排空 |
| EPDIS | IN/OUT CTL bit30，S1:29346/29635 | endpoint disable请求；S3设置 | 取消已经完成 |
| SNAK | IN/OUT CTL bit27，S1:29337/29632 | Set NAK请求 | endpoint完全停用 |
| NAKSTS | OUT CTL bit17，S1:29609 | NAK状态 | EPENA清零或buffer可释放 |
| XFRC | IN/OUT INT bit0，S1:29476/29643 | S4进入正常数据完成处理 | 收到取消确认、整个多包MSC数据阶段完成 |
| EPDISD | IN/OUT INT bit1，S1:29479/29646 | endpoint disabled interrupt；S4有独立处理分支 | 无身份地将标志归给当前新Transfer |
| STUP | OUT INT bit3 | S4分发Setup；不同于XFRC与取消 | 可以随旧事务一起无条件清除 |
| SGONAK / CGONAK | DCTL bit9/10，S1:28456/28459 | 设置/清除Global OUT NAK的请求位 | 写请求后已经生效 |
| GONSTS | DCTL bit3，S1:28440 | Global OUT NAK状态 | 所有旧RX条目均已退役 |
| BOUTNAKEFF | GINTSTS bit7，S1:28742 | Global OUT NAK effective | 全部端点或SDMMC已静止 |
| RXFLVL | GINTSTS bit4，S1:28733 | RX FIFO非空 | FIFO首项属于取消目标，可以随意弹出 |
| USBRST / ENUMDNE | GINTSTS bit12/13，S1:28751 | S4分别处理硬件复位及枚举完成 | 两者等价或同时调用上层Reset |

NVIC IRQ101与USB内核中断使能是两个层级，另有BSP `s_ready/s_started`门控。VERIFIED（E3）：NVIC ISER3 bit5=0时USB IRQ未使能，即使PCD State=READY、Lock=UNLOCKED、GINTMSK非零也不能称软件仍能处理USB IRQ。

## 3. IN/OUT停止机制：当前源码究竟证明什么

VERIFIED（S3）：当前IN与OUT分支均在EPENA置位时设置SNAK、EPDIS，再最多轮询约10000次等待EPENA清零；失败返回HAL_ERROR。它不是异步Request/完成回调接口。VERIFIED（S4:1992–2007）：HAL_PCD_EP_Abort直接把该返回值向上传递。

VERIFIED（S5:337–370）：BSP Abort还有FIFO处理和PIO占用归还；任一步失败都不能把BSP成功语义缩减成“HAL请求提交过”。当前IN成功路径清本端点旧事件并清TX状态；OUT成功路径才清RX active。共享RX是否安全处理仍须结合其他接收状态。

VERIFIED（S4:1144–1164、1234–1251）：HAL有EPDISD处理；IN分支还调用TX FIFO flush，普通Bulk不通知专用取消完成回调，只有对应iso incomplete分支才走iso通知。**HAL处理结束后EPDISD可能已经被清除；事后读到0既不能证明没有发生，也不能证明完成。**

VERIFIED（S4）：IN的该次flush返回值被忽略。因此“HAL走过EPDISD分支”仍不能代替驱动要求的FIFO/PIO收尾成功证明；拟议取消契约必须保留实际结果，不能从回调返回或PCD READY推导已安全归还。

外部官方LL参照包含Global OUT NAK生效等待及OUT停用确认，而本地版本没有该段。INFERRED：此差异可能关联本工程OUT取消失败；当前没有完整首次失败事件链，不能把版本差异直接提升成最终根因。该外部代码还涉及RX状态弹出，不能未经归属验证直接照搬。

### EPDISD、EPENA、XFRC的判读矩阵

| 观察 | 本轮可作出的结论 | 取消契约要求 |
| --- | --- | --- |
| EPENA=1、EPDIS=1 | 已请求停用，仍未看到停用结果 | Pending/Failed，保留引用 |
| EPDISD=1，且能绑定本次Cancel | 收到目标停用事件候选 | 还要核验控制状态、PIO封锁和旧事件屏障 |
| EPENA=0，无EPDISD证据 | 可能空闲、正常包完成、事件已清或其他边界 | 不能单独判Cancelled |
| XFRC=1 | 一个当前硬件传输完成候选 | 确认是哪一PIO包/Transfer；不能当Cancel事件 |
| XFRC与EPDISD都为1 | 两类事件都待处理 | 按目标身份及整次Transfer证据决定唯一终结，不臆测先后 |
| EPDISD在HAL后为0 | 可能已被HAL应答 | 依靠HAL前捕获及软件证据记录，不重新造事件 |

UNRESOLVED（H-U1）：RM0399对各端点类型的精确停用编程序列、EPDISD与EPENA/EPDIS硬件清除的保证、EP0例外及限制尚未完成正文核验。设计采用更保守的组合证明，不将本矩阵冒称为手册已逐条确认的保证。

## 4. HAL IRQ前后如何取得证据（拟议设计）

现状VERIFIED：BSP `OTG_FS_IRQHandler:619`直接调用HAL；普通Bulk没有取消事件记录。以下为拟议方案，不是已有功能。

1. **HAL前只观察。** 在调用HAL前，按本轮不可变arm映射，记录controller/session/endpoint incarnation、Transfer/Cancel身份、GINTSTS和mask、DAINT和mask、相关端点INT原始值与使能值、CTL、IRQ入口序号。原始置位与使能事件分开保存；未使能旧标志不能直接视为新完成。
2. **不可事后套当前epoch。** HAL会同步调用Setup、Reset、数据回调，期间软件Session可能变化。记录必须沿用进入时的旧目标，收尾资格不受新Session过滤。
3. **只调用一次原HAL IRQ。** 不为制造NAK生效而递归进入HAL IRQ。HAL前诊断不弹GRXSTSP、不读FIFO、不先清旧XFRC/EPDISD，不破坏HAL正常消费。
4. **回调时标记数据事实。** Data IN/OUT仍登记正常传输候选，不能直接发布取消。Stop后BSP PIO自动续包必须受准入封锁；Class回调过滤之前先保留旧所有权收尾信息。
5. **HAL后核验结果。** 在不重新arm的屏障内读取CTL、PIO状态及已捕获事件，作唯一终结决定。前后寄存器不是硬件原子快照；记录读取顺序，处理两次读取间新事件，不凭一次不同值推断所有先后。
6. **独立通知/可靠排队。** 仅当证明完整时产生带旧身份的cancel_complete；队列满保留记录/引用并锁定，不能丢证据后释放。

VERIFIED（S2:487/488、S4）：本地HAL使用精确位写入应答端点事件，是其W1C使用方式。拟议实现必须保留目标事件证据后再定向应答；不能用读改写或全1写法把新XFRC、STUP或其他端点事件一起抹除。硬件访问属性的手册交叉核验状态见下一节。

**arm/retire屏障**：原始硬件事件无软件Token。如果旧标志尚未处理就复用端点并更新映射，之后再把IRQ包装成“当前Token”仍然会错误匹配。新arm必须晚于旧目标停止、旧标志安全归属/应答、事件引用隔离和必要回调排空。USB IRQ短临界区只负责元数据，不在ISR里等待SD或长时间轮询硬件。

HAL USBRST在1332行以后会清端点旧标志并清DCFG.DAD；ENUMDNE才调用ResetCallback。若同时有RX/XFRC和Reset，HAL的既有处理顺序不能当作物理发生顺序。必须捕获Reset边界，不能等上层Reset回调后才为所有已发生事件重贴Session。

## 5. 共享RX FIFO与SDMMC边界

VERIFIED（S5）：USB控制器使用一个共享RX FIFO。目标OUT2取消不能授权丢弃EP0的新Setup或其他OUT的状态/数据。GRXSTSP是消费动作，RXFLVL只是非空指示；读完一个条目可能使NAK有效，但也可能先消费正常数据或Setup。

设计要求：取消前封锁目标新arm；全局NAK如被使用，必须记录其所有者与原状态，不能随意替别的流程清除。RX条目归属不明则保留受控故障，不靠flush或弹出“碰碰运气”。如何在不改ST HAL的情况下保留/路由已有RX条目是H-U2，未完成前不能批准生产实现。

USB PIO停止不证明SDMMC IDMA完成。后者仍按Worker实际quiescent/drained及独立缓冲引用证明处理；USB Reset不等于SD sync，RAW租约不转FatFS。

## 6. RM0399核验记录

选择核验对象为RM0399 Rev4；不混用检索中出现的其他修订版页码。官方索引确认覆盖STM32H747；在线可读取目录，但相关正文未成功取得：完整PDF在线读取受大小限制，官方直接下载超时/返回567。本轮不把目录或其他STM32系列手册当作本芯片正文。

| Rev4目录位置 | 寄存器/主题 | 目录核验 | 正文位/访问属性/时序核验 |
| --- | --- | --- | --- |
| 60.14.39，p2825 | DCFG | VERIFIED：官方目录 | UNRESOLVED；DAD位值由S1另行确认 |
| 60.14.40，p2827 | DCTL | VERIFIED：官方目录 | UNRESOLVED；NAK请求/状态位值由S1确认 |
| 60.14.42/43，p2830/2831 | DIEPMSK/DOEPMSK | VERIFIED：官方目录 | UNRESOLVED；EPDM由S1确认 |
| 60.14.44/45，p2832/2833 | DAINT/DAINTMSK | VERIFIED：官方目录 | UNRESOLVED；位映射由S1确认 |
| 60.14.54/55，p2840/2842 | DIEPCTLx/DIEPINTx | VERIFIED：官方目录 | UNRESOLVED；EPENA/EPDIS/XFRC/EPDISD由S1/S4确认 |
| 60.14.61/64，p2848/2851 | DOEPINTx/DOEPCTLx | VERIFIED：官方目录 | UNRESOLVED；位值及HAL应答方式由S1/S4确认 |
| 60.15.6，p2903起 | Device programming model | VERIFIED：官方目录 | UNRESOLVED：IN/OUT停用、Global NAK、FIFO和Reset完整编程序列 |

上述“源码确认”说明SDK定义和当前软件行为，有可复核依据；不等价于已经完成手册级硅行为保证核验。**RM正文的这组未解决项是后续生产实现门槛，本轮只完成核验记录。**

## 7. 既有实测与未解决清单

VERIFIED（E1/E2/E3）：RAM首次地址为21、config1；重连硬件地址0、软件地址仍21，OUT2控制寄存器`0xC00A0040`，USB IRQ101禁用。它证实硬件地址清零和端点/IRQ故障状态，不证明完整取消事件先后。旧环形记录被重复Stop覆盖，不能据此重建首次失败链。

| ID | 状态 | 待解决问题/解锁条件 |
| --- | --- | --- |
| H-U1 | UNRESOLVED | 获得RM0399对应正文，核实停用/清位保证和EP0例外 |
| H-U2 | UNRESOLVED | PIO Global OUT NAK与共享RX已有条目的无损处理，不能盲弹/flush |
| H-U3 | UNRESOLVED | HAL前后事件捕获与禁止重arm的完整屏障；原始IRQ没有Token |
| H-U4 | UNRESOLVED | Reset清旧标志时的独立硬件静止证明，不能假定Reset即全端点静止 |
| H-U5 | NOT_TESTED | 真正延迟EPDISD、XFRC竞争、停用确认丢失及迟到IRQ的板级验证 |

本轮没有新增实测，也没有改变任何寄存器或生产代码。后续不得用以上设计性要求替代实际硬件验证。
