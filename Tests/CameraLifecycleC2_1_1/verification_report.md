# C2.1.1 Replay安全复用软件验收报告

## 基线和交付范围

- 基线：main@182c84e7334f0a62d96f1d3109b0d7fb8efb8158。
- 分支：codex/stage-C2-1-1-replay-reuse。
- 本轮只实现Replay普通C工具，尚未接入Camera、USB、Storage业务，不进入C2.2。
- 原工作区仅有未跟踪output/，本轮保留；正式构建前备份既有AXF/Map等输出。
- 软件专项648/648，未注入故障ABI验证2/2，原C1/C1.1协议443/443，门禁自测29/29，有效历史回归688/688。
- 28组历史回归相对基线无新增退化；19组历史失败/不可构建仍保留，不宣称全仓全绿。

## 实现与批准设计对应

| 约束 | 实现与验证 |
|---|---|
| 固定容量 | 每接收方向只有一条Replay、一条回复Pending、一个56B候选槽 |
| 单未完成请求 | Tx只在EMPTY绑定新请求；COMPLETE读取后也必须显式确认消费 |
| issued_through | 仅Stage成功推进，取消、提交失败或超时不回退 |
| 完整消费身份 | ConfirmConsumed逐字节比较完整原请求，再核对原请求与终结回复 |
| 安全退休 | 原Retire继续拒绝成功；新窗口只在合法后继接替时推进水位 |
| HELD/QUARANTINED | 非法提前后继保留隔离现场，不因旧回复之后成功入队而转合法 |
| 原子接替 | 临时ReplayBegin成功后才先推进旧编号水位、安装新记录、发布新当前状态；无业务回调 |
| 失败回滚 | 包括实际ReplayBegin包装返回失败、错误候选、错误输出及旧回复背压 |
| 四方向隔离 | 固定source/target/consumer绑定，独立编号与水位，不按session/generation清零 |
| 队列和通知 | 使用现有真实queue.c/list.c与消息路径，覆盖队列满、通知合并/失败、共享收件箱分流 |
| 无业务侵入 | 无运行期对象、无Initcall/Task/动态内存、无业务START/STOP调用 |

## 代码与文件清单

| 文件 | 修改原因 |
|---|---|
| CM7/Application/app_camera_lifecycle_replay.c | 新增受控Tx/Rx管理、候选保留、旧编号拒绝和安全接替 |
| CM7/Application/app_camera_lifecycle_replay.h | 新增固定布局、接口契约及ARMCC5大小/偏移断言 |
| MDK-ARM/h7_mac.uvprojx | 仅在CM7现有组登记新.c，原TargetOption、输出路径和CM4不变 |
| Docs/Design/C2.1.1_Replay安全复用.md | 最终接口、状态机、边界、未来适配约定 |
| Tests/CameraLifecycleC2_1_1/replay_test.c | 真实队列、异常交错、完整身份及耐久循环 |
| Tests/CameraLifecycleC2_1_1/layout_probe.c | 独立链接未注入故障的生产实现，核对ABI与调用图 |
| Tests/CameraLifecycleC2_1_1/run_tests.py | ARMCC5 O0/O2编译和Unicorn执行 |
| Tests/CameraLifecycleC2_1_1/run_legacy.py | 隔离重跑原C1/C1.1与门禁自测，不改原断言和结果文件 |
| Tests/CameraLifecycleC2_1_1/run_regression.py | 用原严格比较器比较全量用例ID、结果和历史错误指纹 |
| Tests/CameraLifecycleC2_1_1/run_build.py | 备份原输出后执行两核完整重编 |
| Tests/CameraLifecycleC2_1_1/audit_scope.py | 固定基线范围、旧测试、工程结构及实际Map审计 |
| 本目录结果JSON、两份构建日志、README及报告 | 保存实际验收结果；不提交output或测试二进制 |

原app_camera_lifecycle.c/.h、app_messages.c/.h和所有既有测试文件保持原样。Camera/USB/Storage、Control、Media/FIFO、Bridge、Device、BSP/HAL均无修改。全部原工程选项和CM4项目树在移除新增CM7文件登记后与基线一致。

## 接口与状态机

