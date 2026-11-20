# P1-C1 — 完整软件验证报告

VERIFIED（软件/源码）：基线 `3305d4a729e965247b33c95e5558eac576f1c6ef`，最终源码 tag `c1-final05`。所有旧断言保留；原1868个 Tests 跟踪文件与基线逐字节相同，17个历史入口的818条结果记录（含导入/多报告重复）逐项状态及诊断完全一致，没有新增失败、缺失或伪造成功。不是818个互不重复的新用例。

NOT_TESTED：本轮未烧录、未插拔、未抓 USBPcap。以下 PASS 全部属于指定模型上的实际 ARM 程序、源码或构建证据，不是硅片静止证明。

## 分层专项结果

| 层级 | 用例 | 断言 | 结果与报告 |
| --- | --- | --- | --- |
| 独立真实 HAL Hook（不依赖 BSP Stop） | 19 | 130 | VERIFIED PASS；`Tests/UsbBulkOutLifecycle/reports/hooks.json` |
| C1 BSP / Platform / HAL / LL 集成停止 | 57 | 925 | VERIFIED PASS；`reports/stop.json` |
| 原 Bulk IN | 38 | 796 | VERIFIED PASS；`reports/in-baseline-c1-final05.json` |
| 原 P1-B.1 集成 | 38 | 362 | VERIFIED PASS，同上 |
| 原 C0 HAL 观察（未绑定默认路径） | 24 | 99 | VERIFIED PASS；`reports/c0-c1-final05.json` |

VERIFIED：独立19项先成功结束，再执行停止专项。真实 HAL/LL 使用 ARMCC 编译并由 Unicorn 执行；BSP/Platform 专项直接包含授权生产源码。初始化夹具表示已完成硬件 Init 的已断开 READY 边界，随后执行真实 Enable/Start/Open；没有测试真实 RCC/PHY 上电。模型只更新寄存器、队列及延迟事件，不写 owner/refcount/proof 来制造成功；计数器耗尽和初始状态是明确故障输入。

VERIFIED：每个新用例的输入 op、故障、预期/实际、资源变化、原始 MMIO 读写、失败原因和源哈希均保留在两个 JSON；编译 argv/exit/output 在其中及 `reports/compile.json`。RX pop 检查一次性消费；事件由真实 W1C 前 Hook 取得；关闭 NAK 自动成功、延迟 Disable/解除、注入 Reset 均可得到故障而非错误归还。

## C1用例清单

| ID | 场景 | 最终结果 |
| --- | --- | --- |
| C1-01 | never-armed idle Stop | VERIFIED PASS |
| C1-02 | active EP2 cancel | VERIFIED PASS |
| C1-03 | queued 31-byte packet drained before stop | VERIFIED PASS |
| C1-04 | RXFLVL pending cannot retire before service | VERIFIED PASS |
| C1-05 | normal completion before Stop | VERIFIED PASS |
| C1-06 | normal XFRC wins race after fence | VERIFIED PASS |
| C1-07 | XFRC and EPDISD in same snapshot | VERIFIED PASS |
| C1-08 | normal callback rearm then cancel new identity | VERIFIED PASS |
| C1-09 | submission fence rejects next Receive | VERIFIED PASS |
| C1-10 | EP0 SETUP during target stop | VERIFIED PASS |
| C1-11 | other OUT completion remains normal | VERIFIED PASS |
| C1-12 | shared FIFO other OUT Buffer preserved | VERIFIED PASS |
| C1-13 | ISO claims lease before Bulk | VERIFIED PASS |
| C1-14 | ISO joins Bulk lease | VERIFIED PASS |
| C1-15 | Bulk leaves valid ISO lease held | VERIFIED PASS |
| C1-16 | unrelated EPDISD cannot clear lease | VERIFIED PASS |
| C1-17 | unknown hardware NAK not stolen | VERIFIED PASS |
| C1-18 | GONAKEFF timeout | VERIFIED PASS |
| C1-19 | disable timeout | VERIFIED PASS |
| C1-20 | EPENA clear without retired interrupt insufficient | VERIFIED PASS |
| C1-21 | Reset retains old Buffer | VERIFIED PASS |
| C1-22 | Disconnect retains old Buffer | VERIFIED PASS |
| C1-23 | repeat Stop and Commit | VERIFIED PASS |
| C1-24 | forged transfer rejected | VERIFIED PASS |
| C1-25 | ISR cannot Begin Stop | VERIFIED PASS |
| C1-26 | overflow veto preserves Buffer and other packet | VERIFIED PASS |
| C1-27 | corrupt HAL pointer veto | VERIFIED PASS |
| C1-28 | Reset during actual PIO copy | VERIFIED PASS |
| C1-29 | stalled clock bounded by polls | VERIFIED PASS |
| C1-30 | Attempt exhaustion fences old transfer | VERIFIED PASS |
| C1-31 | lease exhaustion retains Buffer | VERIFIED PASS |
| C1-32 | transfer sequence exhaustion no submission | VERIFIED PASS |
| C1-33 | same-attempt recovery from late NAK | VERIFIED PASS |
| C1-34 | same-attempt recovery from late Disable | VERIFIED PASS |
| C1-35 | old Commit cannot retire newly armed transfer | VERIFIED PASS |
| C1-36 | late raw XFRC cannot complete active new transfer | VERIFIED PASS |
| C1-37 | late direct callback cannot retire new transfer | VERIFIED PASS |
| C1-38 | IN progresses during OUT stop | VERIFIED PASS |
| C1-39 | masked RXFLVL cannot produce NAK proof | VERIFIED PASS |
| C1-40 | EP0 control data uses guarded Buffer | VERIFIED PASS |
| C1-41 | release command is not release completion | VERIFIED PASS |
| C1-42 | late release completion recovers original attempt | VERIFIED PASS |
| C1-43 | legacy Close Abort Flush Stop cannot bypass proof | VERIFIED PASS |
| C1-44 | flash destination rejected before HAL submit | VERIFIED PASS |
| C1-45 | enable cannot reset live controller | VERIFIED PASS |
| C1-46 | ISO inactive disable cannot fake completion | VERIFIED PASS |
| C1-47 | ISO cannot steal releasing lease | VERIFIED PASS |
| C1-48 | lost NAK barrier cannot revive | VERIFIED PASS |
| C1-49 | Reset between W1C and callback | VERIFIED PASS |
| C1-50 | EP0 zero status with Platform preparation | VERIFIED PASS |
| C1-51 | EP0 zero status with HAL setup receiver | VERIFIED PASS |
| C1-52 | late opt-in after legacy session refused | VERIFIED PASS |
| C1-53 | XFRC waits for actual remaining RX consumption | VERIFIED PASS |
| C1-54 | Stop takes over deferred normal completion | VERIFIED PASS |
| C1-55 | EP0 cannot replace a still-owned receive | VERIFIED PASS |
| C1-56 | new SETUP retains unfinished EP0 data owner | VERIFIED PASS |
| C1-57 | single-EP RX Flush remains denied after Commit | VERIFIED PASS |

