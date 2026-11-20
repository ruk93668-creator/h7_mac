# UDLM P1-B.1 最终软件验证与人工审核交付

基线：`5113000227e8d3d3a9a460b4585993e28096d0cc`；分支 `codex/usb-device-lifecycle-manager`。

## 交付结论

VERIFIED：授权范围内的软件整改、专项测试、17入口回归、三配置完整重编和真实 MAP 核验已执行。原 38 项全部保留，另增 38 项集成用例，两个集合均全部通过，共 1158 条断言。

UNRESOLVED：这不是所有旧同步契约及所有硬件竞争均已关闭的验收。五项 Core 断言继续 FAIL/等待 CONTRACT_CHANGED 人工审批；非纯 SOF 的共享命令竞争仍保守失败；ZLP 勘误修复和 MSC Class 调用者迁移不在本轮授权内。不能声明 P1/P1-B.1 整体安全验收全通过，也不能声明 Bulk OUT 或 MSC 整机实板重连通过。

NOT_TESTED：本轮未烧录、未插拔板卡、未读取新硬件状态；没有进入 P1-C、P2 或合并 main。

## 已关闭的软件问题及安全证据

| 问题 | 状态 / 证明 | 证据 |
| --- | --- | --- |
| Service 把真实 Pending 当 ERROR | VERIFIED，查询实际非零 transaction/Attempt，保持 STOPPING，普通 BUSY失败 | 新测试01–10、30、38；ServiceIntegration Report |
| 两个 App 停止执行器/提前 ACK 风险 | VERIFIED，真实 app_usb.c只读编入测试；Pending无ACK/FAULT、最终ACK一次 | 新测试31–33；Application文件未修改 |
| 首次有效停止前已有前置失败 | VERIFIED，失败尚无事务ID时仍为首次真实Pending分配非零身份，耗尽拒绝 | 新测试38；无身份回绕 |
| 纯 SOF 导致不必要 FIFO故障 | VERIFIED，仅派发SOF的受限BSP分支，不把入口快照当完整HAL无冲突证明 | 新测试11、36，途中Reset仍保留到下次IRQ |
| Stop失败记录被覆盖 | VERIFIED，first_error/failed_stage/failure_count保留，当前error独立 | 新测试17–26、35 |
| 有证据的迟到完成无法收尾 | VERIFIED，同Session/Transfer/Attempt复核EPDISD或原FIFO命令，仍须Poll/Commit | 新测试18–25、35；无直接归还引用 |
| Close绕过停止 / 影响另一方向 | VERIFIED，未证明时Close/Abort返回BUSY，IN和OUT保留 | 新测试27、28、37，原Core334仍保留FAIL |

## 三配置完整构建与真实调用链

VERIFIED：ARM Compiler 5.06 update 7 (build960)，使用现有 `Tests/MscReconnect/build.py`，在隔离快照重建 MDK 工程，工程原件未修改。完整命令、编译器、预处理值、AXF/HEX/MAP哈希及源码一致性在 `Tests/UsbBulkInIntegration/reports/artifact_verification.json` 和 `Tests/UsbBulkIn/reports/build-p1b1-final03.json`。

| 配置 | BOOT/SD/WRITE最终预处理值 | 结果 | AXF SHA256 |
| --- | --- | --- | --- |
| ram | 1/0/0 | VERIFIED，0 Error / 0 Warning | `66d32c2bf9a6e972b1bdb5248063d04979f9c2e89ea4c5c53745e2167e2d36cb` |
| sd_readonly | 1/1/0 | VERIFIED，0 Error / 0 Warning | `ae3cd1c91302e2fbde4840f6033ae58ae0f08c4a73435927527fd1d2b0ac27b7` |
| uvc | 0/0/0 | VERIFIED，0 Error / 0 Warning | `b78e810efa052230a65063e235154e7c2cae383d6672fddca7a1b867bd7c0a55` |

VERIFIED：每个配置 MAP 各有8条真实交叉引用，不是测试夹具符号：USB_Mode_Process/Stop→UsbMode_AdvanceStop→USB_Device_Stop/GetStopProgress；Usb_StopDevice→Usb_DrainBulkInStops→Begin/Poll/Commit/Reconcile→BSP Reconcile。核验记录保留具体 MAP 行号。最终生产源码与最终构建快照字节哈希一致。

## 原17入口回归（逐用例对照）

VERIFIED：全部入口已执行，6个退出成功、11个退出失败。历史失败未删除或降级断言；相对5113000没有新增失败。相对P1-B前原始基线仍多Core334/335/361/362/364五个FAIL，不能称为“全部历史回归通过”。所有用例的状态、失败行、actual/expected及assertions保留于history_comparison.json，原始JSON/日志在regression-p1b1-final04报告中。字节相同而被快照驱动略去的4项EP0结果已单独补入对照。