```text
Tx: EMPTY → READY → WAIT_REPLY → COMPLETE → 显式完整身份确认 → EMPTY
                └取消→ EMPTY（编号作废）
队列满保留READY；WAIT_REPLY无超时清除；GetCompleted不消费。

Rx: 完整候选 → HELD → 临时Replay构造 → 当前执行中
当前执行中 → 固定终结回复 → 回复入队 → 缓存保留
合法后继 + 旧回复工作完成 → 先推进旧水位 → 安装新Replay → 只交付一次
非法提前请求 → QUARANTINED，不自动恢复
```

发送端8个接口：Init、ReserveIds、Stage、Flush、AcceptReply、GetCompleted、ConfirmConsumed、CancelReady。接收端6个接口：Init、ReceiveRequest、Promote、Complete、FlushReply、GetStatus。完整输入输出和错误语义见头文件及设计文档。

退休后仅拒绝旧编号，不保存全部历史正文；当前Replay或候选中同编号不同正文返回协议错误。内部BUSY不自动发送为终结业务BUSY。SESSION_QUERY的零session受同一请求编号水位保护，缓存查询重放原结果，不重新求值。

## 专项测试结果

54个主要场景、14个完整请求字段变异、256个64次循环样本，每个分别执行O0/O2，共648项。循环还包含单窗口不重新初始化连续4096次；累计40960次模拟事务，覆盖QUERY/ACQUIRE/RELEASE/STOP_IF_IDLE及反向请求、变化的session/generation。没有实际启动或停止Camera。

| 场景ID | 测试 | O0/O2 |
|---:|---|---|
| 0 | ARMCC layout and queue sizes | PASS / PASS |
| 1 | Running duplicate executes once | PASS / PASS |
| 2 | Completed duplicate replays exact reply | PASS / PASS |
| 3 | Same ID modified generation conflicts | PASS / PASS |
| 4 | Successor while running quarantined | PASS / PASS |
| 5 | Successor before first ACK quarantined | PASS / PASS |
| 6 | Committed ACK permits safe successor | PASS / PASS |
| 7 | Retired request rejected | PASS / PASS |
| 8 | Gap request rejected | PASS / PASS |
| 9 | Duplicate reply backlog holds successor | PASS / PASS |
| 10 | Candidate duplicate coalesced | PASS / PASS |
| 11 | Candidate same ID changed body quarantined | PASS / PASS |
| 12 | Second distinct candidate quarantined | PASS / PASS |
| 13 | Zero-session query replay and retirement | PASS / PASS |
| 14 | Session and generation do not reset watermark | PASS / PASS |
| 15 | Malformed large ID leaves window unchanged | PASS / PASS |
| 16 | READY cancel burns issued ID | PASS / PASS |
| 17 | WAIT_REPLY timeout never clears | PASS / PASS |
| 18 | Result read is not consumption | PASS / PASS |
| 19 | Four domains isolated | PASS / PASS |
| 20 | Reinitialization rejected | PASS / PASS |
| 21 | Stage failure leaves issued unchanged | PASS / PASS |
| 22 | Maximum ID and exhaustion | PASS / PASS |
| 23 | Rollback malformed candidate | PASS / PASS |
| 24 | Rollback backward candidate | PASS / PASS |
| 25 | Rollback wrong-domain candidate | PASS / PASS |
| 26 | Tx callback reentry guarded | PASS / PASS |
| 27 | Rx callback reentry guarded | PASS / PASS |
| 28 | BUSY completion immutable | PASS / PASS |
| 29 | Failure completion immutable | PASS / PASS |
| 30 | Real full request queue retry | PASS / PASS |
| 31 | Real full reply queue retry | PASS / PASS |
| 32 | Notification loss does not duplicate | PASS / PASS |
| 33 | Candidate preserved across real full replay queue | PASS / PASS |
| 34 | Single pending Stage gate | PASS / PASS |
| 35 | Original ReplayRetire still sealed | PASS / PASS |
| 36 | Pure protocol status | PASS / PASS |
| 37 | NULL input checks | PASS / PASS |
| 38 | Output alias checks | PASS / PASS |
| 39 | Wrong domain rejected | PASS / PASS |
| 40 | Submission timeout preserves READY | PASS / PASS |
| 41 | Reply failure preserves pending | PASS / PASS |
| 42 | No automatic duplicate reply sends | PASS / PASS |
| 43 | Premature reply rejected | PASS / PASS |
| 44 | Late generation ACK rejected | PASS / PASS |
| 45 | Promotion output failure rollback | PASS / PASS |
| 46 | Cancel identity mismatch | PASS / PASS |
| 47 | Promote delivers at most once | PASS / PASS |
| 48 | Invalid initialization and context | PASS / PASS |
| 49 | Zero reservation rollback | PASS / PASS |
| 50 | Temporary ReplayBegin failure rolls back all state | PASS / PASS |
| 51 | Mismatched pending reply cannot authorize retirement | PASS / PASS |
| 52 | 4096 transactions on one persistent window | PASS / PASS |
| 53 | Shared inbox drains other domain and reverse reply despite held candidate | PASS / PASS |
| 60 | Full identity word 0 | PASS / PASS |
| 61 | Full identity word 1 | PASS / PASS |
| 62 | Full identity word 2 | PASS / PASS |
| 63 | Full identity word 3 | PASS / PASS |
| 64 | Full identity word 4 | PASS / PASS |
| 65 | Full identity word 5 | PASS / PASS |
| 66 | Full identity word 6 | PASS / PASS |
| 67 | Full identity word 7 | PASS / PASS |
| 68 | Full identity word 8 | PASS / PASS |
| 69 | Full identity word 9 | PASS / PASS |
| 70 | Full identity word 10 | PASS / PASS |
| 71 | Full identity word 11 | PASS / PASS |
| 72 | Full identity word 12 | PASS / PASS |
| 73 | Full identity word 13 | PASS / PASS |

