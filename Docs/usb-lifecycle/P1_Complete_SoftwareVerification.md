# P1 Complete Software Verification — 部分交付 / BLOCKED

VERIFIED：起点 `9d51d0c5d5b62d04af3be968ec452dae8835c0c1`；最后生产代码提交 `8b5c6f42992ba708a2c7b48bd5bb05e1176eb545`；最终验证批次 **p1-final05**。源码与构建快照逐字节一致，三个固件构建均为完整重编。最终文档提交号由Git提交及交付回执确定，不能将构建前SHA冒充文档最终SHA。

## 已执行专项

| 范围 | 用例/观察数 | 通过 | 断言 | 证据 |
| --- | ---: | ---: | ---: | --- |
| 原Bulk IN + Service | 76 | 76 | 1158 | in-baseline-p1-final05.json |
| 原C0硬件模型 | 24 | 24 | 99 | c0-p1-final05.json |
| 原HAL Hook | 19 | 19 | 130 | c1-p1-final05.json |
| 原Bulk OUT | 57 | 57 | 925 | c1-p1-final05.json |
| 新双向聚合 | 21 | 21 | 611 | dual.json |
| 新MSC消费者排空守卫 | 3 | 3 | 33 | worker.json |
| 当前EP0/ZLP行为见证 | 5 | 5符合预期 | 14 | gate.json |
| 离线采集/解析工具 | 12 | 12 | 不另累计 | offline_tools.json |

VERIFIED（软件模型）：原176项共2312断言；新增24项生命周期测试644断言。另有5项阻塞行为见证14断言和12项工具检查，**不可合并解释成41项完整生命周期验收**。gate验证当前LL合并CNAK/EPENA及OUT0取消行为，非修复或硅片AHB时序通过。模型调用真实ARM BSP/Platform/HAL/LL，MMIO事件来自模型；模型没有向Proof写PROVEN或直接清引用。Worker测试执行真实Class/BOT/Storage，设备quiescent输入是模型，Close保守拒绝，不证明真实SD IDMA已停止。

VERIFIED：dual-history/worker-history/gate-history及全部早期批次保留，不修改原断言。最后一轮源码、MMIO轨迹、实际/预期与命令见各JSON；测试覆盖范围不等于所有端到端协议恢复已完成。

## 原17入口逐记录对照

VERIFIED：原测试文件核验 **2028** 个，变化0；可比较记录 **818** 条，状态及细节变化0。原始6个入口成功、11个失败仍保留。以下是报告记录数，包含导入及O0/O2重复，非独立用例总数。

| 入口 | 记录 | PASS | FAIL | 说明 |
| --- | ---: | ---: | ---: | --- |
| UsbCoreDecoupling/run_tests.py | 204 | 196 | 8 | 原记录保持 |
| UvcLifecycle/run_tests.py | 40 | 34 | 6 | 原记录保持 |
| UvcStreaming/run_tests.py | 36 | 1 | 35 | 原记录保持 |
| UvcPayload/run_tests.py | 124 | 122 | 2 | 原记录保持 |
| UvcEp1Lifecycle/run_tests.py | 45 | 0 | 45 | 原记录保持 |
| UvcProbeCommit/run_tests.py | 89 | 0 | 89 | 原记录保持 |
| UvcProbeCommit/run_cancellation_tests.py | 128 | 0 | 128 | 原记录保持 |
| Ep0IrqRace/run_race.py | 4 | 4 | 0 | 原记录保持 |
| UsbUvcDescriptors/run_tests.py | 16 | 16 | 0 | 原记录保持 |
| UvcUsbCleanup/run_tests.py | 14 | 0 | 14 | 原记录保持 |
| MscDeferredStatus/run_tests.py | 17 | 17 | 0 | 原记录保持 |
| MscProtocol/run_tests.py | 41 | 41 | 0 | 原记录保持 |
| MscBlockAdapter/run_tests.py | 18 | 18 | 0 | 原记录保持 |
| MscSdIntegration/run_tests.py | 24 | 24 | 0 | 原记录保持 |
| MscEnumeration/run_tests.py | 0 | 0 | 0 | NOT_RUN：链接失败 |
| MscEnumeration/run_integration.py | 0 | 0 | 0 | NOT_RUN：链接失败 |
| MscSenseCompat/run_tests.py | 18 | 18 | 0 | 原记录保持 |

VERIFIED：UVC Payload为62个独立case、124次执行，case37在O0/O2的2个原始FAIL保留；Cancellation报告含导入记录；Descriptor16记录中存在重复且入口仍因历史编译缺口失败，不能仅看表内16 PASS宣布整个入口通过。

UNRESOLVED（新增夹具链接缺口）：两个MscEnumeration入口原本均无法链接，本次由11/8个链接错误变为12/9个，分别新增未提供 `USB_MSC_BotBackendDrained`。原始夹具未被改成空成功桩；该变化单独记录于case_comparison.json及原始日志。没有新增可执行case FAIL，**不能声称全部历史日志无变化或所有回归通过**。新worker真实源码测试独立覆盖该守卫；生产完整固件链接通过。

## 三配置生产构建与实际调用

VERIFIED：ARM Compiler5.06 update7 build960，MDK Plus5.32；现有MscReconnect/build.py在独立snapshot使用完整重编命令，未修改根Keil工程。argv、预处理、AXF/HEX/MAP大小与SHA256、完整日志和MAP交叉引用见build_artifacts.json。

| 配置 | BOOT/SD/WRITE实测预处理 | Error/Warning | AXF SHA256 |
| --- | --- | --- | --- |
| ram | 1/0/0 | 0/0 | `6ee433df49c9378989df108ebfa0e508891d7fe52e17361f29ca687580f64936` |
| sd_readonly | 1/1/0 | 0/0 | `c52e86ceb9b09a20b5ad375775eb21ec0ab2899654f67bb4ea5a392a211949fe` |
| uvc | 0/0/0 | 0/0 | `3b805920499ac1f51092921c0452968522bce4329ded05792330aa61c53c2c7b` |

VERIFIED：三个MAP均保留 `Usb_DrainBulkEndpointStops` 和真实OUT机制；RAM/SD存在 `Msc_DeInit -> USB_MSC_BotBackendDrained -> backend.drained`。**三配置应用均未调用EnableBulkOutLifecycle，Enable符号被链接器剔除**；故不能以聚合函数存在宣称新停止链已正式启用。C2 opt-in入口通过真实ARM模型验证；完整生产启用仍BLOCKED。

UNRESOLVED：GetStopProgress扩大到多端点快照，Service栈占用增量未进行实板高水位测量；未用生产构建0警告掩盖该风险。

## 验收边界

BLOCKED：EP0合法退休、Slave/PIO ZLP合格AHB屏障、ISO/frame消费者票据、BOT Reset全迁移、统一ParticipantRegistry/PreparedTicket及Controller Resume。历史同步语义未通过重写旧断言“消除失败”。

NOT_TESTED：全部板级枚举、插拔、视频、SD实读、IDMA迟到IRQ、AHB实际时序与USBPcap。本轮无烧录、无探针连接、无SD写入、无硬件操作。最终结果为**部分软件交付，P1整体未通过，不具备合并main资格**。

证据目录：`Tests/UsbLifecycleFinal/reports/`。完整复现方式见同目录上级README；所有早期输出和失败均保留。
