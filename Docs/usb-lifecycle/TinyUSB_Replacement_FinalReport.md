# TinyUSB Replacement Final Report — 部分实验交付 / BLOCKED

## 结论与边界

**当前固定版本可用于继续替换研究，但尚不能安全替代本工程USB生产路径，P1不具备实板验收条件。** 已停止在旧UDLM上叠加功能。没有复制StopAttempt、ParticipantRegistry、旧EP0或旧BOT状态机。已独立运行真实TinyUSB USBD/MSC和真实旧RAM Block Backend；DWC2反例表明新驱动仍可能无界等待，不能声称解决Windows重连问题。

VERIFIED：研究起点 `81e872f834efedc9f064dc5f45ec73d73350b8cc`；独立分支 `codex/usb-tinyusb-p1-replacement`。原P1分支和全部历史未改写，原工作区未操作。最终提交号和远端实际核对结果见最终答复及ignored交付回执；不将本报告起点误称为最终SHA。

## 固定上游与许可证

VERIFIED：TinyUSB `da5b55a5803a3b9ca788ccc82569db699c01aa74`；GitHub API所查最新HEAD也为同一SHA。没有混合不同版本。codeload压缩包SHA256为 `40cefc21be15af18f0b9cc63c0d8f0a08c5b265ab8966a7bf271246be318c792`。选入52个文件，逐个pristine字节按Git blob SHA1与该commit tree核对；完整来源及SHA256保留于ThirdParty/tinyusb/UPSTREAM.json和Tests/TinyUSBReplacement/reports/upstream_audit.json。MIT许可证及文件头保留。

VERIFIED：OpenMV参照 `da8dba466eb131691da776733e67b29021b12da6`，其实际MicroPython子模块为 `57a7b25003de432e3910707fe7df51dd4c1b1326`，TinyUSB子模块为 `aa0fc2e08f1c2dd6f026a431e8989357fbb4c5bf`。另读MicroPython仓库HEAD `0fd6c573ea815774668bbb16b8e197c8822368b2`，不冒充OpenMV固定子模块。上游只供对照，没有将其不同TinyUSB版本混入vendored代码。

## OpenMV集成方式对照

| 事项 | 可核对来源 | 本实验采用/限制 |
| --- | --- | --- |
| 单控制器单栈 | MicroPython固定子模块ports/stm32/usbd_conf.c:352–369，用TinyUSB/ST分支区分IRQ | 新ARM测试镜像不含ST Core或HAL PCD，真实生产工程尚未切换 |
| GPIO/PHY/时钟分层 | 同文件:92–204保留STM32引脚配置；TinyUSB H7 family.c:88路由rhport0 | 需从现bsp_usb提取纯板级初始化；不能调用HAL_PCD_Init后再启动DCD |
| Task事件 | shared/tinyusb/mp_usbd.c、OpenMV protocol/omv_protocol_channel_tinyusb.c:26 | 借鉴原生DCD队列；当前模型OS_NONE，尚未接入FreeRTOS UsbTask |
| 构建选择 | OpenMV ports/stm32/port_config.mk:84；MicroPython Makefile TinyUSB源列表 | 保留配置选择思想，不复制其链接器、Camera或Python运行时 |
| 启动器与应用分开 | OpenMV boot/src/ports/stm32/tusb_config.h:36/56是OS_NONE/DFU | 启动器使用TinyUSB不构成H747 FreeRTOS MSC/UVC已验收证据 |

INFERRED：继续移植时仅让原UsbTask调用tud_task_ext，事件hook唤醒原任务，避免重复调度器；TinyUSB队列与任务执行须和板级中断优先级配套。没有实现新的Controller Manager。