原实现逻辑没有复制到测试：使用实际协议.c、实际新管理.c、实际消息适配和FreeRTOS队列源码。仅任务身份/调度出口使用测试桩。临时构造失败通过包装实际ReplayBegin返回失败注入，默认仍调用原实现；生产代码没有测试宏或故障注入口。另用未包装的独立ABI镜像核对大小与栈。

## 历史回归逐项比较

所有组均在新的基线/候选快照重新执行，使用原完整ID比较器。没有修改历史夹具或降低断言。

| 测试组 | 候选实际摘要 | 相对182c84e |
|---|---|---|
| B1Behavior | total=39, passed=39, failed=0 | 一致 |
| B1BridgeStorage | total=11, passed=11, failed=0 | 一致 |
| B1Media | total=6, passed=6, failed=0 | 一致 |
| B2Behavior | total=102, passed=102, failed=0 | 一致 |
| B2Integration | total=19, passed=19, failed=0 | 一致 |
| B3Behavior | total=51, passed=51, failed=0 | 一致 |
| Bridge | 历史夹具无法编译/链接，无可执行用例报告 | 一致 |
| CameraApp | 历史夹具无法编译/链接，无可执行用例报告 | 一致 |
| CameraContext | 历史夹具无法编译/链接，无可执行用例报告 | 一致 |
| CameraDeviceLifecycle | 历史夹具无法编译/链接，无可执行用例报告 | 一致 |
| CameraStream | 历史夹具无法编译/链接，无可执行用例报告 | 一致 |
| Cancellation | total=39, passed=0, failed=39 | 一致 |
| ContinuousMedia | 历史夹具无法编译/链接，无可执行用例报告 | 一致 |
| Core | total=204, passed=201, failed=3, assertions=2947 | 一致 |
| Descriptors | total=8, passed=7, failures=0, errors=1 | 一致 |
| EP0IRQ | total=4, passed=4, failed=0 | 一致 |
| EP1 | total=45, passed=0, failed=45 | 一致 |
| IpcStatus | total=448, passed=448, failed=0 | 一致 |
| JpegDevice | 历史夹具无法编译/链接，无可执行用例报告 | 一致 |
| JpegMedia | 历史夹具无法编译/链接，无可执行用例报告 | 一致 |
| NativeIntegrated | 历史夹具无法编译/链接，无可执行用例报告 | 一致 |
| NativeTest | 历史夹具无法编译/链接，无可执行用例报告 | 一致 |
| Payload | unique_cases=62, executions=124, passed=122, failed=2 | 一致 |
| Probe | total=89, passed=0, failed=89 | 一致 |
| Streaming | total=36, passed=1, failed=35 | 一致 |
| Strings | total=8, passed=8, failed=0 | 一致 |
| UvcLifecycle | total=40, passed=34, failed=6 | 一致 |
| VideoConfig | 历史夹具无法编译/链接，无可执行用例报告 | 一致 |

