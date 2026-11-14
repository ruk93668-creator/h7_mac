# C2.2A-R2 STM32H747 实板验收方案

文档准备日期：2026-11-12。代码基线：`d0bebf5663796bfa2f3425b52cfda22fd99d511d`。
分支：`codex/stage-C2-2A-R2-stop-ownership`，PR #7 仍为 Draft、未合并。

**这是待执行方案，不是实板通过报告。** 本轮只阅读源码、新增本文档；不重编、不运行软件专项、不写入或清理output、不改生产代码和原断言，不进入C2.2B。

## 本轮入口补充（2026-11-14）

现已提供独立Watch验收构建，详见 [KeilWatchUsage.md](KeilWatchUsage.md)。原正式固件仍无此入口；只有烧录独立验收AXF后才能通过g_r2_hw投递。本方案的源码行号固定于原d0bebf5，新增Control测试条件调用会使当前Control行号移动，按函数名定位。BASEPRI修复已包含；旧固件哈希不能用于新验收镜像，实际哈希见watch_build_results.json。

## 1. 开始前必须确认的限制

1. 当前正式代码没有 START/STOP 故障注入开关。H1/H2 使用调试器一次性修改函数返回后的局部 `result`，只验证错误分支、停止协调和去重，不能证明真实硬件失败后的恢复能力。硬件自然故障另行记录，不人为短接、断传感器供电或改时钟制造故障。
2. 全部CM7源码搜索中，`ControlTask_RequestStart/Stop`、Storage四个请求接口只有定义，没有按键/串口等生产调用点；也没有可写 `g_storage_debug.request` 入口。**正式固件仍缺少手动触发入口；独立Watch镜像已按KeilWatchUsage提供。**
3. 应由现有合法任务侧控制途径调用公共接口。若使用调试器目标函数调用，先确认该Keil版本确实支持、运行的是目标函数、没有切换所有者上下文，并检查返回值。本文未验证Keil支持这种调用，不能直接把 `ControlTask_RequestStop()` 当作已经验证可执行的Command命令。
4. `ControlTask_Request*` 要求调用者为已登记业务任务，消息接口还检查ISR和PRIMASK等条件；Storage请求要求Thread模式且PRIMASK/BASEPRI为0。调用返回SYS_OK仅表示入队，不是停止完成。
5. 没有可用合法控制入口时，H3/H4以及H5的SD子项填写**BLOCKED：缺少触发入口**。不要写Task句柄、控制状态、队列内存、引用计数来绕过。需要另行批准受控独立验收适配；本轮不增加它。
6. 关闭播放器、USB Reset或拔线，不等于显式业务STOP。不能用它们代替H3/H4。
7. 两项旧B1精确轨迹检查ID9/ID10仍失败：它们要求原先的同轮重复推进；本方案核对授权的新去重行为，不宣称原检查已PASS。

## 2. 固件、环境与记录规则

### 2.1 固件核对

仅使用当前已有镜像，不在本轮重新构建或覆盖输出：

| 核 | 现有AXF | 本轮读取到的SHA256 |
| --- | --- | --- |
| CM7 | `D:/32/STM32H7/h7_mac/MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf` | `af62f94c74409c3a6e58d4880024ca1e2af15c8ee74493a4808f775e8ca59c08` |
| CM4 | `D:/32/STM32H7/h7_mac/MDK-ARM/h7_mac_CM4/h7_mac_CM4.axf` | `6b45de12f9e420ffad973522f4909d40fdc4cf5073a7d6c7a2fa5f9a74dcbece` |

AXF哈希标识当前磁盘文件；并不等于已经核验板上Flash。Keil加载符号必须与实际烧录镜像一致，完成Download/Verify后记录实烧版本。之前R2构建报告的生产源码哈希与当前源码一致；本轮没有独立重编证明。

当前工程CM7宏为 `APP_VIDEO_MJPEG_MODE=1`，配置为320×240 JPEG、输出目标25FPS、最大40000B；Storage选择第三MBR分区。实际相机、USB、录像帧率分别测量，不能把25写成实测。CM7原来不生成HEX，直接使用对应AXF；不改Keil输出配置。

板卡、CM7/CM4配套固件、USB线、主机、播放器命令、SD卡CID/分区和场景均记录。SD写入子项仅使用已确认可写卷，不格式化、不覆盖已有文件。

