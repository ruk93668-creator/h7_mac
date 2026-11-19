# USB Endpoint Lifecycle P1 软件验证报告

> 起点：`9d38e56966f8ea5da9ee63285b037747613c47d5`；2026-11-19；仅新worktree。
> **P1整体：未通过/受阻。下面PASS仅限明确的基线构建或缺陷刻画，不代表新停止实现通过验收。**
> VERIFIED=实际运行/源码证据；INFERRED=设计；UNRESOLVED=无法证明；NOT_TESTED=未执行/无实板验证。

## 1. 运行环境与隔离

**VERIFIED（软件）**：ARM Compiler 5.06 update7 build960 / MDK Plus5.32；版本输出文本在 `Tests/EndpointLifecycle/reports/compiler.log`。Unicorn2.1.4、pyelftools0.33隔离安装在ignored python_deps；未改全局依赖。既有脚本按原命令/断言运行于复制的基线快照，未覆盖历史结果、原Keil配置或任何生产源。具体命令/结果在 `baseline-regression.json`、`build_results.json` 和 `hal_contract.json`。

**VERIFIED**：源码基线不变，不能称“新P1固件编译通过”；本轮未产出Endpoint Lifecycle生产改动。允许的BSP/Platform修改尚因P1-A证据门禁未实施，详ImplementationReport。

## 2. 三配置完整CM7构建

**VERIFIED（构建）**：采用现有 `Tests/MscReconnect/build.py`，在包含CM7/Board/Common/Drivers/Middlewares/MDK-ARM等跟踪输入的独立快照中执行完整Keil rebuild。三个配置均 `USB_DEVICE_LIFECYCLE_DIAGNOSTICS=1`；UVC配置是BOOT_MSC=0，不能误写成另测了诊断宏0。所有配置显式 `USB_MSC_SD_WRITE_ENABLE=0` 并保存预处理证据。没有烧录。

| 配置 | BOOT_MSC / SD_TEST / WRITE_ENABLE | Error / Warning | ELF SHA256 |
| -- | -- | -- | -- |
| ram | 1 / 0 / 0 | 0 / 0 | `ca8ccf2bc42526c3cb86c408999a428d32d0148930bbef9b8b8116009dc772ca` |
| sd_readonly | 1 / 1 / 0 | 0 / 0 | `48ffa7c192845857274efde1ba9f5d43dd1247da3f2b4f11427feb2500aa6d63` |
| uvc | 0 / 0 / 0 | 0 / 0 | `da58d9f02b767be93022f0a579acad62ecc66b03b99c33d95d65548361f11b01` |

**VERIFIED**：三构建日志的UTF-8/LF文本副本保存至 `Tests/EndpointLifecycle/reports/cm7_ram_build.log`、`cm7_sd_readonly_build.log`、`cm7_uvc_build.log`。ELF/HEX/MAP和预处理产物留在 `output/MSC24/udlm-p1/baseline-build-board-complete/output/MSC41/diagnostic-build/` 对应模式目录；构建产物不提交。

**VERIFIED（工具失败）**：首次隔离快照遗漏Board目录，导致3个 `board_config.h` include错误，0 Warning；这是本轮隔离工具缺陷，不计作工程本身无法构建。已补复制依赖并使用新目录重建，旧失败输出保存在 `initial_snapshot_ram_build.log`、`baseline-build.json` 及原隔离目录，没有删除。

## 3. 原有回归：17个入口全部执行

**VERIFIED（软件）**：6个入口exit0、11个入口exit非0。生产代码没有变化，旧测试源码/断言保持原样；下面是基线对照，不声称所有旧测试已通过。下表断言数是本次实际执行到的计数，失败后未执行的断言不计入；O0/O2两次执行分别计数。

