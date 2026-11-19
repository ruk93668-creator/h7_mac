# UDLM 增量迁移计划

> 基线：`35401119bef1d5a4594925478119e048f68c2c77`；研究分支：`codex/usb-device-lifecycle-manager`；审计日期：2026-11-19。
> 范围：Phase 0，仅文档；未修改生产/测试代码，未编译或烧录。
> 标记：**VERIFIED**＝本基线源码确认；**INFERRED**＝推断或拟议设计，尚非已实现能力；**UNRESOLVED**＝证据/契约尚缺；**NOT_TESTED**＝本轮未运行验证。源码引用采用仓库相对路径和一基行号，均相对于上述 SHA。

## 1. Phase 0 交付边界

**VERIFIED**：本轮只写 `Docs/usb-lifecycle/` 八份指定文档，未更改生产/测试/配置，未构建或烧录；源码引用与 Git 检查作为文档验收。本文所有实现阶段均 **INFERRED / NOT_TESTED**，需人工批准后才能开始，本文不自动授权 Phase1。

**INFERRED**：先建立证据与停止原语，再让协调器承担权力。保留基线协议算法，不将 MSC、UVC 或应用全部重写为通用异步框架。每一阶段只增加可审查的小边界，默认仍单 Class。

## 2. 分阶段实施矩阵

| 阶段 | 拟修改/新增文件 | 新增或扩展接口 | 验证/验收门槛 | 依赖 | 回滚方案 |
| -- | -- | -- | -- | -- | -- |
| P0 本阶段 | 仅本文及同目录七文档 | 无代码接口 | 八文档、真实引用、scope diff、提交推送与SHA一致；硬件未知明确保留 | 指定基线/干净新 worktree | 追加更正文档提交，不改历史/原工作区 |
| P1 契约与可观测性 | Platform usb_device.c/.h、BSP bsp_usb.c/.h、专用测试（均需下一轮授权） | typed error/proof snapshot、ISR pre/post、identity/tombstone 查询 | 不改变成功协议行为；首因保留；RM0399器件/版本/停止顺序确认；不读破坏性 FIFO 状态 | 解除 O1–O4 审批/证据门禁 | 独立 revert 此阶段提交；先停实板流量并确认旧所有权归还 |
| P2 有界同步停止原语 | BSP usb + 测试；Platform proof adapter | AbortWithProof/QueryProof、受控 Reconcile、真实 Flush 结果 | OUT/IN/EP0/ISO、正常完成竞争、Flush失败、共享RX都能区分成功/未知；无提前释放 | P1 硬件证据与接口授权 | 保留旧入口；遇未知仅锁定，不退回不安全“清busy”；回滚前排空 |
| P3 UDLM 主动 Stop | 新普通C coordinator/contract、Platform、USB Task/Mode/AppUSB adapters | 静态 Registry、RequestStop/Process/Query、PreparedTicket/Ledger | 单执行者；重复Stop幂等；ClassRelease前prepared；PCD Stop前Class安全释放；失败保留资源/IRQ实况 | P2、静态容量/栈/时间预算 | 逐批迁移调用者；关闭新入口只能在STOPPED且ledger空；不能热切回旧状态机 |
| P4 Reset/Disconnect/EP0 gate | Platform Managed callbacks/Setup，BSP lifecycle事实 port，测试 | ControlGate、精确标准Setup pending、LL status veto、Session fence | 新SETUP取消旧票据；Core同步回调不等Task；状态提交失败无伪ACK；Reset早期事件不污染新Session | P3、EP0/USBRST证明 | 原Core保持字节不变；受控停止后revert外围adapter；不能运行中取消gate丢cleanup |
| P5 MSC 生命周期接入 | MSC Class/BOT最小辅助、Storage proof adapter（另授权） | FreezeCBW、PrepareBotReset/Stop、Worker identity/proof query | 保留BOT/SCSI规则、RAM LUN回归；ClassReset/status/双ClearHalt；SD只读下IDMA独立证明/RAW保留 | P3/P4、Storage port批准 | 保留RAM/SD只读模式；排空Worker后revert；不开放SD写入，不自动交FatFS |
| P6 UVC 生命周期接入 | UVC Class adapter、Bridge proof port（另授权）；Payload算法保留 | PrepareAlt0/FrameDrain、retired-frame proof、lease ACK映射 | 活跃ISO Alt0/Stop/Reset；正常EOF、取消归还、ACK重发；视频数据和描述符历史对照 | P3/P4、Camera所有者port授权 | 停流/取消并等RETURN_ACK；确认Frame/packet无借用后回滚adapter |
| P7 联合稳定性及未来 Composite 评估 | 仅已授权模块适配/测试与报告；Composite实现另立任务 | 多participant scope规划；不等于新增Composite Core | RAM→SD只读→UVC三组同机同口，连续10次重连、活动中断开/迟到IRQ；Composite另核接口、路由、FIFO/FS带宽 | P5/P6全部通过 | 继续单Class；每次退回稳定提交前保存失败证据并排空资源 |

