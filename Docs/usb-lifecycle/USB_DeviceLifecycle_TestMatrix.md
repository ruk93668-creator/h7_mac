# UDLM 验收与故障注入矩阵

> 基线：`35401119bef1d5a4594925478119e048f68c2c77`；研究分支：`codex/usb-device-lifecycle-manager`；审计日期：2026-11-19。
> 范围：Phase 0，仅文档；未修改生产/测试代码，未编译或烧录。
> 标记：**VERIFIED**＝本基线源码确认；**INFERRED**＝推断或拟议设计，尚非已实现能力；**UNRESOLVED**＝证据/契约尚缺；**NOT_TESTED**＝本轮未运行验证。源码引用采用仓库相对路径和一基行号，均相对于上述 SHA。

## 1. 本轮结果与未来测试分界

**VERIFIED**：本轮只进行文档结构、源码引用/行号、基线源码指纹与 Git 范围检查；结果在最终交付报告中记录。**NOT_TESTED**：没有编译、运行测试、烧录、USBPcap、重连或硬件证明。下表均是未来验收计划，状态统一 NOT_TESTED，不能当作已通过结果。

**INFERRED**：每项要固定测试commit/构建宏/编译器/板版本/端口线缆，保存原始日志、事件身份、transaction/proof/ledger、IRQ/global mask/PCD/Core/EP0状态和阶段。模拟 HAL 返回值只能验证软件反应，不能验证硅片停止；硬件测试必须有真实抓包和状态证据。

## 2. 九入口与标准请求

| ID / 状态 | 场景/注入 | 必须核对的结果 |
| -- | -- | -- |
| L01 NOT_TESTED | 主动Stop：OUT2 idle | 冻结CBW；Prepare票据；Class/Endpoint释放在PCD Stop之前；IRQ最终状态正确；重复Stop返回原结果 |
| L02 NOT_TESTED | OUT2正在收CBW时Stop | 不再arm CBW；完整正常完成或取消仅结算一次；buffer在proof前保留 |
| L03 NOT_TESTED | OUT2正在收Data OUT时Stop，使用RAM LUN | 部分数据不伪造完整写成功；不执行真实SD写；Worker与USB义务分别结算 |
| L04 NOT_TESTED | IN2单包/多包Data IN中Stop | 包间XFRC不能当整笔完成；BSP续包被围栏；原source在proof前不可复用 |
| L05 NOT_TESTED | Bus Reset在空闲/CBW/IN/Worker中 | USBRST→ENUMDNE顺序、硬件地址0；旧Token失效、旧请求收尾；新EP0/Setup不被旧事件污染 |
| L06 NOT_TESTED | Disconnect与Reset不同先后、同一IRQ快照 | 原资源清理恰好一次；sticky事实不丢；按真实检测来源记录，不虚构拔线回调 |
| L07 NOT_TESTED | CONFIGURED时SET_CONFIGURATION(0) | Core变更前gate；Prepare后同步DeInit；状态包仅Commit成功后提交；地址保留，PCD不停 |
| L08 NOT_TESTED | cfg0 DeInit失败/Close失败 | 即使Core忽略ClrClassConfig返回也不能发STATUS成功；Class/端点部分进度保留，RECOVERY_REQUIRED |
| L09 NOT_TESTED | 合法/非法SET_CONFIGURATION、同配置重复、默认/地址状态 | 既有合法路径一致；非法请求仍STALL；不将所有请求异步化 |
| L10 NOT_TESTED | UVC Alt0：无帧/待slot/IN flight/WAIT_CONFIRM | 仅VS范围；EP1 proof后Payload取消；RETURN_ACK后归还；VC/Probe/Commit不被清空 |
| L11 NOT_TESTED | Init失败：Clock、HAL Init、FIFO、MSC IN/OUT open、BotStart、UVC alloc | acquisition bitmap准确，逆依赖回收；回滚失败保留现场，不假定从未初始化 |
| L12 NOT_TESTED | PCD Stop / DeInit失败 | 捕获raw返回和后置状态；不提前关IRQ让收尾失去进展；失败不清callbacks/owner |
| L13 NOT_TESTED | Suspend/Resume | 不释放RAW/Frame/Context，不认定安全归还；RECOVERY_REQUIRED不被Resume清除 |

## 3. Endpoint、FIFO、HAL/LL 故障注入

