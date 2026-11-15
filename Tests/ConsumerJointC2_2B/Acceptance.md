# C2.2B Test Redesign & Acceptance

日期：2026-11-15。分支：`codex/stage-C2-2B-B2B6-v1-compat`。生产源码、回归及正式重编基线：`cc9f25a54d95cdfbe332c1c5f7a4791ff06e68d3`。main维持`f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`；本轮不更新、不合并PR #8。

## 结论

**测试体系重建及证据封存已完成；C2.2B最终软件验收未通过。** 新联合测试10/11，发现一处P1生产路径缺口：USB STOP失败后的后台停止成功未提交RELEASE意图。本轮按授权只记录缺陷，没有修复生产代码。

必须分别判断：

- **数据面集成**：顺序授权、双消费者帧处理、AVI写入封尾、两路帧归还及故障注入已获得真实业务模块证据；新联合测试总计10/11，仍不能宣布完整C2.2B通过。
- **当前退出保护**：独立核心R01–R08全部通过（O0/O2共16次）；真实适配器没有伪造资格释放或硬件停止，仍安全保留资格。但USB异常退出的RELEASE意图存在缺口。
- **C2.2C真实关闭**：NOT_RUN/尚未实现，released/idle生产回调仍NULL，不能把核心桩通过解释成真正关闭。

## 新联合测试的真实边界

`joint_test.c`编译真实app_camera、camera_task、app_camera_consumer、app_camera_qualification、app_usb、app_storage、Bridge、FrameDispatcher、JPEG和AVI；Tx/Rx/Replay也链接真实实现。App_USB内部自己的消费者对象由真实HandleCommand/Process/FinishRound推进，不使用测试专用USB消费者替代。

初始化由仿真器进入真实CameraTask_Run，在首次App_EventsWait处暂停并交回显式测试调度；CameraTask自身执行QualificationPrepare并建立其自身ready状态。随后调用真实CameraConfigure，再通过真实App_USB START完成QUERY/ACQUIRE/匹配回复及GRANTED，资格核心首次启动Camera。Storage独立申请，GRANTED之前不允许Mount。

硬件、Device、CameraCapture和FIFO租约边界、UVC物理接口、文件介质为可控桩。JPEG样本来自既有已验证捕获文件，真实JPEG/CRC/AVI代码执行。USB/Storage调度器显式重现原每轮调用顺序；Camera使用真实CameraTask_Process。**不是运行真实抢占式FreeRTOS，未证明真实FIFO算法、硬件仲裁或跨核竞态。** 三条生命周期运输桩为四项静态有界队列并检查所属任务及来源；生产队列的实际权限/容量/通知语义另外由C1/C1.1回归覆盖。

停止时没有强制Camera STOP。正常退出请求保留grant并等待released证据；没有生成假的QUALIFICATION_RELEASED/HARDWARE_STOPPED。ID6对旧Camera STOP精确检查SYS_ERR_NOT_SUPPORTED ACK、命令号及来源，两路资格保留，硬件停止调用为零，随后完成帧归还。

## 测试结果

| 项目 | 本轮实测 | 判断 |
|---|---|---|
| 新联合11项 | 10 PASS / 1 FAIL（ID7） | FAIL，生产路径缺口 |
| R01–R08 | 8/8；O0/O2共16/16 | 核心Ops桩门控通过 |
| 原B1 | 37/39 | 保留ID9、ID10原断言与FAIL；批准的R2停止时序变化 |
| 原B1 Media / 联合 | 6/6；0/11 | 历史结果原样保留 |
| C1 / C1.1 | 419/419；24/24 | PASS |
| C2.1.1 / C2.2A / R2 | 648/648；162/162；74/74 | PASS |
| 既有B2 / B3原运行器 | 16/102；14/51 | 历史失败保留，未改判 |
| 授权前置补齐后的B2/B3原C入口 | 102/102；51/51 | 只说明C入口执行/断言，不等于Python完整验收 |
| B2/B3完整153项门禁 | 24 PASS；1 FAIL/C；128 BLOCKED/D | 未通过，不自动提升原129项 |
| 既有C2.2B业务专项 | 62/62 | 既有资源桩边界 |
| 既有Consumer专项 | 42/44，ID17 O0/O2 FAIL | 保留双路首次QUERY交错失败 |
| V1电源/I2C专项 | 14/14；24/24 | 软件桩通过 |

