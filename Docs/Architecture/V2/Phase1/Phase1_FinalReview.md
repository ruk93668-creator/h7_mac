# Phase 1 Final Source Review

审核基线501330a；本轮为源码审核与计划设计，未重跑测试、未编译。输入工作区仅有未跟踪output/，保留。此前250/250和双核0 Error/Warning是501330a提交的历史证据，不是本轮新增执行结果。

## 1. 完整读取范围

实际读取全部4个C、5个H，包括内部helper；下表为本次核对的原始文件指纹，不能用实施报告替代源码。

| 文件 | SHA256 |
|---|---|
| CM7/ApplicationV2/Configuration/v2_composition.c | 3714e242237aee6ce3940cbfeb80690762f862744b3d5531a0d704a0d4fc4872 |
| CM7/ApplicationV2/Configuration/v2_registry.c | 86db515e3985af1334a605c4343ae3f710e7b67cf2e6684ea24d21fd977b9e26 |
| CM7/ApplicationV2/Core/media_format.c | 590207b34bca68d4b3c0df70f7478488665d7970e75049f195c7d0b5cf6f0f9a |
| CM7/ApplicationV2/MediaGraph/media_graph.c | 6329ad333b973c583d2bd4a224dc7f30859a44a5415e545ed25a439e51238b40 |
| CM7/ApplicationV2/Configuration/v2_composition.h | cd7abb497180392d52332b978e9189d3f3105f13c0e2d95d9ecfd97487da96cc |
| CM7/ApplicationV2/Configuration/v2_registry.h | 933f373278e4a1068ccf7f755ef55406709b6cbb24435a7b90df541f1d5f8c03 |
| CM7/ApplicationV2/Core/media_format.h | 497f56785ff400bb1cfb571ea8a6dfa88138632356e6cdedd6f81b417c2a68fe |
| CM7/ApplicationV2/Core/v2_types.h | e0b7dbb5e2dccaa3a67d8b0387bebdd2c1bd18d9464131403b6e91f0cb0bbdc8 |
| CM7/ApplicationV2/MediaGraph/media_graph.h | 253f61ba430a842e22fe9bb81d8235accac172b504968aa3bb4b5bc6647f2696 |

## 2. 分项判断与源码依据

| 项目 | 实际实现与判断 |
|---|---|
| Core 类型/ABI | Core/v2_types.h：32位Module/Capability/Consumer/Request，16位Graph ID，Pad零基；Status明确uint32；非运行期身份分配器。Core/media_format.h：14个uint32共56B，Graph仅别名，不混同旧56B消息 |
| 错误/格式 | Core/media_format.c CoreMedia_Validate：Raw行长及乘法溢出、YUY2偶数宽、JPEG双Span/单Plane、对齐、Backend/stream；CoreRate_Validate只检查非零，约分在Graph；无硬件能力承诺 |
| Registry | Configuration/v2_registry.c V2Registry_Validate/Get/Providers：借用静态表，校验ID/版本/flags/总容量，不建动态模块，不调用Ops |
| Resolver | 同文件 V2Resolver_Resolve：最多16轮闭包，Required和显式AUTO Optional拉入provider，默认Optional仅匹配已选集合；冲突和依赖循环拒绝；依赖先于使用者，同层按Module ID排序 |
| Provider | choose/providers：显式preferred必须在合法候选内；歧义拒绝；无隐式排名/降级。AUTO缺失记录optional_missing；默认Optional歧义仍拒绝，不随机挑选 |
| Graph | MediaGraph/media_graph.h/.c：Entity/本地Pad/Link/固定Backend Pipeline和caps；Catalog全图与依赖DAG；shape正反可达/degree/互斥/Kahn；不是Frame所有权对象 |
| Fan-out/合并 | validate_set：显式max_links/FANOUT；同Source同完整格式，共享边去重，不同Sink目标可不同；重复同Sink政策必须一致；Raw/JPEG混合拒绝，无Retain |
| 格式权威 | accepts粗能力后按Entity/Sink调用纯Ops，真实返回码保留。没有真实Camera/UVC/Storage Port，不能断言所有底层限制已接入 |
| Composition | Configuration/v2_composition.c：Resolver→Graph→Entity/Module绑定→必需ResourceValidator→candidate_only=1；不执行启动/切换；失败不提交out（合法互不重叠存储前提） |
| Scratch | Graph reserved[15]及Composition busy为同owner重入检测，不是原子锁；Resolver无回调但同样不得共享scratch并发；只读目录可共享，不可拼裸快照做资源证明 |
| 成本 | 读取Tests/V2Phase1/resources.json：scratch696B、candidate276B；O0/O2对象RO10088/8144B，RW/ZI0；Mock组合栈556/580B，静态调用图仍含Unknown。真实任务栈/WCET未证明 |

