# Phase 1 资源预算与构建报告

## 1. 工具链与证据边界

ARMCC5 5.06 update 7 build 960，原工程 C99 配置；新增源在 Cortex-M7.fp.sp O0/O2 独立构建中全部编译/链接。最终正式工程无文件/选项变更，两核完整 Rebuild 均 0 Error、0 Warning。原始命令、退出码、构建日志及 artifact SHA256 见 `Tests/V2Phase1/Evidence/`；真实 MAP/AXF/HTM/HEX 保留在本地 `D:\32\STM32H7\h7_mac\output\V2Phase1\build_20261116_202422`。

最终源码通过编译文件哈希绑定：`results.json.source_sha256` 与 `Evidence/build_results.json.source_sha256`。未提交二进制构建产物或依赖运行库。图/配置校验没有接入任何任务，本轮没有实板高水位或 WCET 数据。

## 2. ARM 目标实测 sizeof

O0/O2 一致，使用目标指令读取，不按主机指针宽度估算。字节：

| 类型 | 字节 |
|---|---:|
| CoreMediaFormat | 56 |
| GraphPipelineDesc | 108 |
| GraphSelection | 124 |
| GraphValidatedView | 168 |
| GraphScratch | 280 |
| V2ModuleDesc | 52 |
| V2Resolution | 104 |
| V2ResolverScratch | 136 |
| V2CompositionCandidate | 276 |
| V2CompositionScratch | 696 |
| GraphCaps | 48 |
| GraphEntityDesc | 20 |
| GraphPadDesc | 16 |
| GraphLinkDesc | 16 |
| GraphDesc | 40 |
| GraphWorkspace | 112 |
| V2Provided | 8 |
| V2Requirement | 20 |
| V2Registry | 8 |
| V2Issue | 20 |
| GraphIssue | 28 |
| V2EntityBinding | 8 |
| V2Desired | 16 |
| GraphEntityOps | 8 |
| GraphFrameRate | 8 |
| GraphSinkPolicy | 12 |
| AppMessage_t | 52 |
| AppLifecycleMessage_t | 56 |
| CameraFrame_t | 24 |
| V2Status | 4 |

旧 AppMessage=52B、LifecycleMessage=56B；CameraFrame=24B。V2 本地 CoreMediaFormat 恰为 56B，与 Lifecycle Message 不是同一结构。没有新增消息 ABI 或改变其字段偏移。

## 3. 固定容量与 RAM 上限

| 对象 | 容量/字节 | 说明 |
|---|---:|---|
| Registry | 16 Module；32 Provided；64 Required；32 Optional；32 Conflict | 描述在调用者 const 表，非运行时增量数组 |
| Graph | 16 Entity；32 Pad；32 Link；8 Pipeline；32 Caps | 另限制 5 Sink / Pipeline、4依赖 / Pipeline |
| Composition scratch | 696B | 含 Resolver136 + Graph280 + Candidate276 + busy4 |
| 独立候选结果 + 两种 Issue | 276+20+28 = 324B | 成功末尾提交，不能把 scratch 当成结果 |
| 一个完整校验工作集 | 1020B | 上述696+324；建议调用者静态持有 |
| 最大 Selection 输入 | 8×124 = 992B | 原请求不可写 |
| 根模块 / Binding / Desired | 64+128+16 = 208B | 最大16根、16绑定 |
| 包括全部最大输入的持久 RAM 预算 | 2220B | 不含调用栈，不代表本轮已有此静态实例 |

实际新增生产 C 没有全局可写实例，独立对象的 RW/ZI 都为 0。本轮没有创建任何实际 Registry 产品表、任务/队列/Frame Pool/Lease 或 SDRAM 缓冲；原固件链接增量为 0，V2 未被原工程引用。未来若把2220B工作集放入函数局部变量，会显著加深栈，禁止把当前测量直接当作那种调用方式的栈预算。

const 描述最大预算：Registry 表为16×52 + 32×8 + (64+32)×20 + 32×4 + Registry8 = **3144B**。Graph 表含16 Entity、32 Pad、32 Link、8 Pipeline、32 Caps及Graph40/CheckContext8，共3792B；再为每Pipeline保留独立默认格式/5个rate共768B、24个最长32B名称共768B、16组Ops指针128B，合计 **5456B**。两类const加最大128B绑定约8728B，不含函数代码/opaque真实Module Ops；共享默认值/名称/回调可降低。不额外放进RAM，也不与已经存在的CameraStream/FIFO重复分配。此为用实测sizeof计算的容量预算，不是Map中已分配的产品数据。

