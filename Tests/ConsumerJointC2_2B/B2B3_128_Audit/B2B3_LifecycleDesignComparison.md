# Lifecycle Design Comparison

基线14bbdcc。仅比较未来方案，不改变生产ABI、任务配置、Replay规则或M4延时，不执行测试。本轮两个缺口仍NOT_PROVEN；可论证的设计不等于已验证实现。

## 方案对照

| 维度 | A 最小关联改动 | B 更强显式隔离 |
|---|---|---|
| 核心方式 | 原有限在途账本附G标签，USB请求旁不可变D；Camera消费核对 | Frame envelope显式携G；逐消息核对G/B |
| 复用权威账本 | 原Qualifier/Bridge/slot/Media | 同样复用，不另做引用计数 |
| 新字段 | D、当前交接/last_ack G/M、slot G；原phase作准入门 | envelope G及资源标签；可能另有handoff序号 |
| 跨任务完整G | 经受同步保护D或同步调用参数传递；不进52B消息 | 明确随Frame消息跨队列传递 |
| 原消息ABI | 可保持AppMessage52B/Lifecycle56B/CameraFrame不变 | 扩展AppMessage或新类型/通道均须另审；例如52+16=68B仅算术示意 |
| 静态RAM | 原始必要字段约64+16*S（S=1约80B）；对齐/额外同步未计 | 至少每帧队列槽+16B及资源标签，另算接收栈；非实测 |
| 旧grant风险 | D不变及退休边界是核心证明义务，缺D一律拒绝 | 报文身份可直接拒旧；仍要防资源记录/入口复用 |
| 闭合复杂度 | 单在途下较小，但需严谨跨队列元数据协议 | ABI/队列成本更大，多在途更直观 |
| 新增生产API | Camera身份化准入/核验、资源只读身份查询/参数适配 | 上述加typed frame消息适配 |
| 本轮可否实现 | 不可，只有设计授权 | 不可，特别是ABI/队列变更无授权 |

内存是设计字段预算，不能写成已验证RAM增量；后续只有单独授权后才能ARMCC5实测。A若为同步/退休新增字段须更新预算，不隐藏额外成本。两方案不能自动关联non-JPEG帧，必须审对应CameraCapture路径或拒绝未定义格式；本阶段不修改实现。

## 推荐

推荐先审核A：当前Bridge单REQUEST、Storage固定slot已有严格状态，增加身份关联元数据与统一身份化准入，复用Qualifier ACTIVE→RELEASING关闭线性化点，避免先扩大Frame消息ABI。Camera核心按consumer identity选择证据提供方，不能变成一串USB/Storage/Network特判；资源适配仍按模块职责实现，不建万能AppManager。

推荐有条件：必须先完整冻结D发布/取消/重试/终结消费/退休协议；确认所有取Frame和Retain经过G门；解决只读M身份查询及SD-only内部持有归属；动态证据覆盖原缺口。A无法证明时继续NOT_PROVEN并审B，不能省略身份匹配。B作为未来多在途或多个资源使用者的备选，不因Network远期需求立即实现。

## AC-01至AC-06核对

- AC-01：两方案都用身份化Camera核心入口及资源提供方；不扩大固定专用函数集合，不新增引用真源。
- AC-02/03：明确G与R独立，受控Qualifier关联，Frame B/M不复用request编号；保留Replay高水位及接替。
- AC-04：USB/SD真实owner保持，关联标签不释放其他owner；未来Network另审适配，只在真实持帧参与Frame账本，不在本轮注册。
- AC-05：Return ACK入队/消费、资格确认、业务ACK分别采集；D/envelope存在不等于传输或Release成功，不新增平行生命周期ACK协议。
- AC-06：当前UVC Cancel仅登记，真实Process/Abort完成才无源访问；未来复制DMA/零拷贝DMA每操作终止访问证据独立，身份标签不能替代硬件完成。

## 剩余设计前置条件

1. A中D共享字段的单写/读同步、两端退休协议及重放记录时限。
2. 普通Frame队列中已排队旧消息与生命周期RELEASE独立次序的组合证明。
3. 真正只读核验与准入关闭动作分离：关闭由资格阶段提交，查询不得暗中Stop/Release。
4. 生产字段/内部API变更清单、精确RAM/栈预算、完整错误码与同owner上下文合同。
5. 新方案与B2 ID23有界BUSY、ID31分段目标兼容，不伪造成功重启。

## 下一步最小范围

人工选择A或B并审其前置条件；随后只设计选定方案的精确接口、元数据布局与证据采集。另获生产授权后才实现准入、资源身份只读查询及绑定；不能在本轮偷偷接入Qualification released/idle或执行C2.2C。保留所有历史FAIL/BLOCKED/output。

本次Git Diff仅三个新增设计文档。提交后停止，等待人工审核。
