# C1.1 Camera Lifecycle Protocol 可靠性整改验收报告

## 基线与结论

- main：`35b4fb7f5c39f7339b0bf0aa2a68594fd9748e7f`。
- 整改基线 C1：`b684b76914b46da20314b27235f44b7d5d214fd5`。
- 整改分支：`codex/stage-C1-1-lifecycle-reliability`。
- 结论：本轮软件整改通过；未合并 main，未实施 C2，未执行实板验证。
- 原 C1 419/419、新增协议交错 24/24、回归门禁自测 29/29、有效历史回归 688/688。历史失败继续存在，不宣称全仓全绿。

## 实际修改与接口契约

### Replay 安全退休

调用关系：ReplayBegin登记请求；ReplayComplete写入终结回复；ReplayGetReply只读；ReplayRetire不再清除任何记录。对生产源码的调用检索确认，这些工具尚未接入Camera、USB、Storage现行业务路径。

| 条件 | ReplayRetire结果 | 记录变化 |
|---|---|---|
| 空指针 | SYS_ERR_INVALID_ARG | 无 |
| 未占用或尚未完成 | SYS_ERR_BUSY | 无 |
| 已占用且完成 | SYS_ERR_NOT_SUPPORTED | 无 |

completed、ACK入队成功、等待超时都不能证明旧请求失效。没有安全退休边界时不释放缓存；缓存耗尽拒绝新事务，不能淘汰旧记录。没有引入tombstone、水位或会话管理框架。未来业务接入前仍需设计并审核安全失效边界。

### READY取消

新增`App_LifecycleCancelReady(AppLifecyclePending_t *)`：仅READY且kind为REQUEST时清零待发记录并返回SYS_OK；空指针返回INVALID_ARG；其他阶段以及READY回复返回INVALID_STATE且保持内容。取消不发送消息，不更改调用者身份或ID分配器，不释放任何业务资源。WAIT_REPLY仍不能按超时清除。

取消后显式重新Stage同一请求属于新的本地提交尝试；原请求从未入队，测试检查最终只入队一次。已提交事务不能走此接口。

### 回归门禁

`run_regression.py`使用全新隔离源码快照，默认显式比较C1基线，支持`--baseline`。历史测试源码不改；候选快照仅覆盖4个协议/消息文件和原工程注册。

`regression_compare.py`使用实际id/case/scenario/name及执行变体构建完整ID集合。缺失PASS或FAIL用例、重复ID、未知结果、异常统计、回归失败均使门禁失败；新增用例和新增套件列入结果，新增失败不能放过。报告行重排不影响比较。不使用zip截断比较，也不将双方退出0作为通过证明。无用例报告但退出0直接失败。不可编译历史夹具保留错误指纹并逐组比较。

29项自测覆盖缺失、重排、重复、增减、变体、非法结果、统计冲突、无报告、套件变化及诊断变化。

### 文件编码

app_messages.c/.h原main部分为UTF-8，C1追加部分使用GBK，原文件出现混合编码。本轮转换为完整UTF-8；审计按原始两段编码还原并逐字比较，正文、注释、声明、队列和业务语句均未变化。

## 专项测试

实际编译当前协议及真实FreeRTOS queue.c/list.c，在ARMCC5/Unicorn环境运行；任务身份、调度器环境为测试桩。不是硬件验收。

原419项均保留。原case56要求completed后退休成功，与本轮明确禁止退休的新契约矛盾，因此只将这一项收紧为NOT_SUPPORTED，并新增缓存回复不变断言；没有删除场景或放宽断言。

| 新增ID | 场景 | 结果 |
|---:|---|---|
| 0 | ACK not enqueued cannot retire | PASS |
| 1 | ACK queued unconsumed duplicate | PASS |
| 2 | Retire refused old request still duplicate | PASS |
| 3 | New session old request guarded | PASS |
| 4 | New generation late ACK | PASS |
| 5 | READY cancel repeat and retransmit | PASS |
| 6 | WAIT_REPLY cancel and force clear refused | PASS |
| 7 | SENDING cancel refused then READY cancel | PASS |
| 8 | COMPLETE cancel refused | PASS |
| 9 | COMMITTED cancel refused | PASS |
| 10 | Pending reply cannot be cancelled | PASS |
| 11 | NULL cancel | PASS |
| 12 | Empty and duplicate cancel | PASS |
| 13 | Full request queue cancelled request absent | PASS |
| 14 | Full ACK queue duplicate request reply retry | PASS |
| 15 | Bounded replay pool full no eviction | PASS |
| 16 | Failure cache cannot become success | PASS |
| 17 | Old request ID ACK rejected | PASS |
| 18 | Cancel leaves identity counter and notifications unchanged | PASS |
| 19 | Cancelled unsubmitted request sends once | PASS |
| 20 | Invalid and in-progress retirement | PASS |
| 21 | Session and generation mismatch | PASS |
| 22 | Duplicate completion unchanged | PASS |
| 23 | BUSY reply remains terminal outcome | PASS |