## 3. 模块扩展结论

在现有能力语义和16模块等容量内，新增模块只需增加实现、静态描述符/能力与产品登记，无需修改providers、choose或V2Resolver_Resolve。它们没有Camera/USB/Storage业务名分支。Module ID不要求连续，依赖由能力/版本决定。

“只增加描述符及实现”不等于只改一个文件：媒体模块还需Graph Entity/Pad/Link/Pipeline、Entity→Module绑定与纯Validator登记；真实backend能力和生命周期适配另审。超过容量、新像素转换/多输入图、第三物理owner不属于可纯描述扩展：须审核后端和预算，不能改Resolver冒充资源能力。未来Network可先FUTURE描述，不能借USB/SD owner位执行。

## 4. 最终审核保留项

- FR-01 / P1集成门禁：跨任务scratch保护不成立。源码Graph_ValidateSet:559及V2Composition_Validate的busy均为普通读写；只有单所有者前提下有效。禁止将这两个字段当锁后接多任务；未来调用上下文须显式串行化。本轮不修改。
- FR-02 / P2契约覆盖：别名检查不是所有嵌套对象的完整防御。v2_registry.c Resolve检查roots/scratch/out/issue，但不检查模块表及各能力数组与这些存储重叠；Graph alias:59未纳入name/Ops；Composition的外层检查未覆盖所有嵌套输入与整个scratch。合法调用须遵守头文件disjoint约束，不能宣称任意别名非法调用均有无副作用拒绝保证。若产品要求运行期完整检测，先独立授权加固和负向测试。
- FR-03 / P2诊断：多个INVALID_ARG早返回不填Issue，Resolver环仅报告一个未完成module而非完整环链；必须以函数返回值判成功，不能读取陈旧Issue宣布成功。可读诊断完善单独计划，不擅自改本轮代码。
- FR-04 / 预算门禁：scratch/candidate不是线程安全发布；callgraph Unknown不能作为最坏栈；同步Resolver尚无分步cursor和DWT。进入真实任务前应补上下文/周期及真实回调测量。
- FR-05 / 范围：V2没有加入活动Keil/旧BOOT，没有真实消费者/硬件适配。独立目标Mock证明纯候选算法，不能作为真实USB+SD验收。

未从合法单所有者、有效且互不重叠C对象的既定调用契约发现需要本轮紧急修复的P0。建议按“纯基础库有条件验收”提交人工签核，不建议据此批准运行期集成或全部资源安全。

## 5. 必须保持的验收边界

Grant→Frame Lease、Stable Admission Closure均NOT_PROVEN；72项R3均NOT_RUN；真实硬件验证NOT_RUN。旧AppMessage52B、Lifecycle56B和CameraFrame24B沿用前次ARM证据，未改ABI/Replay/M4/Legacy/Keil。本轮不运行新测试，不重签ADR，原ADR保持UNRESOLVED。

## 6. 冻结路线同步（替代上一轮阶段冲突判断）

2026-11-17用户明确最后批准路线：Phase0架构冻结，1 Core+Composition，2 Media Resource，3 Execution Manager，4 Camera，5 USB，6 Storage，7 Control Service，8 Runtime Reconfiguration，9 Network/AI，10 Legacy Cleanup。此前依据历史MigrationPlan认定Phase2=Graph、Resource=Phase4的结论现已失效；原文保留于bd24c03历史，不丢弃此前源码审核成果。

本报告第1～5节源码指纹、FR-01～05及单owner纯基础库有条件验收保持；路线同步不批准并发/硬件。Graph/Format已由Phase1实现，不在Phase2重复开发。两个NOT_PROVEN、原72和新增32项NOT_RUN保持；集合核对见同步索引及交叉表，不按104项独立契约汇总。
