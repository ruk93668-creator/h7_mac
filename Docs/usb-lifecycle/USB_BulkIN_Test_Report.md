> P1-B.1 更新：以下原 P1-B 内容保留为历史证据；Service Pending、纯 SOF 仲裁与受控 Reconcile 的当前结论以 [P1-B.1 最终验证](USB_P1B1_FinalVerification.md) 及其四份专题报告为准。五项旧同步断言未迁移，历史 FAIL 未删除，硬件验收仍 NOT_TESTED。

# P1-B 软件验证与构建报告

> 基线5a27af621c219ead2bc680858efd4aeb17e110a4。最终生产输入为p1b04快照；软件模型不是实板证明。VERIFIED注明来源；INFERRED为推断；UNRESOLVED为缺证据；NOT_TESTED为未执行。

## 1. 完整CM7构建

**VERIFIED（构建）**：ARMCC V5.06 update7 build960，沿用工程构建方法在隔离副本Rebuild；真实Keil/RTE文件不变，未下载固件。

| 配置 | Error/Warning | 预处理配置 | AXF SHA256 |
| -- | -- | -- | -- |
| ram | 0 Error(s), 0 Warning(s) | {'BOOT': '1', 'SD': '0', 'WRITE': '0'} | `f11460823022ad16e7ff5ec27318e1c5c1e7c2aa528cf29e4d14b48bf7cbd1dd` |
| sd_readonly | 0 Error(s), 0 Warning(s) | {'BOOT': '1', 'SD': '1', 'WRITE': '0'} | `639c672d9a2a9e227f137a10fbb126fecc38f27717b3b0471812f41a57e23ea9` |
| uvc | 0 Error(s), 0 Warning(s) | {'BOOT': '0', 'SD': '0', 'WRITE': '0'} | `90683afc041226bc18799811de5231f1c639c00d3fcef6c5c9d1f4dbabf4b30d` |

原始命令行、预处理输出及构建摘要：`Tests/UsbBulkIn/reports/build-p1b04.json`。AXF/HEX/MAP和完整日志位于`D:/32/STM32H7/h7_mac_lifecycle/output/MSC24/udlm-p1b/build-p1b04/output/MSC41/diagnostic-build/`下的三个配置目录；SD_WRITE最终为0。未运行真实SD读写。

**VERIFIED（MAP）**：最终RAM固件包含BSP与Platform的Begin/Poll/Commit符号，主动Stop协调保留调用链。早期p1b03曾因无调用者删除这些入口，已作为被替代证据保留。p1b01/02源变化检测失败也保留，不用它们声称最终版本通过。

## 2. Bulk IN专项

**VERIFIED（ARM软件模型）**：38项、796条断言；PASS=38，FAIL=0。真实BSP/Platform源代码执行；MMIO/W1C/SNAK和硬件进展为明确注入模型。

| ID | 场景 | 结果 |
| -- | -- | -- |
| BI01 | idle endpoint | PASS |
| BI02 | normal XFRC before stop | PASS |
| BI03 | processed intermediate XFRC | PASS |
| BI04 | last XFRC undelivered | PASS |
| BI05 | intermediate XFRC undelivered | PASS |
| BI06 | XFRC and EPDISD race | PASS |
| BI07 | NAK timeout | PASS |
| BI08 | disable timeout | PASS |
| BI09 | TX flush timeout | PASS |
| BI10 | repeated Begin | PASS |
| BI11 | repeated Commit | PASS |
| BI12 | late raw event before new arm | PASS |
| BI13 | old transfer identity after new arm | PASS |
| BI14 | EP1 normal during EP2 stop | PASS |
| BI15 | reset during stop | PASS |
| BI16 | disconnect during stop | PASS |
| BI17 | PIO visitor outstanding | PASS |
| BI18 | no early source release | PASS |
| BI19 | identity exhaustion | PASS |
| BI20 | stop from ISR rejected | PASS |
| BI21 | transfer identity exhaustion | PASS |
| BI22 | forged endpoint generation | PASS |
| BI23 | late callback before retirement | PASS |
| BI24 | IRQ while GRSTCTL owned | PASS |
| BI25 | restore originally masked target | PASS |
| BI26 | EPENA clear without terminal evidence | PASS |
| BI27 | NAK level does not masquerade as old XFRC | PASS |
| BI28 | common mask would expose unretired NAK | PASS |
| BI29 | unrequested disable event | PASS |
| BI30 | legacy flush fenced | PASS |
| BI31 | endpoint identity exhaustion | PASS |
| BI32 | premature commit rejected | PASS |
| BI33 | unknown failed submission retains ownership | PASS |
| BI34 | continuation failure retains busy | PASS |
| BI35 | controller generation exhausted | PASS |
| BI36 | session generation exhausted | PASS |
| BI37 | original CPU mask preserved | PASS |
| BI38 | Task Stop coordinator starts bounded transaction | PASS |