| 原入口 | 实际结果 / 未关闭事项 |
| --- | --- |
| UsbCoreDecoupling/run_tests.py | VERIFIED，204项196 PASS/8 FAIL；109/110/201原有，334/335/361/362/364在5113000已有；2914断言 |
| UvcLifecycle/run_tests.py | VERIFIED，40项34 PASS/6 FAIL，历史失败 |
| UvcStreaming/run_tests.py | VERIFIED，36项1 PASS/35 FAIL，历史失败 |
| UvcPayload/run_tests.py | VERIFIED，124次122 PASS/2 FAIL，保留case37的O0/O2失败 |
| UvcEp1Lifecycle/run_tests.py | VERIFIED，45项全部FAIL，历史失败 |
| UvcProbeCommit/run_tests.py | VERIFIED，89项全部FAIL，历史失败 |
| UvcProbeCommit/run_cancellation_tests.py | VERIFIED，39项全部FAIL；另导入执行的89项结果也保留，不能重复计数为新增用例 |
| Ep0IrqRace/run_race.py | VERIFIED，4项PASS；非仅凭退出码 |
| UsbUvcDescriptors/run_tests.py | VERIFIED，入口FAIL；旧缺camera_mjpeg_fifo.h的子构建错误仍在，8项Core字符串测试PASS，保留所有子报告 |
| UvcUsbCleanup/run_tests.py | VERIFIED，14项全部FAIL，历史失败 |
| MscDeferredStatus/run_tests.py | VERIFIED，17项PASS |
| MscProtocol/run_tests.py | VERIFIED，41项PASS |
| MscBlockAdapter/run_tests.py | VERIFIED，18项PASS |
| MscSdIntegration/run_tests.py | VERIFIED，24项PASS，软件模型不代表真实SD |
| MscEnumeration/run_tests.py | VERIFIED，原11项链接错误仍存在，行为测试NOT_TESTED |
| MscEnumeration/run_integration.py | VERIFIED，原8项链接错误仍存在，行为测试NOT_TESTED |
| MscSenseCompat/run_tests.py | VERIFIED，18项PASS |

VERIFIED：UVC Payload在最终生产源码快照单独重跑，仍124次122 PASS/2 FAIL，原case37 O0/O2失败全部保留，见uvc-payload-isolated.json/.log。P1-B基线一次批量case1的非稳定失败及其历史隔离重跑记录未删除。本轮不根据不稳定日志臆测硬件根因。

## 失败证据与中间快照

VERIFIED：原38项的起始specialist.json按原始字节存入specialist-history-*；旧断言源码和原17个入口字节未修改。新增测试初轮包含3项夹具失败，扩展阶段有非法端点范围失败，均保留history JSON；另一次新增夹具枚举初始化告警已改为正确指定初始化，最终专项编译无告警。

VERIFIED：build-p1b1-final01底层三构建成功但original_changed非空，不作为最终源码证明；build-p1b1-final02为中间冻结版本，后续发现“前置失败后首次事务身份”边界而被最终03替代。regression01–03及原始日志保留，最终04无original_changed。每次重复均由源码/边界整改触发，没有覆盖原失败。

## 待人工审核的旧契约与授权扩展

UNRESOLVED：请审核 [五项兼容审查表](USB_P1B1_Compatibility_Review.md)。334/335/361/362/364均建议按CONTRACT_CHANGED处理，原断言未迁移；335真实HAL不提供BUSY路径，不能把夹具的立即重试假设推广为零副作用硬件保证。

UNRESOLVED：MSC BOT/DeInit内部调用者尚未迁移，完整Controller/OUT/ISO关闭及共享GRSTCTL的所有事件分发未统一。本轮保留锁定与原资源，不通过强制重启掩盖。需要未来明确授权的Class/事件分发协调阶段，不能偷偷进入Bulk OUT。

UNRESOLVED：ES0445 Rev7 2.24.5的PIO ZLP风险仍需另行审批全部IN/EP0提交时序扩展，见[共享FIFO及勘误申请](USB_P1B1_SharedFifo_Arbitration.md)。本轮没有修改HAL/LL/Core/EP0协议，不能宣称覆盖所有ZLP。

## Diff与Git交付

VERIFIED：生产变更仅BSP usb.c/.h、Platform usb_device.c/.h、Service usb_mode.c。没有修改Application/USB Task、MSC/UVC、Camera/Media/SDMMC/FatFS、HAL/LL/Core、Keil/MPU/Linker。新测试只读编入Application以验证契约，不修改其文件。

VERIFIED：只按授权路径暂存；RM0399 PDF哈希仍为61d07b67deb291aed8a5cf8019c94add0a1065318542e57b3287d2694b14bd60，未跟踪。保留所有output；没有操作原工作区、stash/reset/clean/强推/合并main。提交与远端SHA以实际Git核对结果为准，避免在本提交内自引用未生成的提交号。

相关报告：[Service集成](USB_P1B1_ServiceIntegration_Report.md)、[兼容性审核](USB_P1B1_Compatibility_Review.md)、[共享FIFO](USB_P1B1_SharedFifo_Arbitration.md)、[恢复契约](USB_P1B1_Recovery_Contract.md)。完成后停止等待人工审核，不进入P1-C。
