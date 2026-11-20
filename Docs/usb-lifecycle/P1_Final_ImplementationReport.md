# P1 Final Implementation Report — PARTIAL / BLOCKED

VERIFIED：工程 `D:/32/STM32H7/h7_mac_lifecycle`，研究分支 `codex/usb-device-lifecycle-manager`，起点 `9d51d0c5d5b62d04af3be968ec452dae8835c0c1`。最后生产源码SHA `8b5c6f42992ba708a2c7b48bd5bb05e1176eb545`。本报告随最终证据提交，最终交付SHA须以实际Git HEAD/远端查询为准，核对回执保留在ignored output，不制造自引用SHA。

**结论：P1尚未完整实施或通过。** 按任务允许的关键硬件/架构门禁停止，保留独立完成的安全改动。本轮不进入硬件执行，不具备合并main资格。

## 阶段矩阵

| 阶段 | 状态 | 实际交付 | 未闭合条件 |
| --- | --- | --- | --- |
| Phase0资源/调用链审计 | VERIFIED | 全局资源表、原路径上下文与责任 | 目标契约不等于现有统一ledger |
| C2双向协调 | PARTIAL / VERIFIED软件 | 单Controller身份，原子Fence，多方向Begin/Poll/Commit、独立失败和只读进度；21项611断言 | 只在显式opt-in路径执行；无EP0/消费者ticket，不自动Enable，不完成Controller Stop |
| C3消费者保护 | PARTIAL / VERIFIED软件 | Class DeInit在backend未drained时零Close；3项33断言；生产RAM/SD已接通 | 完整BOT Reset/同步Abort迁移未实施 |
| C4协议联合恢复 | BLOCKED | 旧协议专项重跑、保留结果 | Deferred Status依赖EP0及完整Controller协调 |
| D ISO/UVC | NOT_IMPLEMENTED / UNRESOLVED | 现有ISO/frame/Alt调用链和风险审计 | 独立ISO硬件退休/Frame消费者proof未实现；Alt0控制还依赖EP0；未改UVC |
| E EP0 | BLOCKED | RM/ES/真实LL行为见证5项14断言；最小接口申请 | OUT0不适用通用EPDIS，现有ZLP启动不能给出合格AHB时序证明，禁止越权改LL/Core |
| F统一闭环 | BLOCKED | 设计/边界及失败保留 | ParticipantRegistry/PreparedTicket、Reinitialize/Resume未实现；禁止端点证明冒充全设备证明 |
| G准备 | PARTIAL / VERIFIED工具 | 12项离线检查、3配置ELF匹配清单、SAVE-only脚本、离线pcap/HEX解析 | 全部实板NOT_TESTED，尚不具备执行准入 |

## 实际生产Diff

VERIFIED：仅6个生产文件，227行新增、13行删除：

- `CM7/BSP/bsp_usb.c`：IN Flush owner期间，只对“自有、未解除、被屏蔽的Global NAK＋单独SOF”放行原受限SOF分发。Reset/RX/EP/未知组合仍保守失败，不进入完整HAL分发。实际组合测试20/21证明软件分支及资源保留。
- `CM7/Platform/usb_device.c/.h`：Controller ID在真实Endpoint Begin前发布；短PRIMASK/DMB Fence；16方向槽进度；各Endpoint真实proof汇总；旧资源API及Core DeInit/Reset/Disconnect旁路否决；Enable成功能力仅用于选择安全路由，不充当proof。
- `CM7/Service/usb_msc_bot.c/.h`：新增有界backend.drained查询。
- `CM7/Service/usb_msc_class.c`：BotStop取消后先查drained，再允许尝试Close；未排空返回USBD_BUSY且不等待。

VERIFIED：基线到本轮实际生产patch：`output/MSC24/udlm-p1-final/P1_FINAL_production.patch`，SHA256 `67a3a2c1c4e6b01ce02951d8155d67472629c438cb0102ad41f344757d0762b5`，21558字节。机器可核对回执见Tests/UsbLifecycleFinal/reports/production_diff_receipt.json。实际代码亦可用 `git diff 9d51d0c5d5b62d04af3be968ec452dae8835c0c1 8b5c6f42992ba708a2c7b48bd5bb05e1176eb545 -- CM7` 审核。未修改本轮HAL/LL/Core、UVC、USB Task/Mode、Camera/Media、SDMMC/FatFS、Keil/MPU/Linker/CM4。

