> 维护说明（2026-11-08）：本目录中已确认不兼容的旧测试入口已按用户要求删除；其余公共桩、共享C夹具及历史报告保留。下文为历史说明，当前可运行集合以 Tests/SdmmcStage08B/run_regressions.py 为准。删除不代表旧失败被修复。

# UVC-09B：EP1 ISO IN异步调度验收

基线：`18f30d243f532ddca913297f92a90a1ecdc852c4`（UVC-09A）。分支：`codex/uvc-09b-stream`。main与PR #1保持不变。新PR以09A分支为base形成堆叠审核，不自动合并任何分支。

## 接口与使用边界

- `USB_UVC_SubmitFrame(frame, length)`：只由主循环调用。当前已声明的YUY2配置要求38400B；设备Configured、Commit有效、VS Alt1已打开且无关闭故障才接受。一帧在途或待处理时返回BUSY。不复制整帧，外部缓冲区必须保持有效且只读直到所有权归还。
- `USB_UVC_Process()`：主循环执行构包、提交、完成确认及取消恢复。通过CM7 main.c的USER CODE Includes与Main_Run接入，CubeMX生成不会删除该入口；不改变原Mode/Framework/App调用顺序。
- `USB_UVC_CancelFrame()`：请求取消，不在接口中伪造底层已停止。由Process确认Abort后归还Frame。
- `USB_UVC_GetStreamStatus(status)`：主循环查询六种状态、最近结果、偏移、Token、累计成功帧数、frame_owned。frame_owned=true时不得复用图像；Class释放成功后接口返回INVALID_STATE，此时本模块已归还帧。DeInit失败时Class不释放，frame_owned仍保持。

没有在正式固件生成图像，Tests中的模拟数组不加入Keil业务。无外部Submit时，Alt1下100次SOF也不发送数据。默认USB Mode仍是TEST，本轮不自动切换UVC，保留09C接入静态图像边界。

## 事件调用链

```
OTG_FS_IRQHandler
  → 原ST HAL_PCD_IRQHandler（SOF中断）
  → HAL_PCD_SOFCallback
  → BSP callbacks.sof(frame_number)
  → Platform Usb_Sof（同一硬件帧号去重、单调序号）
  → USBD_LL_SOF
  → UVC_Sof（仅覆盖本Class最新机会）

Main_Run → USB_UVC_Process
  → 原09A PayloadBuildPacket
  → USB_Device_Transmit(0x81, packet, length)
  → BSP_USB_Transmit → HAL_PCD_EP_Transmit

真实XFRC → HAL_PCD_DataInStageCallback
  → BSP确认当前事务有效、清PIO占用
  → Platform记录真实完成序号
  → USBD_LL_DataInStage → UVC_DataIn（仅保存匹配事件Token）
  → 下一轮Process核对Token → 原09A CompletePacket
```

启用PCD Sof_enable，原LL初始化会开启SOF中断。BSP不引入UVC头文件或视频逻辑。新测试通过未改动的ST HAL IRQ实现验证SOF全链路与Class回调实际被调用。

## 六态与发送节奏

| 状态 | 行为 |
|---|---|
| WAIT_FRAME | 无图像，不提交 |
| WAIT_SLOT | 有图像，等待最新SOF机会及帧间隔 |
| IN_FLIGHT | 提交成功，唯一EP1事务在途 |
| WAIT_CONFIRM | IRQ只记录真实完成，偏移未推进 |
| CANCELLING | 停止新提交，等待底层归还 |
| RECOVERY | 错误优先，保留引用并重试Abort/等待Stop恢复 |

SOF只保留最新序号，不累加发送额度；同一SOF最多一次Class提交尝试。BUSY保留已构造包及Token，等待新SOF重试，不推进偏移。帧间隔由现有Commit.dwFrameInterval/10000换算为毫秒，当前100ms；第二帧首包与第一帧首包相隔100个SOF，76包传输后等待余下间隔。没有在Payload模块内加入帧率或分辨率常量。