### 2.2 调试与无断点运行分开

- H1/H2和停止顺序检查可以暂停；H5连续运行必须关闭断点，避免调试器停核引发DMA/USB时序异常。
- 不在BSP临界区长时间单步；SysTick、RTOS调度、USB主机时间与外设DMA不一定随CPU一起暂停。断点实验不能用来测正常FPS或恢复耗时。
- 静态变量cannot evaluate时，在对应源文件/函数断点选择正确栈帧，查看Map/Locals；不复用历史绝对地址。符号仍不可见则标记该证据未取得，不能猜值。
- 不在Watch调用有副作用的函数；只读Getter也不要反复作为实时求值表达式执行。观察其已有调用返回处局部副本或权威字段。
- 记录本轮 `r = g_camera_task_debug.wake_count` 与 `n = s_stop_attempt_sequence`。同轮范围从 `App_CameraBeginRound()` 到Task的FinishRound/Reply收尾；下一轮是新的BeginRound。序号差按无符号32位计算。
- 单独数实际 `BSP_DCMI_Stop()` 入口命中次数。只读查询、DRAINING等待、Media Stop函数入口都不等于一次硬件停止。
- 每个故障注入用例重新完整断电启动，避免上一例的Control ERROR、FAULT或USB主机超时污染结果。记录暂停造成的额外FE，不能算作自然故障根因证据。

## 3. 四类完成条件必须分栏

| 条件 | 观察与证据 | 不能代替它的值 |
| --- | --- | --- |
| H：采集硬件停止 | `DMA1_Stream0->CR & 1 == 0`、`DCMI->CR & 1 == 0`、`(DCMI->CR >> 14) & 1 == 0`；记录BSP真实返回及本次停止序号 | HAL状态单值、任务state、NDTR不动 |
| M：Media完全停止 | `s_capture.initialized=true`、`mode=NONE`、`state=IDLE`，对应原IsStopped=true | CameraCapture_Stop返回0、DRAINING、只有H成立 |
| B：帧交接全部完成 | USB侧Bridge IDLE且无frame_owned；Camera侧held/offer_pending/ack_pending/request_pending均false，release_pending也无悬挂；Storage队列/槽位/SD loan另行核对 | Camera侧Bridge Idle单独为true、收发帧计数相等、Storage IDLE |
| D：Device停止完成 | Media完全停止以后执行Device_Stop/Ov5640_Stop，回调成功，Device从RUNNING经BUSY到READY；若原已READY不重复回调 | App状态0、H成立、文件关闭 |

H只表示硬件静止；BSP_DCMI_Stop还包含HAL恢复，可能H已经成立但BSP返回失败。M可在DRAINING期间不成立。Device READY是软件生命周期确认；如需独立证明传感器实际进入待机，另采SCCB写入/传感器信号证据，不能只凭枚举证明电气状态。

参考状态值：Media IDLE=0、CAPTURING=1、READY=2、DRAINING=3、ERROR=4；模式NONE=0、SNAPSHOT=1、STREAM=2。Device READY=1、RUNNING=2、BUSY=3、ERROR=4。优先显示枚举名称。

停止事实：`s_stop_attempt_sequence`是实际BSP调用序号，`s_stop_hardware_result`是最近一次实际结果；`s_stop_attempt_kind`区分NORMAL_ATTEMPT=16、PROTECTIVE_ATTEMPT=32。Getter还组合INITIALIZED=1、ERROR=2、DRAINING=4、COMPLETE=8。历史结果只有新实际尝试才能更新；新尝试成功不抹除Media原last_result或原请求失败事实。

## 4. 通用观察清单