| ID / 状态 | 注入点 | 验收观察 |
| -- | -- | -- |
| E01 NOT_TESTED | 同源stop与normal complete竞争：完成在fence前/后 | Transfer terminal互斥；无双归还、重复CSW/回调 |
| E02 NOT_TESTED | EPDISD延迟、缺失、与XFRC同置位 | 不用EPDIS写入当完成；保留原identity；源码/手册接受谓词满足后才能proof |
| E03 NOT_TESTED | EPENA=0但旧XFRC、RX数据或PIO source仍存在 | proof拒绝；不因软件idle释放；整笔完成与单包区分 |
| E04 NOT_TESTED | LL Stop轮询耗尽HAL_ERROR | SYS_ERR_IO/raw HAL_ERROR，非Pending；首因保存；新请求被围栏 |
| E05 NOT_TESTED | other OUT active导致BSP guard BUSY | 未调用HAL；来源BSP_GUARD、具体依赖可WAIT；不与HAL_ERROR混淆 |
| E06 NOT_TESTED | HAL lock BUSY / HAL_TIMEOUT | 原始来源/阶段保留；不推断所有BUSY是共享FIFO冲突 |
| E07 NOT_TESTED | LL Tx/Rx Flush超时，HAL_EP_Flush仍OK | 未来proof读取真实LL结果，拒绝成功；不能靠HAL返回覆盖缺口 |
| E08 NOT_TESTED | HAL Stop/IN EPDISD/USBRST内Flush失败 | 每位置分别测试；pre/post证据不丢，未知Flush不签proof |
| E09 NOT_TESTED | 共享RX FIFO：两OUT活跃、EP0新SETUP、局部Stop | 不冲掉无关流量；扩大scope需重新Prepare；EP0特殊条件独立验证 |
| E10 NOT_TESTED | PIO续包恰在Abort前；TXFE与Stop同IRQ | 不重装已停止endpoint；buffer/offset账目一致 |
| E11 NOT_TESTED | AbortAll数据端点scope同时EP0活跃 | 不清EP0旧记录/新SETUP；若实现仍需扩大scope则显式拒绝局部操作 |
| E12 NOT_TESTED | Close返回HAL_OK而LL deactivate未证明 | Close后置证据不足不Free；不能把mask变化等同硬件完成 |
| E13 NOT_TESTED | 旧pending IRQ跨Open generation后到来 | 不给旧IRQ贴新token；新arm前fence不满足时必须阻止新arm |
| E14 NOT_TESTED | IRQ初始disabled/故障中mask部分改变 | 保存/恢复按阶段契约；不能无条件enable或disable；无等待自锁 |
| E15 NOT_TESTED | 外设Reset在IRQ disabled短Commit窗口发生 | pending lifecycle事实使票据失效或下一ISR立即围栏；不信“CPU关IRQ阻止硬件Reset” |

**VERIFIED**：注入依据分别是 BSP other-RX guard（`CM7/BSP/bsp_usb.c:346`）、BSP续包（同文件 `CM7/BSP/bsp_usb.c:655`）、HAL Flush吞返回（`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:2018`）和LL轮询（`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:915`）。**INFERRED**：无需修改 ST Core/HAL 的测试 double 可用于原接口故障注入；具体 harness 待授权，本轮不修改测试代码。

## 4. 票据、旧Session与EP0

| ID / 状态 | 场景 | 必须成立 |
| -- | -- | -- |
| T01 NOT_TESTED | 未Prepare直接Managed DeInit | 无资源释放；状态veto；受控故障可诊断 |
| T02 NOT_TESTED | ticket重放/被篡改/扩大scope/membership改变 | 拒绝；Class Free/Close至多一次 |
| T03 NOT_TESTED | Prepare后新arm/重新注册/open generation变更 | 票据失效；重做Prepare，不能无条件Commit |
| T04 NOT_TESTED | 旧Session迟到软件completion/worker/Frame ACK | cleanup-only匹配原ledger，不能推进新协议；不直接丢旧义务 |
| T05 NOT_TESTED | 正常完成、取消、失败Reconcile重复到达 | 所有权只结算一次；历史失败不被重写 |
| T06 NOT_TESTED | 新SETUP在Prepare/Commit前到达 | 旧gate/status资格失效；旧cleanup继续；原请求不重放 |
| T07 NOT_TESTED | Core本地Commit后LL状态提交失败 | 本地变更与未确认分开；不能重复释放、不能返回假ACK |
| T08 NOT_TESTED | 状态已提交后新Reset/Disconnect/Stop | Token失效；旧IN完成不能确认新控制请求 |
| T09 NOT_TESTED | EP0标准普通请求/UVC Probe/Commit/TEST历史对照 | 正常数据/STALL/取消语义一致，严格BM身份检查，不全局接受BUSY |
| T10 NOT_TESTED | identity/ticket/sequence耗尽，retired表满 | 拒绝新工作；不回绕、覆盖、清旧owner；故障记录保留 |
| T11 NOT_TESTED | notification合并/丢唤醒/队列满/ACK重发 | Task查持久记录继续；不丢不可拒绝Reset；重发回缓存结果不重复Release |
| T12 NOT_TESTED | ClassRelease成功、公共Close失败、PCDStop失败组合 | 分段进度正确；后续收尾不二次Class Free；旧Class pin不悬空 |

## 5. BOT Reset Recovery 专项

**VERIFIED**：现有协议入口与等待链为 `CM7/Service/usb_msc_class.c:134`、`CM7/Service/usb_msc_bot.c:262`、`CM7/Service/usb_msc_bot.c:366`。以下均 **NOT_TESTED**：

