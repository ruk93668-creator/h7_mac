# Phase 2 Implementation Planning（PROPOSED）

输入501330a及Phase1_FinalReview；不构成实施授权。冻结路线唯一权威：MigrationPlan第2节、ApplicationArchitecture第10节。Phase2仍为Static Media Graph；本组资源文档是Phase4前置契约，不能据标题实施Resource或提前迁移Phase6/7/8。

## 1. Phase2工作包

| 顺序 | 工作/候选文件 | 输入/输出 | 验收与回退 |
|---|---|---|---|
| P2-A | Configuration/product_graph只读装配 | 已验证Graph算法→实际产品Entity/Pad/Link/Raw及JPEG固定Pipeline表 | 静态ID/能力/主线板型核对，future拒绝；无启动绑定；独立提交可整体回退 |
| P2-B | 产品Module描述/Entity绑定 | Registry仅扩大描述，无Resolver业务分支 | 新增Mock模块不改Resolver；依赖/歧义/optional及容量完整测试 |
| P2-C | 能力适配接口审核 | Source/Backend/UVC/Storage纯查询，Core中立类型 | 缺少纯查询先列最小ADAPT申请，不调用Offer/Prepare试探；实际生产Port实施归Phase3及单独授权 |
| P2-D | 联合纯配置门禁 | 表/输入→候选拓扑，Resource暂用明确Mock/拒绝器 | 10FPS输出基线与25目标分开；候选成功不Apply；未支持Raw双消费者必须拒绝真实运行 |

依赖顺序A→B→D，C接口设计可并行但缺真实能力时不得伪造成功。保留Legacy唯一活动入口、原Target、任务/栈。每包独立提交；不增加启动器或顶层管理器。Phase1本地接口和容量先人工确认，FR-01/02/04不解决前不接入并发任务。

## 2. Phase4资源前置设计与实施顺序

1. 先冻结身份和Backend公开能力；Raw不能虚构独立多owner Retain，JPEG不能泛化现有USB/SD位为任意consumer。
2. 只读Backend快照/精确M身份与DMA证据接口先单独审核；不在外围重建物理引用计数。
3. Media Resource的Grant关联元数据和准入关闭，先做单owner纯模型，再接真实Backend适配；事务工具沿用原Replay，不复制。
4. Terminal Receipt、ACK消费和退休必须先通过故障交错，后进行逐消费者退出核验；没有证明返回BUSY/错误并保留事实。
5. 最后验证真实Raw/JPEG双backend、SD-only、独立退出、最后消费者排空。Camera/USB/Storage真实业务绑定仍在Phase6/7/8；硬件切换在已授权集成门禁后进行。

1是2/3前提；Receipt设计可与Backend端口设计并行，但最终核验依赖二者完整。失败只回退未活动V2提交；一旦进行硬件实验必须回退完整固件，不能把旧新owner账本混用。不得通过清空Replay或重置session解决排空失败。

## 3. 人工待决事项

阶段编号冲突、Raw多消费者采用复制后端还是经审核扩展底层所有权、第三consumer后端适配、Receipt与在途上限、启动Source代次持久边界均UNRESOLVED。接口/预算为PROPOSED，Grant关联和稳定关闭继续NOT_PROVEN。不得以本计划直接开始Phase2或Phase4生产代码。