| 层/位置 | 观察内容 |
| --- | --- |
| `app_camera.c` | s_camera.running/prepared/stopping、round_open/round_stop_attempted/round_stop_result/round_media_sequence、device->state、ack/ack_pending/stop_command、fault/last_result |
| `camera_capture.c` | s_capture.initialized/mode/state/last_result、三个s_stop_*字段；已有GetStopFacts调用处的facts |
| `bsp_dcmi.c` | BSP_DCMI_Stop实际返回；s_dcmi.ErrorCode、s_dcmi_dma.ErrorCode；首次Dcmi_RecordError调用栈与原始DMA/DCMI标志（若发生） |
| `app_uvc_bridge.c` | s_camera_bridge的持有/待发/待归还状态，s_usb_bridge.state；g_bridge_debug acquired/submitted/released/retries/result作辅助 |
| `camera_mjpeg_fifo.c` | s_fifo.loans每项usb_owned/sd_owned；不可用HasLoan=false独自证明消息已经排空 |
| `app_storage.c` | g_storage_status（当前名称，不是旧g_storage_debug）；s_slots状态，s_count，s_accepting，s_stop_pending，s_mounted/s_file；权威槽位不允许手改 |
| 全局诊断 | g_camera_task_debug、g_usb_task_debug、g_control_task_debug、g_storage_status、g_ipc_task_debug；诊断不用于业务释放判据 |

ACK记录分三步：构造并pending → App_MessagesSend返回SYS_OK后清pending → Control收到匹配source/command_id/result。清pending只证明入队。不能漏掉后一步。

## 5. H1：START失败，回退停止成功（对应旧ID9）

### 前置条件

- 新的完整冷启动，无USB/SD旧租约；Device应由本轮从READY启动，`sensor_started=true`。
- 禁止Storage写入。先完成一次不注入的冷启动，确认真实硬件路径可启动。
- 本例是**真实板上API返回值注入**，不是自然启动故障。

### 操作步骤

1. 设置一次性断点：JPEG模式在 `CameraJpegStream_Start` 第1411行，即CameraMjpegFifo_Start已返回、但尚未保存last_result；YUY2对应第508行（BSP StartContinuous返回以后、进入错误判断前）。
2. 冷启动运行到断点。确认真实启动返回SYS_OK、当前位于CameraTask、mode=STREAM、Device=RUNNING。若真实已经失败，保存原现场，不再覆盖结果，另记为自然故障。
3. 仅把当前栈帧局部 `result` 改为 `SYS_ERR_IO=-9 (0xFFFFFFF7)`，并读回确认。不要只在App_CameraStart第268行以后改返回值，那不会使Media保存真实错误分支；不要改s_capture.state。
4. 禁用这个一次性注入断点，继续到 `App_CameraStart` 第273行，确认result=-9、sensor_started=true、Media ERROR；记录r、n0。
5. 跟踪AdvanceStop → CameraCapture_Stop → BSP_DCMI_Stop真实执行，不修改停止返回值。到Media第773行后记录n1和hardware_result；到Device_Stop回调返回记录结果。
6. 到CameraTask第135行确认BridgeCameraProcess仍被执行。首启无租约时只证明推进调用没有被跳过，不冒充“处理过真实Return”。
7. 到FinishRound返回、Task第137行记录r、n2、H/M/B/D、START ACK。随后继续一个新轮次观察，不将Control后续因FAULT发出的新STOP当成同一轮重复调用。

### 观察位置与预期结果

- Media启动错误已保存last_result=-9；成功停止后该历史错误不变为0。
- 本轮真实BSP Stop成功，n1-n0=1，n2=n1；DMA EN、CAPTURE、DCMI ENABLE均0。
- 无租约时Media NONE/IDLE；Device_Stop在M成立后成功，Device READY。
- `round_stop_attempted=true`、`round_stop_result=0`；FinishRound复用事实，不再重复下发硬件停止。
- START ACK仍为启动失败-9，不因回退成功变为0；FAULT可独立交付。

### 失败判据

本轮BSP入口命中超过一次；Device先于M停止；Bridge推进被跳过；START错误被清零；有外部引用却伪造M/B完成；不能确认注入点/局部变量时不得计PASS。

### 源码调用链

CameraTask_Process → BeginRound → HandleCommand(START) → StartResource → CameraInitStream → App_CameraStart → CameraCapture_StartStream → CameraJpegStream_Start → 错误返回 → App_CameraAdvanceStop → Media Stop → BSP Stop → IsStopped → Device Stop → Process → BridgeCameraProcess → FinishRound → Reply。

### 测试记录表

| 实烧版本/配置 | 注入点/读回result | r/n0/n1/n2 | H | M | B | D/Device返回 | START ACK身份/结果/Control收到 | 结论/证据 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 未执行 |

## 6. H2：START失败，回退停止返回失败（对应旧ID10）

### 前置条件

H1准备通过或已取得不注入的硬件基线；全新冷启动，无外部租约；能准确定位局部result。若变量被优化而无法确定，标BLOCKED，不改编译配置。

