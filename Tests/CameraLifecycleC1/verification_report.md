# C1 生命周期协议基础验收报告

## 1. 基线与范围

基线35b4fb7f5c39f7339b0bf0aa2a68594fd9748e7f，分支codex/stage-C1-lifecycle-protocol。开始时main一致，无已跟踪本地修改；未跟踪output保留。用户要求的项目图已作为聊天图片生成，不混入C1代码提交。

只实现普通C协议、三条静态消息通道、实际任务发送权限、身份匹配、待发及回复缓存基础。没有切换Camera/USB/Storage START/STOP；没有消费者资格表、硬件启停、超时回收或新帧租约。

## 2. 实际修改文件

| 文件 | 原因 |
|---|---|
| CM7/Application/app_camera_lifecycle.c/.h（新增） | 固定56B协议、匹配、无回绕编号、待发重试、重复请求缓存 |
| CM7/Application/app_messages.c/.h | 原实现完整保留，在末尾增加独立类型通道和Framework资源准备 |
| MDK-ARM/h7_mac.uvprojx | 只登记新CM7 C文件 |
| Docs/Design/C1_Camera生命周期协议基础.md | 字段、权限、完成语义、C1边界和后续约束 |
| Tests/CameraLifecycleC1/ | 专项、原通道基线比较、回归驱动、构建日志、内存与范围结果 |

Camera、USB、Storage、Bridge、Dispatcher、Media/FIFO、Control、Device、Service、Platform/BSP、HAL/LL及历史测试均未修改。无新Task，不调整优先级、栈、原队列深度、BOOT或视频配置。

## 3. AppMessage影响预先报告与内存实测

实施前说明：原AppMessage_t为52B，追加32B会变为84B，旧5×4项队列额外640B，并增加现有局部变量及消息复制。为避免该影响，使用独立56B协议，旧AppMessage_t和原5条通道保持原样。

| 项目 | 基线 | C1 |
|---|---:|---:|
| 原消息大小 | 52B | 52B |
| 原队列数据区 | 1040B | 1040B |
| 原队列控制块 | 360B | 360B |
| 原队列句柄 | 20B | 20B |
| 新3×4项数据区 | 0 | 672B |
| 新控制块 | 0 | 216B |
| 新句柄 | 0 | 12B |
| 总RW＋ZI | 100472B | 101376B |

**RAM增加904B**，其中新队列900B，包含布局对齐后总增量904B。正式CM7 MAP核对通过。后续每个调用者自备Pending/Replay记录的内存尚未接入业务，不把未来内存计作本阶段已分配资源。

## 4. 专项实际执行

**419/419 PASS，0 FAIL。**

| 组 | 数量 | 验证内容 |
|---|---:|---|
| 普通C协议 | 70 | 两消费者、六操作、请求/回复；版本、会话、代次、请求ID、关联ID、完成权限、编号耗尽、重试、缓存 |
| 原通道基线/候选 | 60 | 五条原通道发送/接收矩阵，52B复制，四项FIFO、满队列、空队列、通知失败，完整轨迹相等 |
| 新真实队列 | 49 | 部分初始化失败、上下文、双向复制、独立性、满队列重试、通知失败不重复提交 |
| 实际任务权限 | 240 | 24种合法消息×五个任务×收发两种场景，拒绝Control/IPC及冒充 |

执行真实FreeRTOS queue.c/list.c，不复制队列算法；调度、任务身份、通知及失败注入是桩，不运行真实调度器。全部测试编译链接无Warning，不把协议标志当真实资源证明。

results.json包含每项名称和结果。队列事件中的实际Frame Request/Return不改格式，也没有从协议工具调用Media Release或Camera Stop。

## 5. 当前及历史回归

28组在基线与候选隔离快照执行；9组有效回归共688/688 PASS：B3 51、B2 102、B2集成19、B1 39、Bridge/Storage集成11、Media 6、EP0 IRQ 4、Strings 8、IPC 448。

专项419＋有效回归688＝**1107项PASS**。其余19组原有失败或不能编译/链接，与基线逐项一致，新增失败0；不能宣称全仓全绿。