Replay容量耗尽测试使用测试拥有的两个静态记录，不意味着生产层新增全局Replay池。新session/generation测试验证当前匹配工具拒绝旧身份，未实现C2会话管理。

## 28组历史回归逐项比较

下表基线与候选均重新执行。有效全通过组共688项，另19组保留历史失败或不可构建诊断。完整用例ID、失败行、缺失/新增记录与诊断见regression_results.json。

| 测试组 | 候选摘要 | 相对C1 |
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

本轮C1比较：新增失败0、缺失套件0、缺失用例0、候选工作区被测试改变文件0。测试输出原样保留。

### 与main的额外差异，不掩盖历史问题

另行将main@35b4fb7与本轮候选比较，严格门禁报告NativeIntegrated、NativeTest两组历史夹具新增`App_LifecycleValidate`未解析链接依赖。C1@b684b76原始日志已存在这两项依赖；相对C1的本轮重跑完全一致。这是原C1回归脚本只比较失败退出状态而遗漏的历史差异，不能宣称main到C1所有失败诊断均相同。本轮不修改其他阶段的历史夹具。详见main_comparison_results.json。

## 编译与内存

使用原正式Keil工程完整重编；TargetOption、输出路径、文件名、任务栈不改。旧正式构建输出已先备份到output/CameraLifecycleC1_1/build/previous_output/，其他原output内容保留。

| Target | Error | Warning | Code | RO | RW | ZI |
|---|---:|---:|---:|---:|---:|---:|
| CM7 | 0 | 0 | 136802 | 3086 | 1328 | 100048 |
| CM4 | 0 | 0 | 26302 | 1438 | 936 | 3576 |

CM7正式设置不生成HEX，AXF与MAP存在；CM4生成AXF/MAP/HEX。本轮没有为了验收改变输出设置。构建日志与SHA256见build_results.json及两份rebuild.log。

- 原AppMessage_t仍52B，原5条队列深度均4。
- 独立AppLifecycleMessage_t仍56B，新3条队列深度均4。
- 原队列控制块/消息区/句柄：360/1040/20 B。
- 生命周期队列控制块/消息区/句柄：216/672/12 B。
- CM7 RW+ZI=101376 B，相对C1增量0 B，相对main累计增量904 B（原C1引入）。
- 不新增任务、不调整任务栈、队列容量或结构字段。

## 修改范围

| 文件 | 原因 |
|---|---|---|
| CM7/Application/app_camera_lifecycle.c/.h | 封闭退休成功路径，新增READY请求取消 |
| CM7/Application/app_messages.c/.h | 仅统一UTF-8编码 |
| Tests/CameraLifecycleC1/protocol_test.c | 保留case56并收紧退休及缓存断言 |
| Tests/CameraLifecycleC1/run_tests.py、results.json | 独立输出目录支持、更新case56说明与实测结果 |
| Tests/CameraLifecycleC1/run_regression.py、regression_compare.py | 完整ID集合及历史失败诊断比较 |
| Tests/CameraLifecycleC1_1/ | 24项真实队列交错、29项门禁自测、结果、构建/范围审计及报告 |
| Docs/Design/C1_Camera生命周期协议基础.md | 同步退休禁用与READY取消契约 |

未修改现行Camera、USB、Storage业务START/STOP、Media/FIFO、帧租约、UVC、BSP、HAL、CM4、任务或Keil配置。scope_audit.json记录字节/语义逆向核对与MAP检查。

## 复核命令与交付

```powershell
python Tests/CameraLifecycleC1_1/run_tests.py
python Tests/CameraLifecycleC1_1/test_regression_compare.py
python Tests/CameraLifecycleC1/run_regression.py
python Tests/CameraLifecycleC1_1/run_build.py
python Tests/CameraLifecycleC1_1/audit_scope.py
git diff b684b76..HEAD
```

上述测试和构建脚本会更新自身结果及output；并非只读审查工具。run_build会完整重编并备份首次既有输出。最终累计Diff使用Git提交对比链接交付，不将二进制或output提交仓库。

## 剩余风险与待验证

1. Replay安全退休仍未实现，当前明确拒绝；有限记录不能无限复用，后续接入须先审核失效边界。
2. 协议completion_flags只约束声明权限，不是帧已归还、文件已关闭或硬件已停止的证明。
3. C1新通道尚未切换业务路径，消费者资格和真实会话生命周期留待C2审核。
4. 本轮未烧录、未做实板事件交错或硬件生命周期验证，模拟PASS不等于实板通过。
5. 历史19组失败/构建不兼容及上述两组C1继承链接差异继续保留，不属于本轮已修复问题。

完成后仅提交并推送本独立分支，等待人工审核；不合并main。