### 操作步骤

1. 按H1在Media启动返回处一次性注入-9，确认Media进入ERROR、sensor_started=true。
2. 增设断点在CameraCapture_Stop第770行：此时本次BSP_DCMI_Stop已经真实返回，尚未完成事实记录。记录**未经覆盖的实际BSP结果和三个硬件位**。
3. 仅第一次把该栈帧 `result` 改为-9，然后立即禁用此注入断点，让事实记录保存本次模拟API失败。若实际BSP原本成功，应在表中明确“硬件已停止、仅API结果模拟失败”，不得称真实BSP故障。
4. 继续至App_CameraRun第186—190行及Task第135行：Media ERROR必须使本轮跳过会再次停止的Media Process，但Bridge仍执行。
5. 在本轮FinishRound收尾记录n1。它不能再次进入BSP/HAL；保留ERROR、round_stop_result=-9，Device仍RUNNING，不得提前Device_Stop。
6. 继续到下一个BeginRound，记录新的r。到CameraCapture_Process第535行，应允许新的保护性停止尝试；本次不再注入。
7. 记录新BSP真实返回、n2。若成功且无租约，M成立后Device才能进入READY；同轮FinishRound不再重复实际BSP停止。
8. 保存原START失败ACK、原Media错误和Control状态。Control进入ERROR与Camera后台安全排空是两件事；后台排空成功不等于系统自动重新START。

### 观察位置与预期结果

首轮n1-n0=1、Media ERROR、Device RUNNING；首轮FinishRound不新增BSP尝试。下一轮真实恢复允许n2=n1+1；原last_result及原失败ACK不能被成功覆盖。无永久故障且BSP恢复成功时，不应无理由永久卡ERROR。

### 失败判据与恢复边界

- 同轮重复BSP/HAL停止、错误被清零、没有M证明就停止Device，均失败。
- 注入已撤销且观察到至少3个新Camera轮次，BSP明确能够成功、无残留租约，却完全没有重试或无法离开ERROR，记录缺陷。
- 若BSP持续真实失败、存在未归还租约或Device_Stop进入ERROR，不能要求本轮自动恢复成功：它可能持续ERROR/DRAINING；保存原因，不强清状态、不改成READY、不强制回收。
- 返回值注入时BSP可能早已关闭硬件，因此本例**不能覆盖“硬件确实关不掉”或HAL确实恢复失败**。此部分仍待自然故障或另行授权硬件故障试验。

### 源码调用链

首轮同H1至AdvanceStop失败 → App_CameraRun跳过ERROR重试 → Bridge推进 → FinishRound读本轮失败；下一轮BeginRound → Media Process ERROR分支 → Media Stop → BSP Stop → Bridge推进 → FinishRound依据最新事实完成Media/Device协调。

### 测试记录表

| 配置/首轮r | 启动注入 | 停止原始返回/注入值 | 原始H/是否仅模拟API错误 | n0/首轮末n1/次轮n2 | 首轮M/B/D | 次轮M/B/D | 保留的错误/ACK/Control状态 | 结论 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 未执行 |

## 7. H3A：正常STOP（USB视频，Storage OFF）

### 前置条件

合法手动控制入口已确认；Camera和USB正常运行，主机正在收流；Storage OFF且没有SD租约。没有入口则BLOCKED，不能用关闭播放器代替。

### 操作步骤

1. 无注入运行至USB确有在途借帧，记录Bridge双方状态、frame_owned、epoch/lease及完成帧数。
2. 由合法业务上下文调用原 `ControlTask_RequestStop()`，记录入队返回。保留旧Control先USB再Camera的顺序，不直接从USB/Storage调用Media或Device。
3. 观察UsbTask：CancelFrame → UVC Process → BridgeUsbProcess(false) → 再Process → UsbFinishRound。Abort未完成仍须持有原帧。
4. 在CameraTask的Bridge路径观察Media Release、Return ACK入队；再确认USB消费Return ACK回到IDLE，USB Mode停止及USB STOP ACK被Control处理。
5. Control随后发Camera STOP。记录Media Stop/BSP Stop返回及H、M；M成立后Device Stop，再检查Camera侧Bridge Idle。
6. 观察Camera STOP ACK：source=CAMERA、command_id匹配、result=0；确认Control实际处理后进入IDLE。分别填写H/M/B/D，不仅看OLED CAP STOP。