[11项因果契约及首个违例](contracts.json)、[原始与候选对照](OriginalVsCandidate.md)、[8项退出安全报告](ExitSafety.md)、[153项逐项分类](Legacy153Review.md)、[全量ID集合/顺序门禁](regression_id_gate.json)。所有历史suite的ID缺失、新增、重复及通过状态变化均为空，执行顺序一致；**ID门禁PASS不表示有历史FAIL的suite通过**。

原始事件独立保存在Evidence/0.json.gz至10.json.gz，153项对照在Evidence/legacy_pairs/，原B1调用轨迹在Evidence/b1_original/。每个新事件包含单调序号、任务执行者、操作、消息身份、Lease、状态快照及硬件调用计数；raw result寄存器对void函数不代表业务结果。AppMessage中ARMCC5枚举字节布局另由trace_layout.json测量，不把其当成13个语义uint32解码。

不再用完整旧事件逐项相等作为新联合测试的唯一标准。正常新增异步事件保留为A类架构差异；`first_contract_violation`只标识缺失的因果义务。旧完整等价结果仍作为历史证据保留，不因新C入口成功而提升129项。Storage在本联合集中先排空已有帧再STOP；STOP与在途帧交错的整条文件封尾/Return Pending顺序为NOT_RUN，未凭这些测试宣称已验证。

## 生产问题与范围控制

[Defects.md](Defects.md)记录P1根因及最小整改建议。没有改动App_USB、Camera、Storage或协议代码。新ID7保留FAIL；没有通过放宽断言、假RELEASE、清理Replay或强制停止Camera追求全绿。B2/B3剩余128项属于证据不足，不统称测试夹具问题，也不推测为生产缺陷。

## 两核正式构建、ABI和资源

使用源自基线的独立只读快照，原Keil工程、ARMCC5执行CM7/CM4完整Rebuild，均0 Error、0 Warning。未覆盖原output或用户Keil设置。完整日志：[CM7](Evidence/build/h7_mac_CM7.log)、[CM4](Evidence/build/h7_mac_CM4.log)；[镜像/Map/Callgraph SHA256](build.json)，[源码及测试哈希](source_manifest.json)。

- ABI：AppMessage=52B、Lifecycle=56B、CameraFrame=24B、Consumer=232B、Tx=144B、Rx=332B、Qualification=824B。
- ARMCC5消息偏移：source=1、command_id=4、stream_epoch=8、lease_id=12、result=16；Consumer.granted=220。
- CM7：RW=1376B、ZI=101536B；CM4：RW=936B、ZI=3576B。本轮生产代码/工程配置不变，Map总量和静态对象与既有基线一致，**本轮新增生产RAM=0**。
- 栈配置：CameraTask=6144B，UsbTask=8192B，StorageTask=8192B；未改变优先级、容量或等待周期。
- 链接器已知调用深度：CameraTask 496B、USB 540B、Storage 1416B；App_QualificationProcess 408B。间接回调的完整最坏深度及真实高水位不作已验证声明。
- 新测试未登记到生产Keil Target。[工程登记检查](project_registration.json)、[ABI/Map/Stack原始值](abi_map_stack.json)。

## 实板与尚未验证

用户此前报告V1的七项：OV5640/I2C1、USB单路、Storage录像抓拍、双路并行、分别停止一路、双停止后恢复均通过。本轮没有重新刷入或执行新的实板验收，不能把旧实板报告当成本轮故障场景的独立验证。START失败、真实硬件STOP失败、Media ERROR恢复、USB Abort失败后的资格退出、双路同时首次QUERY及C2.2C真实关闭尚未完成实板确认。

## 交付与审查建议

所有变更限于新增Tests/ConsumerJointC2_2B目录，包含独立测试源码、运行器、完整事件、差异/门禁/资源报告及构建日志。历史文件零改动。Git提交和远端核对结果在最终交付消息中给出；完整补丁另保存在本轮output/ConsumerJoint交付目录。

**不建议据本轮结果批准C2.2B最终验收或合并。** 先审核USB后台停止遗漏RELEASE意图的生产问题及后续整改范围，再补齐128项完整契约证据；不在本轮自行修复、不进入C2.2C。

## 复现入口

在仓库根目录执行 `python Tests/ConsumerJointC2_2B/run_acceptance.py`，当前应返回1，明确保留ID7失败。退出安全、ID和资源审计分别返回0。完整原回归入口为run_regression.py；两核重编入口为run_build.py，各自使用新的独立output快照。重编后按audit_resources.py核对ABI/Map。运行器和ARMCC5/Unicorn依赖路径沿用工程现有环境，不新增依赖库入库。

[最终运行器退出码](Evidence/final_execution.json)保留非零结果，不因后续审计命令成功覆盖此前失败。