## 17个原有历史入口

VERIFIED：按入口和用例对照 `reports/case_comparison.json`，不是仅比较退出码。总入口仍为6成功、11失败；所有历史 FAIL 保留。下表“记录”包括从原 JSON 观察的重复结果，不作为新增覆盖量。

| 入口 | 退出码（基线→当前） | 结果记录 PASS / FAIL | 判断 |
| --- | --- | --- | --- |
| UsbCoreDecoupling/run_tests.py | 1 → 1 | 196 / 8 | VERIFIED 与基线相同 |
| UvcLifecycle/run_tests.py | 1 → 1 | 34 / 6 | VERIFIED 与基线相同 |
| UvcStreaming/run_tests.py | 1 → 1 | 1 / 35 | VERIFIED 与基线相同 |
| UvcPayload/run_tests.py | 1 → 1 | 122 / 2 | VERIFIED 与基线相同 |
| UvcEp1Lifecycle/run_tests.py | 1 → 1 | 0 / 45 | VERIFIED 与基线相同 |
| UvcProbeCommit/run_tests.py | 1 → 1 | 0 / 89 | VERIFIED 与基线相同 |
| UvcProbeCommit/run_cancellation_tests.py | 1 → 1 | 0 / 128 | VERIFIED 与基线相同 |
| Ep0IrqRace/run_race.py | 0 → 0 | 4 / 0 | VERIFIED 与基线相同 |
| UsbUvcDescriptors/run_tests.py | 1 → 1 | 16 / 0 | VERIFIED 与基线相同 |
| UvcUsbCleanup/run_tests.py | 1 → 1 | 0 / 14 | VERIFIED 与基线相同 |
| MscDeferredStatus/run_tests.py | 0 → 0 | 17 / 0 | VERIFIED 与基线相同 |
| MscProtocol/run_tests.py | 0 → 0 | 41 / 0 | VERIFIED 与基线相同 |
| MscBlockAdapter/run_tests.py | 0 → 0 | 18 / 0 | VERIFIED 与基线相同 |
| MscSdIntegration/run_tests.py | 0 → 0 | 24 / 0 | VERIFIED 与基线相同 |
| MscEnumeration/run_tests.py | 1 → 1 | 0 / 0 | VERIFIED 与基线相同 |
| MscEnumeration/run_integration.py | 1 → 1 | 0 / 0 | VERIFIED 与基线相同 |
| MscSenseCompat/run_tests.py | 0 → 0 | 18 / 0 | VERIFIED 与基线相同 |