### 观察位置与预期结果

USB帧只能由CameraTask实际归还；frame_owned为true时不强制释放。M前不调用Device Stop；Bridge尚有待发ACK/Offer时Camera STOP不能成功。最终H/M/B/D均成立，USB及Camera成功ACK的入队、消费均可区分。

### 失败判据

帧尚被使用就释放；超时强制清引用；USB ACK成功但Return未确认；D先于M；Bridge未空闲便成功STOP ACK；Control收到旧/错source/错command_id ACK后错误前进。

### 源码调用链

Control STOP → USB命令队列 → App_USB STOP → UsbTask显式Cancel/Process/Bridge/Process → Bridge Return/ACK → App_USBFinishRound → USB_Mode_Stop → USB ACK → Control STOP_CAMERA → App_CameraHandleCommand → Media/Bridge/FinishRound → Device Stop → Camera ACK。

### 测试记录表

| 控制入口/请求返回 | epoch/lease | USB Return及ACK消费 | Camera STOP command_id/ACK | H | M | B（双端） | D | Control最终状态 | 结论 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 未执行 |

## 8. H3B：正常STOP（USB与Storage都曾持帧）

### 前置条件

H3A入口可用，Storage公共接口也有合法触发途径；已确认可写SD卷。JPEG模式，USB+录像同时运行，并在权威FIFO账本确认USB/SD引用实际出现过。未捕获重叠持帧，不能声称覆盖“双路同时持有”。

### 操作步骤

1. 调用 `App_StorageStartRecording()`，记录请求返回和实际录像状态，USB继续收流。
2. 显式调用 `App_StorageStop()`，确认返回SYS_OK后s_accepting=false；继续运行，允许已接收写入和文件收尾，不停CameraTask。
3. 跟踪Storage标记RETURN_PENDING → CameraTask的Bridge调用App_StorageProcessReturns → CameraMjpegFifo_ReleaseLease成功 → 槽位FREE。记录generation/session未被旧凭据替换。
4. 等待真实排空：s_count=0、槽位均FREE、pending_returns=0且没有sd_owned。另查文件Finish/Close/Unmount结果，不能用Storage IDLE替代帧排空。
5. 再经原Control入口请求STOP，按H3A完成USB Return/ACK、Media、Device、Bridge和Camera ACK验证。
6. 可选交错子项：在Storage STOP已成功受理、仍有合法在途SD帧时再提交Control STOP，确认CameraTask持续处理SD归还；只有实际捕获此窗口才填写覆盖。

### 观察位置与预期结果

StorageTask处理文件；CameraTask独占实际SD租约归还。pending_returns失败时保持待归还而不是强清。最终H/M/B/D分别成立；文件结果单列。

**现有Control STOP不负责自动停止Storage录像。** 不要直接在持续录像期间只发Control STOP并期待正常AVI封尾；这会触及原有无帧超时/封尾风险，不属于本轮已修复功能。上述步骤显式Storage STOP是本例前置协调，不是新增全局STOP协议。

### 失败判据

SD尚持引用就回收/覆盖；Storage直接Media Release；仅文件已关闭就认定帧已归还；Return失败仍将槽位FREE；因尚未排空强制完成Camera STOP。文件关闭失败须单列，不伪装成录像完成。

### 源码调用链

StorageStop → 关闭接收 → StorageTask Process/Storage_Finish → 槽位RETURN_PENDING → CameraTask Bridge → App_StorageProcessReturns → FIFO ReleaseLease → 槽位FREE；随后Control USB STOP → Camera STOP → FinishRound。

### 测试记录表

| 文件/原SD身份 | 双路重叠证据 | Storage STOP入队 | Finish/Close/Unmount结果 | 队列/槽位/SD引用 | SD归还结果 | USB归还 | H/M/B/D分别记录 | Camera ACK | 结论 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 未执行 |

## 9. H4：正常STOP后重新START

### 前置条件

H3A或H3B通过；H/M/B/D全部成立，Control IDLE，无ACK待发、外部租约或文件未清理。Device ERROR、Control ERROR时不伪装成具备普通重启条件。

### 操作步骤