1. RAM LUN 的CBW idle、IN、OUT、CSW、BOT_FAULT分别发精确Class Reset；其他BM/index/length仍拒绝。
2. 端点Abort失败或backend drain延迟：无状态成功包、无新CBW；超时锁定而不释放buffer。
3. 排空后Status提交、实际IN确认分别记录；ClearHalt在确认前后交错，只有合法Reset Recovery链满足双端点ClearHalt才重新arm。
4. ClearHalt先IN/先OUT/重复/无Reset：保留现有协议规则；标准请求自身状态阶段不能污染Reset状态证据。
5. BOT Reset期间新Setup/Bus Reset/Disconnect：旧Token不能复活；原Worker即使无有效Control Token仍应安全排空。
6. 重连后INQUIRY、REQUEST SENSE、READ CAPACITY、READ(10)；保留Sense生命周期/CSW Tag/Residue/phase error行为；不借生命周期改写SCSI。

## 6. MSC Worker / SD IDMA 与 UVC Frame Lease

| ID / 状态 | 场景 | 验收 |
| -- | -- | -- |
| W01 NOT_TESTED | Worker QUEUED/RUNNING/DRAIN/DONE各时刻取消 | QUEUED未执行；RUNNING buffer仍pin；quiescent前不发排空proof |
| W02 NOT_TESTED | USB已停止但SD IDMA仍active | Class/数据buffer保留；USB proof不可替代SD proof；RAW不交FatFS |
| W03 NOT_TESTED | Worker失败/一次Recover失败/迟到SD IRQ | 原owner处理；保留锁定/RAW；无重新arm覆盖buffer |
| W04 NOT_TESTED | SD只读读中Reset/Disconnect、最后块/越界读取 | completed_blocks真实；不执行SD写；重连再读正确 |
| U01 NOT_TESTED | UVC活跃ISO，正常EOF与Alt0并发 | 正常帧仅计一次；取消不伪造EOF；Probe/Commit/描述符保持 |
| U02 NOT_TESTED | packet Cancel BUSY、Abort失败、ISO incomplete | 帧仍持有，RECOVERY；proof前不PayloadAbortPacket |
| U03 NOT_TESTED | Frame Return队满、Camera Release失败、重复RETURN_ACK | 原租约保留；原owner重试/缓存ACK；无二次Release |
| U04 NOT_TESTED | Class status INVALID_STATE但旧frame proof缺失 | 不以查不到Context直接放帧；故障可继续收尾 |
| U05 NOT_TESTED | 双span/JPEG帧跨包与Stop | 保留源span生命周期、packet边界/FID，Media/Camera数据不改 |

**VERIFIED**：原事实位于 `CM7/Service/usb_msc_storage.c:86`、`CM7/Service/usb_msc_storage.c:156`、`CM7/BSP/bsp_sdmmc.c:957`、`CM7/Service/usb_uvc_payload.c:236`、`CM7/Application/app_uvc_bridge.c:280`。**INFERRED**：不改SDMMC/FatFS/Camera数据管线的实现；通过adapter/double及原owner提供诊断。若测试需要超范围修改，先申请。

## 7. 三组实板联合验收模板

**NOT_TESTED**：本轮没有执行。未来须按相同电脑/USB口/线缆依次测 RAM MSC、SD只读MSC、默认UVC。每组冷启动、正常断开重连10次、活跃传输断开、BusReset、协议恢复及重连后再传输。

每次记录（**INFERRED**要求）：

- 编号、固件commit/ELF SHA256、宏与只读保护、冷/热启动、传输状态、操作时刻。
- **重连前启动**USBPcap；保存原始pcapng/解析日志/文件SHA256；不要用只有成功最终描述符的结果代替完整枚举时序。
- GET_DESCRIPTOR、SET_ADDRESS、SET_CONFIGURATION；MSC额外INQUIRY/容量/READ，UVC额外Probe/Commit/Alt1/活跃ISO/Alt0。
- IRQ pre/post、Reset/Connect/Disconnect序列、PCD/Core/Platform地址与EP0、端点Proof、ledger refs/RAW/Worker/frame lease状态。
- 第1～10次分别有结果和证据文件名；失败保留，不只报告成功次数。无法确认拔线回调来源时写UNRESOLVED。
- SD仅只读，不通过格式化/建文件测试写保护；无独立原件时端到端原始正确性NOT_TESTED。

**INFERRED**：通过标准是全部安全不变量和目标协议都成立，不能以“Windows又看到U盘”作为所有资源已归还或恢复设计通过。

## 8. 软件回归与记录规则

**INFERRED**：先保存原测试输出/已知失败，再运行新增UDLM故障注入；完整CM7 RAM MSC、SD只读MSC、默认UVC构建记录编译器命令、Error/Warning、MAP静态内存/栈。生产成功路径/固有失败断言保持；任何历史失败原样报告，不调整断言造PASS。新增测试不能只镜像实现，必须验证独立所有权不变量与失败现场。

**NOT_TESTED**：上述构建/软件测试/实板回归均留给获授权的实施阶段；本轮不存在可宣称的固件验证结果。