来源：[OpenMV固定源码](https://github.com/openmv/openmv/tree/da8dba466eb131691da776733e67b29021b12da6)、[实际MicroPython子模块](https://github.com/openmv/micropython/tree/57a7b25003de432e3910707fe7df51dd4c1b1326)。本地只读副本与哈希在output/TinyUSBReplacement/upstream及upstream_audit.json。

## H747兼容性核对

| 项目 | 结论 | 依据/剩余项 |
| --- | --- | --- |
| CM7 / STM32H747XI | VERIFIED源码 | MDK-ARM/h7_mac.uvprojx:3098/3102，器件写作STM32H747XIHx；完整封装丝印本轮未读，H6沿用用户信息 |
| USB2_OTG_FS / internal PHY | VERIFIED源码 | CM7/BSP/bsp_usb.c:1033–1042；dwc2_stm32.h:40–45/:145控制器表以rhport0映射FS |
| PIO / Slave | VERIFIED配置 | CFG_TUD_DWC2_DMA_ENABLE=0，SLAVE_ENABLE=1；不启用USB DMA |
| PA11/12 AF10、48MHz源 | VERIFIED源码 | bsp_usb.c:1595–1625、:1894，保留HSI48/CRS及系统时钟；未实际运行新板级适配 |
| IRQ优先级 | VERIFIED源码 / NOT_TESTED执行 | USB_IRQ_PRIORITY=6，FreeRTOSConfig.h最大可调用API优先级5，数值关系允许FromISR；本轮无实板中断 |
| ARMCC5 | PARTIAL | --gnu使上游属性兼容；UVC一行ABI修正后组件可编译，但FreeRTOS门禁仍在 |
| FreeRTOS | BLOCKED | 当前configUSE_MUTEXES=0；TinyUSB OSAL需要xSemaphoreCreateMutexStatic，且旧内核无pdTICKS_TO_MS。未伪造函数或抑制警告 |
| FIFO/RAM | PARTIAL | H7表9双向端点、1024 words，DCD自身分配；未在板上核对GHWCFG/FIFO布局，不沿用HAL配置同时写入 |
| Cache | VERIFIED配置 / NOT_TESTED实物 | PIO不让USB DMA直接访问适配缓冲；不改变SDMMC专用非缓存区和MPU；CPU跨Frame/Worker所有权仍需接通 |

VERIFIED：新工程适配代码共94行（config25、RAM回调/查询、描述符及头文件实际总和，以文件行计含注释/空行），只新增CM7/TinyUSB，不修改旧USB生产文件、FreeRTOS、Camera、SDMMC或链接布局。RAM复用usb_msc_ram.c原16×512字节，不创建第二个BOT或SCSI处理器。

## DWC2硬件停止审计与反例

所有文件路径以下相对ThirdParty/tinyusb/，对应固定SHA。

| 路径 | VERIFIED源码事实 | 实验/影响 |
| --- | --- | --- |
| src/portable/synopsys/dwc2/dcd_dwc2.c:462–522 | edpt_disable为void；IN等待INEPNE、EPDISD均无超时；OUT等Global NAK和EPDISD亦无超时 | 实际ARM分别抑制四种完成条件，100000指令后仍循环且未返回；是缺失条件反例，不是H747必现概率 |
| 同文件:500–514 | OUT停止同步占Global NAK，等待完成后CGONAK；不使用对应中断事件 | 不能把控制位写入当退休；新增Task队列不能解除这个同步循环 |
| 同文件:495–498 | EP0 OUT特殊处理保留SETUP接收，不通用EPDIS | 相比旧OUT0 Abort有改进；不等于所有旧EP0数据/状态已安全退休 |
| 同文件:1168–1176 | PIO SETUP_DONE中对活动EP0 IN同步edpt_disable(is_isr=true) | 可在ISR进入同一无界轮询，不能称新栈已完全Task化 |
| src/device/usbd.c:469/1902 | DCD无abort_xfer时回退STALL/Clear Stall | 本DWC2没有独立abort实现；真实MSC Reset可进入上述停止路径。RAM模型case12验证确实调用fallback，不用成功abort桩掩盖 |
| DCD:839–867 | close_all先屏蔽非控制端点、发disable，之后flush全部TX/RX并重建分配 | 未给出逐端点失败返回和共享RX失败保留证明；不能直接当新P1安全停止 |
| DCD:875–879 | ISO activate先edpt_disable | ISO重启亦可能进入无界等待；非只MSC问题 |
| src/portable/synopsys/dwc2/dwc2_common.h:84–93 | FIFO Flush无超时、void | TX Flush故障注入不返回，不以HAL_OK等替代证据 |
| dwc2_common.c:18–38/:228–244；DCD:680 | deinit core reset等AHBIDL/CSRST无界，DCD最终返回true | AHBIDL缺失模型不返回；全Controller Reset不等于底层Storage停止 |
| DCD:525–579 | PIO ZLP排程同一次写clear_nak/enable，无合格AHB延时间隔 | 实际MMIO记录0x84000000同时CNAK/EPENA，ES0445 §2.24.5门禁仍未闭合 |

VERIFIED：8项DCD观察中6项是未返回的故障见证，2项是EP0 OUT特殊路径和ZLP写序观察；全部预期成立不等于8项安全停止通过。详见reports/dwc2.json，包含PC、寄存器读次数/写序和真实源码哈希。测试不会访问板卡，CPU执行预算只是测试防挂，不是固件超时。

VERIFIED：本轮查询上游30条DWC2文件历史、当前完整HEAD、Issue #3689及评论，并检索相关PR/Issue；所查HEAD未包含使上述等待有界且可传播失败的修复。#3689报告的是ESP32-S2，不能直接认定为H747实板复现，也不能证明所有历史版本都无解决方案。没有为消除报告而退回缺少pending_io修复的旧版。

来源：[固定DWC2源码](https://github.com/hathach/tinyusb/blob/da5b55a5803a3b9ca788ccc82569db699c01aa74/src/portable/synopsys/dwc2/dcd_dwc2.c)、[上游问题与维护者讨论](https://github.com/hathach/tinyusb/issues/3689)。

## 勘误边界

VERIFIED：官方ES0445 Rev7，U/V组REV_ID=0x2003。沿用已有IDCODE=0x20036450，不擅自区分U/V、不声称本轮重新读取。§2.24.5 p44适用于Device Slave ZLP；规定的分步NAK/Enable和AHB间隔未在当前TinyUSB排程中得到证明。§2.24.2为Buffer DMA限制，当前PIO不能直接套用其影响或修复。无实物AHB测量。

来源：[ST ES0445 Rev7](https://www.st.com/resource/en/errata_sheet/es0445-stm32h745xig-stm32h755xi-stm32h747xig-stm32h757xi-device-errata-stmicroelectronics.pdf)。RM0399 Rev5只读PDF保留未跟踪，未修改。换栈不会改变芯片勘误适用性。

## RAM MSC真实源码实验

VERIFIED（ARM软件）：完整链接TinyUSB src/tusb.c、common/tusb_fifo.c、device/usbd.c、class/msc/msc_device.c及原RAM Backend、薄回调；不链接旧BOT/SCSI .c、ST Core、HAL PCD。测试DCD显式模拟主机提交/完成，返回false的abort触发真实上游fallback；不模拟CBW→CSW状态机。

12项、1679断言通过：描述符/SET_ADDRESS/GET_MAX_LUN；INQUIRY；1536字节READ分1024+512；仅RAM写读回；未支持0x7f→12字节CDB REQUEST SENSE；BOT Reset/Bus Reset/重复Reset/Clear HALT期间pending_io保留；旧IO完成丢弃后新READ CAPACITY；已排空后的逻辑DeInit/Init；SETUP软件抢占；活动Data IN的Reset fallback。

VERIFIED：TinyUSB Sense首字节为0xf0（valid位1、response code0x70），本轮新夹具初次错误地要求整字节0x70而失败，已按真实上游格式检查低7位，保留原失败日志。没有改动历史旧栈断言。异步完成由实际RamBackend poll返还后通知tud_msc_async_io_done；重复Poll不会再送完成。Reset保留pending_io是上游提供的资源优势。

UNRESOLVED：tud_msc_async_io_done没有应用事务参数，本薄层单请求/单任务/单次完成负责限制通知；队列满、pending IO期间deinit/reinit、旧DCD XFRC跨新Transfer、非法Offset全部边界尚未充分验收。测试中Stop/Restart只在RamDrained后执行，不能把公开tud_deinit本身当Worker排空。当前适配**没有接入生产Task，也不具有硬件上线资格**。

VERIFIED：ELF中实际存在TinyUSB mscd_xfer_cb/usbd_edpt_abort/tud_rhport_init，旧HAL_PCD_IRQHandler、USBD_LL_Reset、USB_MSC_BotProcess、USB_UVC_Process均不存在。s_ram=8192B、MSC epbuf=1024B、class state=72B、USBD state=80B。它们仅是OS_NONE测试镜像局部开销，不是FreeRTOS完整固件RAM预算。详见elf_evidence.json及各MAP。

## SD只读与UVC状态

**SD：BLOCKED / 未接入**。任务要求RAM合格后再接Worker；RAM的真实DCD停止/重启尚未合格。未修改Storage Worker/SD Block/FatFS，未Claim或写真实SD，也未用内存块模拟SD成功。未来只把Read提交/完成桥接pending_io；取消必须等真实Worker静止，RAW不自动转给FatFS。

**UVC：PARTIAL审计，非工作视频移植**。实际video_device.c明确要求线性帧（:100），当前Bridge可能返回两段view（app_uvc_bridge.c:349–351）。保持160×120 YUY2/10FPS意味着38400B/帧、384000B/s有效数据；512字节ISO包理论有效余量足够不等于FS实际稳定性证明。没有改相机来迎合USB，也未宣称完成新描述符/Probe/Commit/Alt调度。

VERIFIED：真实Video源码3项状态输入见证表明：videod_reset清frame但没有callback；videod_deinit既不归还通知也不清frame；显式_finish_frame(aborted)对照会通知abort。它们是预置“已经持有帧”的输入，不是成功frame_xfer或ISO硬件证明。用例同时验证Probe/Commit sizeof48、format offset2、layout offset40。原包布局在ARMCC5为47字节；一行将bmHint显式改为uint16_t后满足协议布局，未删静态断言。

INFERRED：TinyUSB把源Frame复制到固定USB payload缓冲，DCD PIO使用后者；Camera源帧和USB payload两类所有权应区分。只在同一USB Task确认类已不再取源数据后，最小Frame适配才能归还Camera；不能以IRQ取消或callback缺失猜测源引用结束，也不能提前复用仍被DCD访问的payload。当前未实现该边界；保持Frame引用/BLOCKED，不能挂上Camera即试运行。

来源：[TinyUSB Video官方文档](https://docs.tinyusb.org/en/latest/reference/class/video.html)、固定video_device.c:215–236/:924/:1513。固定源码优先于滚动文档。

## 上游偏离和最小修复边界

VERIFIED：**DCD改动为0**。仅class/video/video.h的bmHint一行改为16位，实际Diff保留reports/vendor_compatibility.patch。不是关闭断言或全局pack；未改协议状态机。其余51个文件的原始Git blob核验留档。

BLOCKED：不能只把edpt_disable/Flush里的while换成超时并在void中return：上层STALL/close_all/ISO/class逻辑仍可能继续释放或重用旧Buffer。建议的后续最小上游修复范围是“DWC2真实失败结果＋DCD调用者失败传播”，而非旧UDLM搬迁；需先明确可传播失败的接口和有界时基，再处理共享RX和ZLP。详细具体边界见TinyUSB_Replacement_Blockers.md。没有在本轮偷偷改TinyUSB Core或重造另一个Manager。

## 三配置构建状态（不得混淆）

| 配置 | 实际执行 | 结果 | 完整固件 |
| --- | --- | --- | --- |
| RAM MSC | ARMCC5/FreeRTOS 6个组件编译；OS_NONE真实Core/MSC测试镜像链接 | 组件0Error但存在OSAL告警；测试12/12 | BLOCKED / NOT_BUILT |
| SD Readonly MSC | 同固定上游组件编译，未接SD Adapter | 组件0Error但存在OSAL告警；非真实SD验证 | BLOCKED / NOT_BUILT |
| UVC | Video等6个组件编译；真实Video状态/ABI测试 | ABI修正后组件0Error；OSAL告警仍在 | BLOCKED / NOT_BUILT |

VERIFIED：共18个组件对象0编译错误、**57个警告**；3个enum范围警告及54个旧FreeRTOS接口相关警告。compile_probe.py因此返回失败；没有过滤告警、伪造0Warning或把objects当三份可烧录AXF。完整FreeRTOS链接和三配置生产调用链未完成；原Keil工程完全未改。

VERIFIED：新测试镜像内没有双栈所有者；当前生产工程仍保留旧路径，不能称TinyUSB已接管真实OTG_FS。真正切换必须同时排除旧bsp_usb/usb_device/ST Core/HAL PCD IRQ/旧Class/BOT/Payload，并实现纯板级Init及唯一IRQ→tusb_int_handler；在本轮门禁未关闭前不启用该构建，不复制部分新旧驱动共跑。

## 原P1阻塞解决程度与下一步审核

| 原问题 | TinyUSB当前状态 | 结论 |
| --- | --- | --- |
| 自研EP0/CBW/BOT状态机复杂 | 原生Core/MSC、队列及pending_io可复用，RAM真实源测试通过 | VERIFIED软件收益 |
| OUT2取消挂起 | 更换为DWC2同步Global NAK/EPDISD无界等待 | BLOCKED，不能宣布消失 |
| Class/Stop失败释放 | void DCD停止结果缺少安全传播 | BLOCKED |
| EP0 OUT错误通用EPDIS | upstream有专用OUT0路径 | VERIFIED局部改善 |
| PIO ZLP勘误 | 仍合并CNAK/EPENA，未证明AHB时序 | BLOCKED |
| UVC Frame归还 | reset/alt可能无callback，源线性约束与Bridge双span不同 | UNRESOLVED |
| Windows重连 | 本轮无真实设备/USBPcap | NOT_TESTED |

INFERRED：值得保留TinyUSB作为协议栈替换候选；不值得把当前未经修复DWC2直接当“成熟即安全”上线。集中审核需决定最小DCD失败传播和PIO勘误补丁范围、现有FreeRTOS mutex兼容方向及Frame最小边界。不能回头继续修补旧UDLM，也不擅自更换其他栈。

NOT_TESTED：实板枚举/读SD/视频/插拔/Reset/Windows重连全部；本轮无烧录、无物理控制、无真实SD写入、不合并main。已完成可独立的软件实验，按关键门禁停止等待集中审核。

## 复现与证据

Tests/TinyUSBReplacement/README.md给出命令。reports包含stack/dwc2/video/compile/upstream_audit/elf_evidence和早期失败。output/TinyUSBReplacement保留所有下载、对象、测试AXF/MAP及原始副本，已专门忽略，不纳入Git。原P1失败和output不删除。新分支普通提交推送并核对SHA；若网络失败仅保留本地提交，如实报告。