1. 保存停机快照及Bridge旧stream_epoch/lease_id，确认旧ACK没有被当作新的启动完成。
2. 由合法业务入口调用ControlTask_RequestStart，记录入队结果。观察Control发Camera START后等待成功ACK，再发USB START。
3. 跟踪StartResource → BridgeCameraBegin → App_CameraInitStream → Prepare → Device_Start → Media StartStream，确认新周期硬件运行、完整帧重新到达。
4. 重新打开播放器，确认真实新画面、完成帧持续增长、帧借还继续；记录新epoch及实际租约轨迹。
5. 无注入重复正常STOP/START至少10轮，每轮都等真实完成，不通过改变量或复位替代Restart。另做一次完整断电冷启动检查初始配置链。

### 观察位置与预期结果

已有configured/initialized缓存会被复用：正常重启**不要求再次执行完整OV5640寄存器配置和Media Init**。完整启动链应依次检查准备条件、Device_Start和Media Start；首次冷启动才验证完整初始化。旧resource_stop_confirmed在新启动前清除，Bridge新周期不得混用旧帧身份。

### 失败判据

未排空即重启；Device ERROR被强行改READY；START接受被当作采集成功；旧ACK推动新命令；新周期显示旧静态帧或不持续产生新帧；重复轮次遗留stopping/待发ACK/外部引用。

### 源码调用链

Control START → Camera命令 → App_CameraStartResource → Bridge Begin → App_CameraPrepare（复用或首次初始化）→ Device_Start → Media Start → Camera ACK → USB START/ACK → 视频。

### 测试记录表

| 轮次 | STOP时H/M/B/D | Control起始状态 | START command_id/ACK | 旧/新epoch | Prepare复用或冷初始化 | Device/Media结果 | USB新帧证据 | 遗留状态 | 结论 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1—10逐轮填写 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 未执行 |

## 10. H5：连续运行

### 前置条件

先完成正常启动基线；禁用全部断点和注入。保持同一分辨率/格式/场景/主机参数。最低10分钟UVC-only；具备Storage合法入口后另测10分钟USB+录像，不覆盖已有文件。

### 操作步骤

1. 运行前记录计数器、最近错误和各任务stack_free_words；每60秒记录一次，不通过频繁停核抓Watch。
2. 相机/USB实测FPS来自对应measured_fps_milli÷1000，并用主机接收帧数/墙钟时间交叉记录。记录Camera dropped_frames及Bridge借还增量。
3. 观察首次DMA/DCMI错误、Control last_fault、USB状态、IPC/OLED失联或陈旧提示。发生错误先保留首次现场；本例不自动归因为FMC或R2。
4. 录像子项记录Storage实际FPS、丢帧、文件结果、pending_returns和栈余量；以显式Storage Stop正常收尾，再验证可解码。录像帧率不足仍是独立已知性能问题。
5. 运行结束按H3停止，停止后再核对借还差额是否归零；运行中存在1张合法在途帧时不能因acquired与released暂不相等判FAIL。

### 观察位置与预期结果

视频连续更新，完成帧持续前进，无新增持续性租约悬挂、内存覆盖或无原因停止。正常无故障运行时停止序号不应无故增加。栈余量保持非零，无StackOverflow Hook/assert。非零余量仅是最低检查，不代表充分安全裕度，记录最小值由用户审核。

### 失败判据

真实DMA/DCMI错误、UVC持续停止/卡死、Stop无法排空、任务栈断言/溢出、IPC持续失联均记录FAIL。未达目标FPS如实记性能不达标，不通过更改配置或降低期望消除。正常既有掉READY帧策略导致的计数增长不单独证明错误，须结合采集/输出实际速率和租约证据判断。

### 源码调用链

CameraTask BeginRound → Media Process → Bridge（含Storage归还）→ FinishRound；UsbTask UVC/Bridge；StorageTask App_StorageProcess；原IPC任务与状态发布路径。

### 测试记录表

| 场景/墙钟时刻 | Camera FPS/帧数/丢帧 | USB FPS/完成帧 | 主机FPS | Bridge借/还/在途 | Storage结果/待还/帧数 | DMA/DCMI/Control错误 | Camera/USB/Storage/IPC最小栈words | IPC/OLED | 结论 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| UVC-only，0/60/.../600s逐行填写 | 待填 | 待填 | 待填 | 待填 | OFF | 待填 | 待填 | 待填 | 未执行 |
| USB+SD，0/60/.../600s逐行填写 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 未执行；使用独立Watch入口 |

