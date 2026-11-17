# Phase4 Camera Module软件基础交付

输入9179dde62a37b35001d3ed77dcf3e850b0e874fd；目录D:/32/STM32H7/h7_mac；分支codex/architecture-media-graph-lifecycle。未改五核心/路线，不进入Phase5或Phase8。

| 工作包提交 | 生产实现 |
|---|---|
| 8d51df8 | camera_format.c/.h：中立配置映射，实际OV5640/Capture验证器 |
| 02926ac | camera_module.c/.h：不可变配置、Lifecycle Ops、owner轮次、Scoped结果/事实、Resource查询 |
| 6395824 | camera_endpoint.c/.h：分离双端、可靠提交/完成/消费、完整身份与重试 |
| 最终验收提交 | 测试、回归、接口、预算、Native门禁与只读Diff草案 |

真实C/H独立ARMCC5链接执行，并非空接口。Native控制为可信端口，没有启用第二Capture/Task/硬件控制器，未复制资格表/物理引用。Phase1/2/3公开契约沿用，不改旧算法或协议。

## 验证

77 ID×O0/O2=154/154 PASS，负对照10/10有效；原Phase3 158、Resource112、Phase1 250、Replay648、ABI2、C1 419、C1.1 24全部通过。实际验证器链接原文件，硬件依赖为触及即失败的测试陷阱；控制/所有权/运输是可控模型。真实Composition/EM→Endpoint→Camera路径已测，非真实RTOS或硬件集成。

CM7/CM4原工程兼容Rebuild均0 Error/0 Warning；新库独立O0/O2编译/链接/执行。Camera336B+双端124/88B共548B caller对象；ROM/Map/栈及回调未知深度见Phase4_ResourceBudget。完整命令、退出码、trace、Source hash和镜像hash见Tests/V2Phase4及output/V2Phase4。

## 关键行为

Configure仅准备，不启动DCMI；普通Deactivate只提交意图，Native FinishRound是唯一普通Stop落点；保护性停止及同轮去重归原owner事实。当前硬件观测、历史Stop失败、原操作结果分开保存。后台恢复不把原IO改OK，超时不强制归还。

QueryExitEvidence按完整R关联G并调用MR_VerifyExit，不用HasLoan/Debug/Storage IDLE替代。USB/Storage独立退出软件路径已测，另一方持有不被释放；未调用真实Qualification RELEASE成功回调。详细接口与调用图见CameraModule_InterfaceContract.md。

## 仍未完成/阻塞

- 两个总体NOT_PROVEN保持：真实Grant→Frame Lease、Stable Admission Closure。原72项和新增32项真实门禁仍NOT_RUN，ScenarioMapping只映射子契约，不累计成104项通过。
- Raw跨Start/slot复用token/epoch未证明；MJPEG查询、原始用途区分、D/T发布、USB真实ACK消费、Storage实际ReleaseLease需单独适配。
- Legacy private Stop结果绑定Qualification身份，不能假充V2执行身份；需要中立domain-tagged入口，保留单Stop结果/单FinishRound推进者。
- CameraTask/BOOT/Keil未切换，运输只有端口与可控队列，未烧录硬件；旧V1成绩不作为本轮通过。
- 独立运行中Media Fault的自动LC→ERROR公共转换及真实Device ERROR恢复需单独审查。当前保留media_result/历史故障/真实物理状态，不直接写LC内部状态或伪造硬件运行。

最小只读Diff和其余需审批的控制/代次改动见Phase4_LegacyAdaptationPlan.md及Phase4_Legacy_ReadOnly_Proposal.diff。草案未应用/编译，不授权据Mock成功放开released/idle。

## Phase5前置接口（未实施）

USB独立Lifecycle通过Composition/EM协调Camera，不直接调用Capture Stop；消费者QUERY/ACQUIRE/RELEASE继续C1/C2 Tx/Rx/Replay，不用执行完成通道代替资格事务。Frame交接遵循G/H→真实M→后绑定B→UVC完成/Abort→Return→实际Release→ACK消费→T退休。真实证明不足时不能确认退出。

VCameraClient执行消费不是USB Frame ACK；Reset/Disconnect不隐式撤销资格。B2 ID23有界BUSY、ID31未退出拒绝重启/合法成功后故障复位保持，未实现USB模块或改变其逻辑。

## Git保护

只新增Camera模块、Tests/V2Phase4与本阶段文档。旧Application/Media/BSP/Device、Replay、USB/UVC、SDMMC/FatFS、Phase1/2/3、旧测试、M4源码无改；用户Keil两文件hash原样，output和其他工作区未清理。软件基础完成，真实集成/实板未完成，不合并main、不自行进入Phase5。完整Diff位于output/V2Phase4/Phase4_Full.diff，推送后核对远端SHA，失败不虚报成功。