## 4. ROM 与最终固件 Map

独立新增 V2 对象（排除测试夹具）RO 总和：
- O0：10088B；RW=0、ZI=0。
- O2：8144B；RW=0、ZI=0。

这些数包含该独立编译方式的代码/只读段，尚未包含未来真实适配器和链接库对齐等开销，不能直接等同于最终启用增量。原固件仍为Legacy入口，无V2调用。

| Target | Code | RO-data | RW-data | ZI-data |
|---|---:|---:|---:|---:|
| CM7 | 137690 | 3114 | 1340 | 100932 |
| CM4 | 26350 | 1438 | 936 | 3576 |

CM7 Map：RO140804B、RW+ZI102272B、实际ROM141176B；Map使用链接器初始化/压缩规则，不能用未压缩RW字段生硬相加来覆盖ROM统计。原有预构建产物来源未知，不把它作为独立重编基线推断RAM变更。最终原工程源码/选项与输入相同，V2独立库RO/RAM另行列出。

## 5. Stack、CPU 与实时性

| 场景 | O0实测SP最大下降 | O2实测SP最大下降 |
|---|---:|---:|
| Test ID 0 | 136B | 184B |
| Test ID 89 | 556B | 580B |
| Test ID 100 | 188B | 184B |
| Test ID 102 | 412B | 428B |
| Test ID 103 | 412B | 428B |

测量包含测试入口与Mock回调，仅为这些执行路径、无中断情况下的SP变化。链接器静态callgraph报告：
- O0：Maximum Stack Usage =        552 bytes + Unknown(Untraceable Function Pointers)。
- O2：Maximum Stack Usage =        576 bytes + Unknown(Untraceable Function Pointers)。

存在函数指针 Unknown；上述数不是全部未来适配器的硬上界。主要链为测试入口→Composition→Resolver/Graph→Catalog→shape→cycle；无C递归。shape有32B局部degree表，其他表在调用者scratch，不创建大局部Frame/Pool。未来真实回调必须有自己的栈与时限审计。

现有实际Map：Camera栈6144B（1536 words）、USB8192B、Storage8192B（各2048 words）；优先级仍3/4/1，未改等待时间。新增函数未接到这些任务，所以本轮没有其真实任务栈增量或运行高水位验收。

Resolver最多16轮，每轮最多96个需求与32个能力提供项的有界比较；另有固定16节点排序/冲突扫描。Graph按原R2最多18次路径helper约束，33408仅指定helper单位，不是CPU周期。精确纯Ops最多21次，其内部成本不由Graph强行上限化。没有新增周期任务/每帧遍历，CPU占用、DWT/WCET仍 RUNTIME_NOT_PROVEN。后续接入Configuration必须先确认任务上下文和延迟预算，不能直接放进IRQ/高优先级每帧路径。

## 6. 镜像与保护核对

最终工程 SHA256：`a8f9e0d08c9cceb9ec0934fe287f6c71ad99bb846f13246ef884e801369c9aa1`。M4 main SHA256：`1dec895ef44e568a9a263fc8b229fc2622c6a625c54b8e291286e186be36454c`；均与ce33d07原字节相同，OLED延时保留。

| Target / artifact | SHA256 |
|---|---|
| h7_mac_CM7 / h7_mac_CM7.axf | `dfa56fb10d355f598a024b9fe3f9bf3b9564b2a7f0077081308cac04b93b790d` |
| h7_mac_CM4 / h7_mac_CM4.axf | `9e1230dfee3ad2b7306ca073a0b0518b21abd35bbb8f2a39a49c818387a1f420` |
| h7_mac_CM4 / h7_mac_CM4.hex | `7be9685cb70201486281dcc0a327ea485705f3c7ba271a5dc2e1feb7ccdbd301` |

AXF/Map/HTM/HEX均保持原Target的产出配置，未强行打开CM7 HEX。后续USB、Storage、Ethernet、Wi-Fi、AI带来的Buffer/网络栈/DMA/控制队列预算不包含在这些Phase1数字中；必须按各阶段独立测量。现16/32/32/8容量够本轮Mock、最大边界和候选校验，不据此承诺未来所有模块一定装得下。超过容量明确拒绝，不能自动扩容或调任务栈。

两个资源NOT_PROVEN继续保持，实际资源使用与硬件性能不从本预算推断通过。