## 11. 源码依据（固定于d0bebf5）

| 文件:行号 / 函数 | 本方案依据 |
| --- | --- |
| CM7/Application/Tasks/camera_task.c:112 CameraTask_Process；:141 Reply | BeginRound在命令前；Media→Bridge→FinishRound；ACK/FAULT独立发送 |
| CM7/Application/app_camera.c:123 AdvanceStop；:169 BeginRound；:180 Run | 同轮事实去重、START回退ERROR不重复保护；普通停止只轮末推进 |
| CM7/Application/app_camera.c:245 Start；:508 StartResource；:198 Prepare | Device启动后Media失败才回退；准备状态缓存复用；启动失败仍后台排空 |
| CM7/Application/app_camera.c:365 FinishRound | Stop→IsStopped→Device→Bridge→ACK；BUSY继续，原失败不伪装成功 |
| CM7/Media/Src/camera_capture.c:450 StartStream；:1403 JPEG Start | 两种启动失败注入位置不同，保留实际采集错误 |
| CM7/Media/Src/camera_capture.c:524 Process；:695 Stop；:815 IsStopped；:823 StopFacts | ERROR即时保护；DRAINING保留租约；实际尝试序号和只读事实 |
| CM7/BSP/bsp_dcmi.c:590 Stop；:1439 WaitStopped | 等DMA EN/CAPTURE/ENABLE归零，并恢复HAL，不能只看HAL返回 |
| CM7/Device/Src/device.c:237 Stop；CM7/Device/Src/ov5640_device.c:182 Stop | 成功READY、失败ERROR；传感器停止由Device执行 |
| CM7/Device/Components/OV5640/ov5640.c:2173 Stop | 具体传感器停止写SYSTEM_CTROL0=0x42；软件回调成功与电气验证分开 |
| CM7/Application/app_uvc_bridge.c:129 CameraIsIdle；:137 CameraProcess；:419 UsbIsIdle；:456 CameraRelease | Camera侧Idle不包含SD全部账本；Media释放后才生成Return ACK |
| CM7/Application/app_storage.c:186 Stop；:334 ProcessReturns；:920 Finish | 显式Storage停止、Camera实际SD归还、文件封尾/关闭/卸载单列 |
| CM7/Media/Src/camera_mjpeg_fifo.c:441 HasLoan；:453 OnStopped | USB/SD引用权威账本；停DMA后保留外部引用 |
| CM7/Application/Tasks/usb_task.c:148；app_usb.c:124 FinishRound | Cancel/Process/Bridge/Process，归还完成后USB_Mode_Stop |
| CM7/Application/Tasks/control_task.c:169、:270、:285、:291 | 旧USB→Camera协调；请求接口只在合法上下文使用，没有内置手动触发点 |
| CM7/Application/app_messages.c:56 Send；CM7/Application/app_events.c:281 IsOwner | 发送权限与合法任务/ISR/PRIMASK/BASEPRI检查；队列入队成功与对端处理分离 |

## 12. 最终汇总与待解决风险

| 项目 | 当前状态 | 必须保留的限制 |
| --- | --- | --- |
| H1 首次START失败/停止成功 | 未执行 | API注入结果不得冒充自然故障 |
| H2 首次START失败/停止失败 | 未执行 | 硬件确实关不掉/HAL确实恢复失败尚无安全注入办法 |
| H3A USB正常STOP | 未执行；独立Watch入口已提供 | 不能用播放器关闭或断USB替代 |
| H3B USB+SD归还 | 未执行；独立Watch入口已提供 | Storage显式Stop；没有全局正常录像封尾新协议 |
| H4 STOP/START循环 | 未执行；独立Watch入口已提供 | 不提供Device ERROR自动恢复 |
| H5 无断点连续运行 | 未执行 | FPS/栈/IPC必须实测，不复用软件专项结果 |

签字记录：执行人____；板卡____；实烧CM7/CM4____；日期____；原始记录位置____；失败项____；待补项____；审核人____。

本文没有改变两项旧B1失败断言，也没有把当前协议核心软件通过写成消费者生命周期实板通过。任何没有取得的H/M/B/D证据单独留空，不能合并填写STOPPED。