所有阶段 **INFERRED**；本轮仅完成文档阶段。新增文件名是候选，具体 ABI/模块数量不在本轮冻结为生产决策。

## 3. 实施前必须解除的阻塞

**UNRESOLVED**：

- BSP 证明/IRQ 顺序/Flush 结果是安全实现的必要扩展。当前只允许 Docs；即使此前 MSC 任务有 BSP 有限授权，也不能推定本 UDLM 阶段已经获生产修改许可。
- 标准 EP0 gate 必须证明 PIO 接收新 SETUP/状态延迟合法，Task Commit 时间预算满足主机超时。现有 Class Deferred API 不可直接放宽。
- RM0399 对应器件/修订的 IN NAK、OUT global NAK、EP0、EPDISD/FIFO 时序以及芯片 errata 适用性需核验；目前只有源码位定义/访问路径。
- HAL Flush 吞掉结果，不能只包一层原 HAL 返回当 proof；应评审 BSP 调现有 LL FIFO 原语并保留真实返回/后置状态的方案。若仍必须改 HAL，另行提出架构申请，不先写代码。
- Camera/Storage proof port 若要求改动既有 owner/消息 ABI，也需明确授权；不把跨任务同步塞进 Platform。

## 4. 保持与废弃内容

**VERIFIED**：现有协议及数据边界：`CM7/Service/usb_msc_bot.c:276`、`CM7/Service/usb_uvc_class.c:366`、`CM7/Service/usb_uvc_payload.c:204`、`CM7/Application/app_uvc_bridge.c:268`。**INFERRED**：所有 BOT/SCSI 命令、UVC Probe/Commit/包构建/FID/EOF、SDMMC实现及 Camera/Media数据结构保留；只在边界增加准备/证明/收尾适配。

**INFERRED**：保留现有端点API和模式选择；淘汰“无票直接DeInit”“HAL Flush OK足以证明”“epoch变化即丢旧事件”“INVALID_STATE即释放”的调用方式。不能删除 ST Core 函数以实现架构统一，也不新增无理由 RTOS Task/heap。

## 5. 软件与硬件验收顺序

**INFERRED**：先现有测试基线（保留原失败），再纯模型/故障注入，继而三类完整固件构建，最后 RAM 实板→SD只读→UVC活跃流；每个结果记录 commit、配置、工具/命令、原始日志。测试失败停止扩大 scope，不改旧断言制造全绿。

**NOT_TESTED**：本轮没有执行上述测试或构建。未来 SD 实验始终显式 WRITE_ENABLE=0、阻断实际写入；USB重连不能代替卷所有权验收。MSC-5/SD可写/Composite均不自动获准。

## 6. Git 交付与审查

**INFERRED（本轮交付程序）**：只在新worktree检查diff，验证每个引用文件/行号、八文件名称、Markdown表格/内部链接；逐个stage八文档，不用 `git add -A` 混入其他文件。提交后推送研究分支并 `ls-remote` 核对。网络失败保留本地提交，如实报未同步，不强推、不改远端、不动原工作区。

**VERIFIED**：本轮范围不包含 Keil 修改，也不包含原工作区的任何操作。源码未改变时不运行无关构建来冒充设计验证。最终提交号以实际 Git 交付报告为准，文档内不自引用尚未产生的commit SHA。