每项输入、注入操作、逐步寄存器、Transfer/StopAttempt身份、source/pool引用、事件结算与归还次数见`Tests/UsbBulkIn/reports/specialist.json`。初始运行时内存辅助函数递归及SNAK模型错误等失败保存在initial-*.json与specialist-history-*.json；已修正夹具而未降低断言。完整真实HAL IRQ行为、芯片响应时间、真实迟到事件均NOT_TESTED。

## 3. 原17入口逐用例对照

**VERIFIED**：修改前和最终均执行原17入口，退出码汇总同为6成功、11失败；不是“全绿”。case_comparison.json按原案例ID和优化配置列出结果。源代码验证三份夹具只有offset→queued_bytes标识符变化，期望常量、断言顺序不变；第四份仅追加HAL_GetTick链接桩，新停止测试不使用该固定时钟。

| 原入口 | 基线退出码 | 当前退出码 | 新失败数 |
| -- | -- | -- | -- |
| UsbCoreDecoupling/run_tests.py | 1 | 1 | 5 |
| UvcLifecycle/run_tests.py | 1 | 1 | 0 |
| UvcStreaming/run_tests.py | 1 | 1 | 0 |
| UvcPayload/run_tests.py | 1 | 1 | 1 |
| UvcEp1Lifecycle/run_tests.py | 1 | 1 | 0 |
| UvcProbeCommit/run_tests.py | 1 | 1 | 0 |
| UvcProbeCommit/run_cancellation_tests.py | 1 | 1 | 0 |
| Ep0IrqRace/run_race.py | 0 | 0 | 0 |
| UsbUvcDescriptors/run_tests.py | 1 | 1 | 0 |
| UvcUsbCleanup/run_tests.py | 1 | 1 | 0 |
| MscDeferredStatus/run_tests.py | 0 | 0 | 0 |
| MscProtocol/run_tests.py | 0 | 0 | 0 |
| MscBlockAdapter/run_tests.py | 0 | 0 | 0 |
| MscSdIntegration/run_tests.py | 0 | 0 | 0 |
| MscEnumeration/run_tests.py | 1 | 1 | 0 |
| MscEnumeration/run_integration.py | 1 | 1 | 0 |
| MscSenseCompat/run_tests.py | 0 | 0 | 0 |

**VERIFIED（保留FAIL）**：Core用例334、335、361、362、364要求旧同步取消/失败复用语义，新实现保留引用和BUSY，仍报FAIL。不能用专项通过掩盖这些兼容差异。MSC枚举两入口仍存在基线已知的链接依赖缺失，标为NOT_RUN（旧脚本退出FAIL），不算行为验证通过。其余历史UVC失败保留。

**UNRESOLVED**：最终批次UvcPayload -O0 case1额外失败，fail_line=0、error=None；并行负载/仿真预算是推测，不是已证实根因。单独复跑结果另附，不覆盖原批次失败。基线原有case37在-O0/-O2均失败，必须保留。

## 4. 验收判定

**VERIFIED**：范围内生产机制、专项测试、三配置编译及原入口比较已执行。**UNRESOLVED**：五项接口兼容差异、Service把Pending转ERROR、共享Flush窗口IRQ导致保守故障、ES0445 slave ZLP覆盖，以及实板停止证据。P1-B整体尚不能宣布通过。

**NOT_TESTED**：烧录/USBPcap/插拔、默认UVC实板流、真实SD、完整自动重连。本轮没有进入P1-C。

## 5. 独立UVC复跑与最终Diff检查（2026-11-20）

**VERIFIED（软件复跑）**：原UvcPayload入口单独复跑，124次执行中122 PASS、2 FAIL，只有case37的-O0/-O2失败，与修改前相同；额外-O0 case1本次未复现。原p1b04批次的121/124和case1 FAIL仍完整保留，不用复跑覆盖。根因仍UNRESOLVED，不能把并行负载/仿真预算推测当成事实。证据为uvc-payload-confirmation.json/.log。

**VERIFIED（Diff）**：生产修改只限BSP/Platform四文件；四份旧夹具仅字段重命名与追加时钟链接桩，原17入口未修改。采用现有CRLF约定，以单次core.whitespace=cr-at-eol检查，无实际行尾空白新增。未修改Service、Class、HAL/LL、Core、Keil或其他禁止模块。PDF哈希不变且不暂存。

**UNRESOLVED（总体验收）**：38项/796断言通过和三配置0错误0警告不等于历史回归全绿。五项同步接口兼容差异和Service Pending接入阻塞保留；本轮不宣称可烧录验收或默认重连恢复。

**VERIFIED（原始日志例外）**：代码/文档按CRLF约定的Diff检查通过；部分原始工具日志含CRCRLF、尾部空行，完整Git whitespace检查会报告这些日志。为保持原始证据SHA，本轮不剥离日志字节，未将日志例外说成代码规范通过。暂存内容已逐文件与工作区字节比较一致。