new_failures=0；缺失套件0；缺失用例0；源码冻结期间被回归改变的文件0。完整ID、失败位置、错误指纹及原始诊断见regression_results.json。旧C1/C1.1专项及比较器自测另在独立快照运行，原脚本和原断言字节不变。

## 编译和内存

| Target | Error | Warning | Code | RO | RW | ZI | 相对基线 |
|---|---:|---:|---:|---:|---:|---:|---|
| CM7 | 0 | 0 | 136802 | 3086 | 1328 | 100048 | 全部0增量 |
| CM4 | 0 | 0 | 26302 | 1438 | 936 | 3576 | 全部0增量 |

完整重编日志确认新源文件被编译；正式Map列出新工具各函数Removing。因为本阶段未接入任何调用，最终固件链接器移除未使用工具，不能把这解释为运行期接入通过。CM7原设置不生成HEX，AXF/Map存在；CM4生成AXF/HEX/Map，不改变原设置。

| ABI | 大小 | 关键偏移 |
|---|---:|---|
| AppLifecycleTxContext_t | 144B | pending=28 |
| AppLifecycleRxContext_t | 332B | replay=40，reply_pending=160，candidate=276 |
| AppLifecyclePending_t | 116B | 原布局不变 |
| AppLifecycleReplay_t | 120B | 原布局不变 |
| Lifecycle Message / AppMessage | 56B / 52B | 原布局不变 |

实际ARMCC5 O0/O2的sizeof、偏移断言及独立Map均确认上述结果。未来四方向完整静态对象为1904B（Camera952、USB476、Storage476）；本轮正式静态RAM增量0，不提前实例化业务对象。原五条消息队列和三条生命周期队列的深度及Map符号大小不变。

未注入故障的调用图：

| 接口 | O0局部栈/已知深度 | O2局部栈/已知深度 |
|---|---|---|
| App_LifecycleRxPromote | 144B / 200B | 152B / 200B |
| App_LifecycleTxFlush | 24B / 48B | 16B / 24B |
| App_LifecycleRxFlushReply | 24B / 72B | 24B / 64B |

正式任务栈大小、调用路径均未修改，当前任务栈增量0。未来接入需要把工具调用、发送回调和中断叠加重新评估；上述Flush已知深度不包含完整间接回调。没有实板测量新增调用后的栈余量，不宣称未来任务栈已验收。

## 检查命令

```powershell
python Tests/CameraLifecycleC2_1_1/run_tests.py
python Tests/CameraLifecycleC2_1_1/run_legacy.py
python Tests/CameraLifecycleC2_1_1/run_regression.py
python Tests/CameraLifecycleC2_1_1/run_build.py
python Tests/CameraLifecycleC2_1_1/audit_scope.py
git diff 182c84e..HEAD
```

测试/构建会生成本轮独立output和更新本目录结果。audit_scope为固定基线验收工具；它不是脱离基线任意运行的通用CI。所有原output保留，首次正式构建原产物备份在output/CameraLifecycleC2_1_1/build/previous_output/。构建与测试产物不提交。

## 边界与未验证事项

1. 未接入业务资格、START/STOP、帧归还核验，不进入C2.2。
2. 无实板验收；当前是ARMCC5/Unicorn软件、真实队列路径和正式构建验收。
3. api_busy防同对象回调重入，不是跨任务锁；对象必须由一个所属任务串行管理。现有消息运输检查真实任务身份，新普通C工具不依赖RTOS任务类型。
4. 接收端不能从不变的消息字段独立证明对端已经消费回复；依赖受控Tx单未完成请求契约。绕过接口、直接改结构、恶意任务或状态丢失不在保证范围。
5. QUARANTINED没有自动恢复或清理接口；协议违例保留现场，不替业务释放资源。
6. 编号耗尽不回绕；会话重建、任务重启或单核热复位恢复尚未实现。
7. 历史19组失败/不兼容保持，不属于本轮已修复问题。
8. 完成后仅提交、推送本独立分支，不合并main。