## 资源证明和失败边界

VERIFIED（软件/源码）：每个Bulk方向沿用真实BSP Transfer身份及StopAttempt；Begin阻止新提交；PIO/RX/FIFO/事件证据由原BSP原语退休；不会手动设置PROVEN。Global NAK仍由唯一lease协调，单Endpoint取消不Flush共享RX FIFO。任何未闭合方向保留Buffer、identity及first_error，其他方向仍有界推进。全组端点证明完毕后仍返回NOT_SUPPORTED/RECOVERY_REQUIRED，保留Class/Controller/Fence，不进入破坏性BSP Stop。

VERIFIED：Class/backend守卫只证明Storage消费者域，不证明USB、IDMA硅片或RAW归还。SD RAW租约保留，不因断线转给FatFS。Reset/Disconnect失效控制资格不等于释放源。完整逐资源创建/持有/退休表见P1_Resource_Ownership_Audit.md。

UNRESOLVED：没有统一生产PreparedTicket全集、ISO Frame Lease proof或EP0退休证明；无法安全解除总Fence并启动新Session。此处不存在已实现的自动恢复/无限重试。原未启用路径仍有历史限制，不能用新组件测试为旧路径背书。

## 软件结果

VERIFIED：原176项2312断言全通过；新双向21项611断言、MSC守卫3项33断言通过；另5项当前阻塞行为观察14断言符合预期、12项离线工具通过。17历史入口6成功/11失败；818条可比较结果无变化，2028个原测试文件未改。两个旧MSC枚举夹具各新增一个缺失守卫符号（链接错误11/8→12/9），单列UNRESOLVED；不修改旧断言、不把NOT_RUN算PASS。Payload O0/O2 case37原FAIL保持。详表见P1_Complete_SoftwareVerification.md。

VERIFIED：p1-final05 RAM MSC、SD只读MSC、UVC完整编译均0 Error/0 Warning；最终宏WRITE=0。MAP/ELF证明守卫接入RAM/SD；C2组件存在但应用不自动Enable，明确不满足完整生产生命周期启用验收。build_artifacts.json保留全部AXF/HEX/MAP哈希和日志。原始output不提交且不清理。

## 风险与集中审批

| 等级 | 缺口 | 处置 |
| --- | --- | --- |
| 关键 | EP0协议取消/硬件退休、所有Slave ZLP合格时序 | LL最小接口扩展申请，未批准不修改LL；如Core最终不可免改另审 |
| 关键 | Controller统一消费者ticket、Session Resume | 保留Fence/Class/资源，禁止自动Enable新契约 |
| 高 | ISO/Incomplete/FIFO/Frame退休及BOT Reset迁移未完成 | 保留历史失败，不能宣称视频或重连回归通过 |
| 中 | 两个旧枚举夹具新增未链接guard | 明确缺口；生产链接已过；后续按实契约扩充夹具而非成功空桩 |
| 中 | 多端点快照的USB Task栈余量 | 实板高水位NOT_TESTED，不改变RTOS配置 |

INFERRED：集中审核先审P1_LL_ZLP_InterfaceExtension_Request.md候选接口、合格AHB屏障和OUT0/EP0证据域，再决定后续实施；当前提案不等于批准或安全证明。P1-D独立ISO证明仍需实际实现，不将所有未完成内容简单归因于LL。

NOT_TESTED：本轮无烧录、Reset硬件、插拔、SD写入或主机实时捕获。离线准备目录prep-final05-*匹配当前AXF，RAM/UVC各4个模式专用符号缺失，SD0缺失；均明确UNRESOLVED且未猜地址。SAVE脚本没有执行；不将空pcap离线测试当USBPcap验收。

## Git交付与停止

VERIFIED：保留基线历史、原始FAIL、output及未跟踪RM0399 PDF（未加入Git）。阶段提交普通递增；只逐路径暂存授权源/测试/文档，不执行add-A、clean、reset、rebase、强推或main合并，不操作原MSC工作区。最终推送及完整本地/远端SHA以最终答复和交付回执为准；若推送失败如实保留本地。

本轮停在关键门禁，等待一次集中人工审核。**不宣布P1完成、不启用C1、不迁移MSC BOT、不自动操作硬件。**