提交之前保存预期传输序号、Token和IN_FLIGHT状态，防止首个完成先到；只有USB提交SYS_OK表示已接受，Build成功不表示发送成功。仅在匹配真实完成且无取消/错误时调用CompletePacket。末包构造、提交和IRQ到达均不会提前翻FID，主循环确认才完成帧。

## IRQ与主循环串行

主循环API通过Platform USB_Device_Lock/Unlock只屏蔽并恢复USB IRQ，不屏蔽DCMI等其他外设中断。内部USB端点操作嵌套锁不会提前重新使能USB IRQ。ISR不能与主循环同时修改Payload。

DataIn/SOF/Incomplete新增回调均为常数时间事件记录，不构包、不提交、不执行Abort。构包最多复制510B，恢复在主循环执行。生命周期SET_INTERFACE/Reset/DeInit沿用既有同步Close路径，只有Close确认后才在该USB生命周期上下文取消并释放Payload；这是已串行的例外，不声称Payload所有操作都只发生在主循环。

未修改HAL/LL：HAL自身Incomplete处理及原EP0关闭中存在有限轮询，不能把整个HAL IRQ描述为完全无等待。本轮没有额外在SOF/DataIn/Incomplete回调增加这些轮询；实际中断延迟仍需实板测量。

## 所有权与Incomplete修复

原BSP Incomplete直接memset传输上下文，Platform直接清busy，不能证明硬件停止。本轮：

1. BSP锁存failed，继续保留source/active/PIO。后来的DataIn不能把失败事务转为成功。
2. Platform记录TIMEOUT，不清busy；Class进入RECOVERY。
3. Process请求09A Cancel，显式调用USB_Device_AbortEp。
4. HAL Abort确认端点停止、FIFO清理成功后，BSP应答本端点旧XFRC/EPDISD/TOC并清is_iso_incomplete，才归还软件所有权。
5. Abort失败：Payload、外部帧、Class上下文和Token保留；不调用CompletePacket，也不标帧成功。
6. 成功：用09A AbortPacket结束取消，下一帧使用正确FID。新ISO提交前再次应答本端点旧完成，不清其他端点状态。

HAL的IISOIXFR分支会尝试Abort，但返回值未必成功；即使稍后进入EPDISD回调，也不依赖该次尝试释放资源。本层必须重新确认Abort。PIO模式未启用USB DMA，软件引用与CPU FIFO续传状态仍须保留到确认。

BUSY不是成功完成，ISO Incomplete不是成功完成，清busy也不是停止证据。已成功取消传输但Close失败时，UVC-07关闭故障标志仍阻止新发送，直到成功重试Alt0或Stop恢复。设备级IO故障保留原Mode Manager策略，未擅自清掉既有致命错误状态。

## 防重复、迟到与重建

- BSP忽略没有活动事务的非EP0 DataIn；失败事务禁止继续分包或成功回调。
- Platform的IN提交序号与真实完成序号跨连接保持单调，Incomplete不产生成功序号，耗尽时拒绝复用。
- Class核对当前在途状态、Platform完成序号和保存的Payload Token；重复回调、旧软件事件不能推进新包。
- Payload对象在Service静态保留，Token跨固定池释放/重建也不回退；Class上下文本身仍来自原1024B固定池，没有标准malloc。
- Reset/Disconnect/DeInit：先停止新提交，再确认Close。失败不释放Class或图像。成功后清活动指针和事件。新流需要重新Commit/Alt1，旧Token不能命中新流。

HAL回调本身只有端点编号，无法凭Class猜出任意注入的裸HAL回调属于哪笔事务。真实硬件旧IRQ依靠同步停止和本端点W1C建立静默边界；旧软件通知依靠序号/Token拒绝。测试明确分别覆盖这两层，不把任意伪造的HAL完成调用当作硬件证明。

**最终结果：调度36/36、09A专项124/124、原USB461/461，共621次执行全部通过；CM7/CM4完整重编均0 Error、0 Warning。ARMCC实测Class上下文208B，低于1024B固定池。**