| 原测试入口 | 执行数 | PASS | FAIL | 实际断言数 | 进程exit |
| -- | -- | -- | -- | -- | -- |
| `UsbCoreDecoupling/run_tests.py` | 204 | 201 | 3 | 2947 | 1 |
| `UvcLifecycle/run_tests.py` | 40 | 34 | 6 | 387 | 1 |
| `UvcStreaming/run_tests.py` | 36 | 1 | 35 | 80 | 1 |
| `UvcPayload/run_tests.py` | 124 | 122 | 2 | 503562 | 1 |
| `UvcEp1Lifecycle/run_tests.py` | 45 | 0 | 45 | 90 | 1 |
| `UvcProbeCommit/run_tests.py` | 89 | 0 | 89 | 178 | 1 |
| `UvcProbeCommit/run_cancellation_tests.py` | 39 | 0 | 39 | 78 | 1 |
| `Ep0IrqRace/run_race.py` | 4 | 4 | 0 | 110 | 0 |
| `UsbUvcDescriptors/run_tests.py` | 未形成用例结果 | 未形成用例结果 | 未形成用例结果 | 未形成用例结果 | 1 |
| `UvcUsbCleanup/run_tests.py` | 14 | 0 | 14 | 29 | 1 |
| `MscDeferredStatus/run_tests.py` | 17 | 17 | 0 | 243 | 0 |
| `MscProtocol/run_tests.py` | 41 | 41 | 0 | 380 | 0 |
| `MscBlockAdapter/run_tests.py` | 18 | 18 | 0 | 146 | 0 |
| `MscSdIntegration/run_tests.py` | 24 | 24 | 0 | 156 | 0 |
| `MscEnumeration/run_tests.py` | 未形成用例结果 | 未形成用例结果 | 未形成用例结果 | 未形成用例结果 | 1 |
| `MscEnumeration/run_integration.py` | 未形成用例结果 | 未形成用例结果 | 未形成用例结果 | 未形成用例结果 | 1 |
| `MscSenseCompat/run_tests.py` | 18 | 18 | 0 | 415 | 0 |

**VERIFIED**：UsbCoreDecoupling失败ID为109、110、201；UvcLifecycle失败ID为11、35–39；UvcPayload的ID37在O0/O2各失败一次。其余各失败ID、实际值/预期值和失败行保存在 `baseline-summary.json` 及 `baseline-regression.json`。没有删旧用例或调低断言。

**VERIFIED（脚本/构建错误）**：

- UsbUvcDescriptors主脚本exit1，摘要为7通过、1 setup error；缺少camera_mjpeg_fifo.h的测试include路径。其单独string子报告8/8通过，不能拿子报告覆盖主脚本失败。
- MscEnumeration两个旧入口链接失败，分别11和8个undefined-symbol错误，包含新增BOT入口未进入老夹具；没有实际形成可计数的用例结果。
- 多个UVC旧夹具在初始化/配置前提失败；本轮保留原始输入、断言及返回码，不在本任务改Camera/UVC实现来制造全绿。
- 原Ep0IrqRace已执行4个竞争场景，4/4通过、110条断言。`baseline-ep0-race.json` 是本次隔离快照输出副本，日志也独立保存。

**VERIFIED**：上述失败都在生产修改为0的起点重现；不存在可供对照的新P1生产实现，因此“新增生产回归失败”不适用，不能据此宣称P1没有功能退化。新实现未做的验收均在第5节列出。

## 4. 新增真实HAL/LL刻画：18/18，108条断言

**VERIFIED（软件测试）**：编译仓库原始 `stm32h7xx_hal_pcd.c`、`stm32h7xx_ll_usb.c` 与新增 `hal_contract_test.c`，没有替换被测Abort/Flush/Stop函数。Unicorn仅按用例注入MMIO读出的硬件状态。四个不相关链接依赖是有计数防护的stub，每个用例独立断言其调用次数为0。

测试专用 `HAL_USB_TIMEOUT=4U` 只用于缩短故障注入；生产LL默认宏未改。10000次Abort循环仍运行真实源码。此测试不测真实总线时间、实际IRQ延迟或硅片行为。