| 组 | 当前实际结果 | 与基线 |
|---|---|---|
| B3Behavior | {'total': 51, 'passed': 51, 'failed': 0} | 一致 |
| B2Behavior | {'total': 102, 'passed': 102, 'failed': 0} | 一致 |
| B2Integration | {'total': 19, 'passed': 19, 'failed': 0} | 一致 |
| B1Behavior | {'total': 39, 'passed': 39, 'failed': 0} | 一致 |
| B1BridgeStorage | {'total': 11, 'passed': 11, 'failed': 0} | 一致 |
| B1Media | {'total': 6, 'passed': 6, 'failed': 0} | 一致 |
| CameraApp | 未生成结果，编译/链接失败 | 一致 |
| CameraContext | 未生成结果，编译/链接失败 | 一致 |
| CameraStream | 未生成结果，编译/链接失败 | 一致 |
| CameraDeviceLifecycle | 未生成结果，编译/链接失败 | 一致 |
| JpegMedia | 未生成结果，编译/链接失败 | 一致 |
| JpegDevice | 未生成结果，编译/链接失败 | 一致 |
| ContinuousMedia | 未生成结果，编译/链接失败 | 一致 |
| Bridge | 未生成结果，编译/链接失败 | 一致 |
| NativeIntegrated | 未生成结果，编译/链接失败 | 一致 |
| NativeTest | 未生成结果，编译/链接失败 | 一致 |
| Core | {'total': 204, 'passed': 201, 'failed': 3, 'assertions': 2947} | 一致 |
| EP0IRQ | {'total': 4, 'passed': 4, 'failed': 0} | 一致 |
| Probe | {'total': 89, 'passed': 0, 'failed': 89} | 一致 |
| Cancellation | {'total': 39, 'passed': 0, 'failed': 39} | 一致 |
| Payload | {'unique_cases': 62, 'executions': 124, 'passed': 122, 'failed': 2} | 一致 |
| UvcLifecycle | {'total': 40, 'passed': 34, 'failed': 6} | 一致 |
| EP1 | {'total': 45, 'passed': 0, 'failed': 45} | 一致 |
| Streaming | {'total': 36, 'passed': 1, 'failed': 35} | 一致 |
| Descriptors | {'total': 8, 'passed': 7, 'failures': 0, 'errors': 1} | 一致 |
| Strings | {'total': 8, 'passed': 8, 'failed': 0} | 一致 |
| VideoConfig | 未生成结果，编译/链接失败 | 一致 |
| IpcStatus | {'total': 448, 'passed': 448, 'failed': 0} | 一致 |

11组未成功编译/链接，8组产生原有失败或错误。未修改旧断言、未恢复历史测试入口、未将缺失夹具视为通过。详情及第一诊断在regression_results.json。回归驱动检查实际工作区未被历史测试修改。

## 6. 正式全量构建

| Target | Error | Warning | Code / RO / RW / ZI（B） |
|---|---:|---:|---|
| h7_mac_CM7 | 0 | 0 | 136802 / 3086 / 1328 / 100048 |
| h7_mac_CM4 | 0 | 0 | 26302 / 1438 / 936 / 3576 |

原输出已备份到output/CameraLifecycleC1/build/previous_output。正式输出目录、名称和TargetOption不变。两核AXF/MAP存在；CM4生成HEX，CM7原配置不生成HEX，本轮不改开关。日志在本目录h7_mac_CM7_rebuild.log及h7_mac_CM4_rebuild.log。新文件参与CM7编译，Framework只准备新队列。

## 7. 审计

原app_messages.c全体字节作为候选前缀保留；旧header结构与API前缀保留。项目移除唯一新C文件登记后与基线逐字节一致；CM4 Target完全一致，CM7全部TargetOption一致。受保护源码及历史断言无差异。普通C协议无RTOS或硬件依赖，业务模块无App_Lifecycle调用。

范围检查与软件/硬件验收分开，scope_audit.json不被计为运行测试。

## 8. 尚未实现与风险

- 资格表、真实session分配、资格生效/释放证明、业务START/STOP、Shutdown/Restart和超时策略尚未接入。
- ReplayRetire只提供已终结缓存退休基础，不证明对端已消费；C2必须定义旧会话/代次拒绝和安全退休点，不能提前遗忘有效重试。
- Pending只允许单所有者调用；未提交重试保留原文，已提交请求等待回复。C1不提供因超时强清的接口。
- flags校验是声明权限检查，不证明硬件、文件或帧安全。C2必须读原账本，不可读Debug作释放证据。
- 三项C0业务选择已记录但未执行，尤其NONE当前仍是旧启动行为。
- 管理员Camera Shutdown命令接入留待后续，不声称三条消费者通道已经支持Control总控。
- 旧全局STOP可能导致Storage无帧超时、异常AVI未正常封尾的问题保持原状。
- 无真实硬件验收，不把模拟器PASS视为视频、SD或Stop/Restart实板通过。

## 9. 实板复核清单（待用户执行）

C1不需要发送新协议业务命令。可用正式CM7 AXF及配套CM4复核：

- [ ] 完整断电冷启动，Framework初始化成功，五个任务和BOOT正常。
- [ ] 原USB枚举及视频正常；实际FPS与相同配置基线比较。
- [ ] 原Storage手动抓拍/录像、明确STOP后文件可打开，租约归还正常。
- [ ] IPC心跳与OLED正常，观察栈余量，无新增任务或高频扫描。
- [ ] 不向尚无业务接收处理的新通道手工灌消息来期待采集启停。

## 10. 交付

仅提交推送本独立分支，不合并main；等待审核后再进入C2。完整Diff由基线到分支的Git比较提供，不提交固件、MAP、原输出副本或一次性补丁。
