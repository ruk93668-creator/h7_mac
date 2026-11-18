# Phase 5 USB Module 实施与验收

## 交付范围

输入 `2c38c6240b2468beed504151745ce6aff06ca474`，保留 `1627d13` 的初始检查点。生产模块提交为 `a5992fc` 和 `152637f`。此报告之后的提交新增联合测试及验收材料。当前交付为独立软件基础，V2 USB 未登记原 Keil Target 或旧任务启动流程。

| 文件 | 职责 |
| --- | --- |
| usb_format.c/.h | 中立格式派生，复用真实 OV5640、Capture 与 UVC 纯验证器 |
| usb_consumer.c/.h | 实际 C2.1.1 Tx；USB 客户端 G/R 与匹配回复 |
| usb_frame.c/.h | 固定一条 H/B/M 交接；未提交通道取消；物理归还、消费、退休 |
| usb_module.c/.h | Configure、Activate、Deactivate、Cleanup、ErrorProcessing 和 owner-local 证据 |
| usb_endpoint.c/.h | Supervisor 与 USB owner 的 Request、Complete、Consume、ACK |
| Tests/V2Phase5 | 真实 ARMCC5 链接执行、负对照、既有套件复验和隔离 Rebuild |

[接口、调用链和状态图](USBModule_InterfaceContract.md) · [Legacy 最小接缝差异、MSC 与 Phase 6 前置条件](Phase5_LegacyAdaptationPlan.md)

## 软件

| 测试 | 结果 |
| --- | ---: |
| Phase 5 O0 | 19/19 |
| Phase 5 O2 | 19/19 |
| Phase 5 负对照（两个优化等级） | 6/6 被断言检出 |
| Phase 1 / 2 / 3 / 4 | 250/250 · 112/112 · 158/158 · 154/154 |
| Phase 4 既有负对照 | 10/10 被检出 |
| Replay C2.1.1 / ABI | 648/648 · 2/2 |
| C1 / C1.1 | 419/419 · 24/24 |

联合用例编号 0–18 依次覆盖正常链、执行队列/回复/消费背压、资格运输背压、旧 session 结果、旧 Offer、失去 host-ready、Abort 首错、Mode Stop 首错、缺少 RELEASE 证明、后端 Release 错误及终结重试、旧 B 的 ACK、独立退出、启动回退、未提交取消、超时后排空、在途重启 BUSY、合法重启清当前故障、Camera STOP_IF_IDLE 延迟、JPEG 两个不同所有者。实际联合执行使用 VUSB、Phase 3 Lifecycle、Phase 2 MR、Phase 4 Camera 格式/退出查询及真实 UVC 规范校验器。端口使用可控任务身份、复制队列、传感器/USB 控制、物理持有事实及资格回复。JPEG 双 owner 用例经过同一真实 MRContext，以不同 M 对同一测试数据的两个 Span 验证 USB 退出保留 SD 租约。实板 FIFO 和真实资格 RELEASE 回调未执行。

负对照在隔离输出目录编译三个变异，跳过读者门禁、伪造 RELEASE 证明、覆盖 STOP 首错。每种在 O0/O2 均由实际测试断言检出。原 72 项和 Phase 2 新增 32 项保留原映射与实机状态，未相加或改判。

复验命令是 `python Tests/V2Phase5/run_tests.py`、`python Tests/V2Phase5/run_regression.py` 及 `python Tests/V2Phase5/run_build.py`。原始命令和执行证据留在 `output/V2Phase5`，进入 Git 的 [Evidence](../../../../Tests/V2Phase5/Evidence/) 为测试结果、构建文字日志、摘要及工件 SHA256。

## 构建及资源

ARMCC5 Cortex-M7 O0/O2 独立编译、链接和执行通过。原 Keil Target 以原用户工程文件在隔离副本双核完整 Rebuild。CM7/CM4 均退出 0、0 Error、0 Warning，V2 未注册在 Active Target。

| 构建 | Code | RO-data | RW | ZI |
| --- | ---: | ---: | ---: | ---: |
| CM7 | 137690 | 3114 | 1340 | 100932 |
| CM4 | 26350 | 1438 | 936 | 3576 |

ARMCC5 实测 USB 局部对象 VUSB、VUSBEndpoint、VUSBClient 分别为 732 B、124 B、88 B，合计 944 B，不含生产队列、调用者 Ops 表或已有 MR 资源。嵌入的 VUSBConsumer 292 B，VUSBFrame 200 B，偏移分别为 224 B、516 B。对象文件五个 USB 编译单元分配 RO 合计 O0 6488 B，O2 5352 B，未增加模块级 RW/ZI。独立镜像包含测试代码和可控端口，不用它推算正式镜像净增量。既有 ABI 门禁实测 AppMessage 52 B 和 Lifecycle Message 56 B。

Stack 在 ARM 指令级模拟记录所选路径的最低 SP，包括夹具、VUSB Endpoint、LC、可控端口和对接的 MR/Camera 调用；O0 748 B，O2 784 B。实际任务栈高水位、真实调用深度上限和 WCET 未测。已保留 Map 与间接回调路径证据，不能替代实板测试。

## 实物门禁

- Grant → Frame Lease 为 **NOT_PROVEN**。待真实 Bridge 发布、Camera Media 原始 token、Legacy Return 和 USB 匹配 ACK 消费闭环。
- Stable Admission Closure 为 **NOT_PROVEN**。待真实 Release 期间旧 REQUEST、pending Offer、Storage Retain、DMA、SD-only 和终结收据窗口的实板/任务交错验证。
- Legacy `released`、`idle` 仍未接线。实板 USB、主机协商、真实 Abort、任务队列与实际 Storage 联合运行系 **NOT_RUN**。Phase 6 未开始。
- Legacy、Replay、52B/56B、任务配置、用户 Keil 改动和 M4 OLED 延时未修改。研究分支单独交付，main 未合并。