VERIFIED：Core 109/110/201/334/335/361/362/364 原8项 FAIL 仍保留；未放宽旧同步语义。UVC Payload case37 在 O0/O2 的2项 FAIL 原样保留（62个场景、124次执行，122 PASS/2 FAIL）。Probe cancellation 记录包含导入时先执行的89项以及新增39项。Descriptor入口原首段缺 `camera_mjpeg_fifo.h`，后续8个字符串测试成功且写入两个报告，故表中16条记录不是16个独立测试。MSC Enumeration两个旧入口仍分别有11/8个未定义符号链接错误，业务用例未运行，不能报PASS。

## 三配置完整编译与最终固件

VERIFIED：使用工程既有 `Tests/MscReconnect/build.py`，仅在独立快照中生成临时工程及恢复 RTE。原 Keil工程/选项字节不变。编译器实际版本：

```
Product: MDK Plus 5.32
Component: ARM Compiler 5.06 update 7 (build 960)
Tool: armcc [4d365d]
For support see http://www.arm.com/support
Software supplied by: ARM Limited
```

| 配置 | BOOT / SD / WRITE 最终预处理值 | 结果 | AXF SHA256 |
| --- | --- | --- | --- |
| ram | 1 / 0 / 0 | VERIFIED 0 Error(s), 0 Warning(s) | `87f4c7cca36a89da87763544a4a3152b21eba6e9a4ce6276f5cd52e8b78ba30b` |
| sd_readonly | 1 / 1 / 0 | VERIFIED 0 Error(s), 0 Warning(s) | `1fe2afc28bf0eb6eb25b14f708fdb1a970bf0644716a1ee7c786dbaf7d5689cf` |
| uvc | 0 / 0 / 0 | VERIFIED 0 Error(s), 0 Warning(s) | `9cfc669fbdb2f6ec2fa9af4c0a60e8870f2761d3bcde9e95240a8c1649ca5ed1` |

VERIFIED：AXF、HEX、MAP 保留于 `output/MSC24/udlm-p1c1/build-c1-final05/output/MSC41/diagnostic-build/` 的各配置目录。完整日志副本为 `reports/firmware-c1-final05-<mode>.log`；命令行见 `reports/build-c1-final05.json`，文件哈希/大小、源快照逐字节匹配及 ELF 实际符号见 `reports/build_artifacts.json`。

VERIFIED：三配置 ELF 含 HAL IRQ 和 managed RX/NAK Hook 实现；未调用的 Enable/Begin 公共API被链接器移除。应用没有自动启用 C1，不能以 Hook 符号证明已绑定或声称生产 BOT 调用链已迁移。已启用的机制执行链由独立实际 ARM 集成专项证明；未来自动启用须配套 Class/Task/Worker 生命周期审批。

## 首次失败、中间检查与原证据

VERIFIED：`hooks-history-e36702168b15.json` 保留首轮 HOOK-11 FAIL，原因是新夹具操作号与复用 C0 初始化操作相撞；改用独立 op，不改旧 C0。

VERIFIED：`stop-history-49e1d9024247.json` 保留首轮48项 FAIL。主因是把既有 Receive 的短 NVIC 锁累计次数误当“停止跨轮关闭整个 IRQ”；修正 NVIC ISER 模型，断言为每阶段返回 IRQ 启用、PRIMASK恢复及活跃 Stop 各阶段不改变 NVIC。EP0 DAINT 路由最初未打开；Reset-copy输入最初排在NAK marker后而未真正触发copy；Attempt耗尽最初错误期待成功，现按拒绝/保留资源验证。这些是本轮新夹具的修正，原76、原24及17入口断言/源码未改。初始报告仍可查看实际失败字段，不以删除失败制造全绿。

VERIFIED：final01/02/03/04 及更多 hooks/stop-history 均保留；final05包含最后的只读EP0/ISO owner查询保护及新增HAL空白行整理。中间检查不冒充最终源码验证。`c0-intermediate-unbound.json` 保留早期直接调用旧C0 runner产生的结果，其 `production_modified` 是旧阶段的固定元数据；本轮HAL确实修改，真实源哈希以当前报告为准。旧 canonical C0 report已恢复本轮误改且保持基线原字节。

## 安全性判断与停点

VERIFIED（软件）：新提交 fence、唯一 lease、真实 copy 前检查、W1C事实、FIFO barrier、正常完成延迟交付、同身份 Reconcile、失败 Buffer/busy 保留均有专项；单EP RX Flush不使用，Stop不会长期关闭USB IRQ，没有强清资源或自动重启PCD。

INFERRED：硬件步骤与RM条件对应，但真实设备最坏时延和所有异步交错不能由模型证明。UNRESOLVED：完整EP0交叠成功恢复、全部ZLP、实际core ID及Class自动迁移；详见 [尚未证明条件](USB_BulkOUT_C1_RemainingRisks.md)。NOT_TESTED：所有板级测试、真实SD、MSC整机重连。本轮不宣布完整硬件验收通过，不进入BOT迁移。