## 软件验收

- 新调度36项全部执行；详见results.json每项输入场景、断言、失败行、包序列及SOF提交计数。
- 38400B逐字节核对76包：75×512B+152B；两帧Header分别80...82与81...83。
- 每SOF最多一次提交、中断内零提交、无SOF不发送、SOF不累计额度。
- BUSY、HAL_ERROR、重复完成、错误Token、完成序号耗尽。
- Incomplete、Abort成功/失败、完成与Incomplete两种顺序、取消与完成交错。
- 在途Alt0、Reset/Disconnect/DeInit、旧Token/迟到Class通知、下一轮流重新初始化。
- 真正ST HAL IRQ处理SOF；W1C寄存器模型检查Abort失败不清旧XFRC，成功仅应答本端点并保留EP2事件。
- 默认TEST与Alt1无外部帧100SOF不产生数据，Frame接口非法参数。
- ARMCC实际Class上下文尺寸见regression_results.json，编译期也断言不超过1024B；Payload静态对象独立，不复制到固定池。

原09A专项62场景×O0/O2共124次通过；原USB461项全部保留并通过。回调表NULL预期按新增DataIn/SOF/Incomplete更新；原Core219/300/357将不安全的“立即清busy”改成“保留所有权直到Abort”，并增加Abort后释放检查，未删除编号或跳过测试。HAL桩新增fault=21用于EP1 HAL_ERROR，既有故障语义不变。

## 文件清单与理由

| 文件 | 修改原因 |
|---|---|
| CM7/BSP/bsp_usb.c/.h | SOF启用及通用转交；Incomplete保留所有权；端点局部旧中断清理 |
| CM7/Platform/usb_device.c/.h | SOF序号与Core分发；真实IN传输/完成序号；保留Incomplete busy；USB IRQ嵌套锁 |
| CM7/Service/usb_uvc_class.c、usb_uvc.h | 复用09A的调度、外部帧注入、事件回调、生命周期及状态查询 |
| CM7/Core/Src/main.c | USER CODE区域加入主循环Process入口，默认TEST直接无发送 |
| Tests/UvcStreaming/ | 独立专项、寄存器模型、命令、逐例结果、完整回归及编译记录 |
| 原UVC相关测试执行器及回调表断言 | 链接真实Payload实现、更新已实现回调绑定 |
| 原USB Core故障断言与HAL桩 | 加强Incomplete所有权检查，新增提交ERROR注入 |

不修改Payload算法、描述符、Probe/Commit字段及编解码/协商函数、HAL/LL源码、官方USB Core、Mode、Camera、DCMI、DMA、Media、CM4及Keil配置。原回归命令/结果JSON由实际重跑更新。

## 复现命令及编译

在独立工程根目录，使用既有ARMCC5 V5.06 update7 build960与Unicorn/pyelftools依赖：
```
python Tests/UvcStreaming/run_tests.py
python Tests/UvcPayload/run_tests.py
python Tests/UvcEp1Lifecycle/run_tests.py
python Tests/UvcProbeCommit/run_tests.py
python Tests/UvcProbeCommit/run_cancellation_tests.py
python Tests/UsbCoreDecoupling/run_tests.py
python Tests/Ep0IrqRace/run_race.py
python Tests/UvcLifecycle/run_tests.py
python Tests/UsbUvcDescriptors/run_tests.py
```

本套件使用真实BSP、Platform、Class、Payload、Core，硬件HAL端点操作为原有Mock；SOF专项另执行未修改的ST HAL IRQ/LL。W1C、硬件事件挂起为显式寄存器模型。不能据此宣称实板吞吐、丢包率、时钟精度、ISR时延或Windows视频显示已经验证。

两核用Keil -r完整重编；命令、Errors/Warnings和全部汇总见commands.json、cm7_keil.log、cm4_keil.log、regression_results.json。没有OV5640/DCMI/Media接入，没有正式假图像或实板操作。完成后只推送本分支并创建审核PR，不合并main或PR #1。