| 用例ID | 注入/输入 | 观察与期望（刻画现状） |
| -- | -- | -- |
| 0 /1 | Tx /Rx AHBIDL不置位 | HAL_EP_Flush返回OK，真实LL不能完成，重现隐藏错误 |
| 2 /3 | 相同故障直接调用公开LL | 返回HAL_TIMEOUT |
| 4 | PCD Stop内部Tx Flush无法完成 | HAL Stop仍返回OK且全局IRQ关闭 |
| 5 /6 | IN2 /OUT2 EPENA持续置位 | 轮询有界结束但返回HAL_ERROR；EPENA仍置位 |
| 7 /8 | IN2 /OUT2 EPENA已清且无EPDISD | HAL Abort返回OK，没有事件证明 |
| 9 /10 | EPENA在轮询中变0，未产生EPDISD | HAL Abort仍OK，不能单凭返回签发取消proof |
| 11 /12 | Flush /Stop的PCD Lock已锁 | HAL_BUSY；Stop尚未关闭全局IRQ |
| 13 | PCD Lock已锁，调用Abort | Abort不取HAL Lock，不能把它与Flush/Stop锁语义混同 |
| 14 | AHB空闲但Tx Flush命令不清 | wrapper仍OK，重现第二种隐藏timeout |
| 15 /16 | 模型按要求清Tx /Rx Flush命令 | 公开LL返回OK，完成位符合模型设定 |
| 17 | 预先残留XFRC /EPDISD | Abort返回OK但两方向旧INT均未退休 |

每项均记录输入、注入点、返回码、寄存器状态、独立断言及错误；原始结果见 `hal_contract.json`。buffer引用变化明确记为 **NOT_MODELED**，因为HAL内没有Endpoint Ledger；不能伪造资源归还计数。

**VERIFIED（测试工具自身问题）**：初始链接遇4个无关依赖，之后复用framework scatter产生1 Warning；均记录于 `harness_setup_failures.json`。最终使用带违规调用计数的链接stub和专用测试scatter，最终编译/链接0 Error/Warning。单元scatter不是生产linker修改。

## 5. P1要求的覆盖缺口

下列为 **NOT_TESTED / 未实现所以无法验收**，不因上面PASS而自动关闭：

| P1要求 | 当前证据边界 |
| -- | -- |
| 新Transfer Identity及各Endpoint状态转换 | 无生产实现；未运行新状态机用例 |
| 正常完成与取消竞争只归还一次 | 基线EP0局部竞争已测；新端点ledger/引用计数尚未实现 |
| OUT2/IN2活跃取消成功proof | 仅刻画真实HAL超时/无事件返回；未验证新成功取消算法 |
| 延迟EPDISD与Reconcile | 无新API，未验证迟到证据收尾 |
| FIFO失败保持资源且不签proof | wrapper失败隐藏已重现；新所有权保留机制未实现 |
| PIO续包与Abort并发 | 需要BSP admission fence/IRQ身份capture；尚未实施 |
| 旧generation迟到事件、新arm后旧IRQ | 需要退休与重开fence；尚未实施 |
| 重复Abort/完成/Free、buffer refcount、Recovery后Stop | 需要具名ledger和Reconcile；尚未实施 |
| ISO IN与EP0硬件边界 | 尚缺RM正文/实例与修订适用性；未验收 |
| 新Stop路径PCD失败/IRQ保留 | 未修改生产Stop，不能声称已修复 |

**NOT_TESTED（实板）**：全部板级项目，包括实际型号/REV_ID、USBPcap、插拔、真实EPDISD和DMA；没有烧录或执行任何SD写入。

## 6. 验收结论与复现

**VERIFIED**：基线三配置完整构建通过；17个原入口执行完成；新HAL/LL刻画18项/108断言通过；旧失败原样保留。

**UNRESOLVED**：P1停止实现、安全proof与完整竞态验收没有完成。原因是RM正文等硬件前置证据不足，不是生产修改未获授权。不得标记P1 PASS、不得进入P2。

复现入口见 `Tests/EndpointLifecycle/README.md`；每次baseline运行使用新的`--tag`保留证据目录。报告只提交文本/源码/JSON，不提交ELF/HEX/MAP、本机依赖或凭据。最终Diff应只包含文档和新增测试/证据，生产与旧测试应无变化。

**VERIFIED（日志保存）**：Git中的 `.log` 为便于审查的UTF-8/LF文本副本；转换前字节另存 `output/MSC24/udlm-p1/raw-text-evidence/`。`raw-log-manifest.json` 记录原始字节与文本副本SHA256；构建器实际原始日志也仍在各隔离构建目录。没有将换行规范化称为字节原样副本。
