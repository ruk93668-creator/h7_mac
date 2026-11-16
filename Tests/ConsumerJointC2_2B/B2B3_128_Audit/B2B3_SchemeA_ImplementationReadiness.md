# Scheme A Implementation Readiness

## 授权和基线

人工正式批准方案A的精确设计，不批准生产实施。当前起点66d3a0b545d1b2bf80d215878964d349f830100b；分支codex/stage-C2-2B-B2B6-v1-compat。最近66d3a0b、14bbdcc、9cf537d、c3350b0为连续文档提交。git diff 14bbdcc 66d3a0b只含三份设计文件，生产源码树一致；本轮以66d3a0b实际源码为准，不把文档提交当生产变更。开始时仅未跟踪output/，不清理。远端状态在交付消息注明。

本轮只新增指定五份文档，不改生产/测试/判定器/消息ABI/任务/Replay，不运行测试或编译。M4 //等OLED稳定 和HAL_Delay(500);完整保留，所有生产文件零写入。

## 证据等级

SOURCE_CONFIRMED：原Qualifier RELEASE先置RELEASING，Ops released/idle当前NULL；Bridge单请求/ACK队列/Return流程；Storage槽位同步；Media双owner账本。DESIGN_DEDUCED：本轮D单写协议、闭合与只读查询草案。RUNTIME_PROVEN：本轮无；O0/O2/ARMCC5/实板均NOT_RUN。Grant关联和稳定关闭整体依然NOT_PROVEN，不能将设计说明当验收通过。

## 生产字段建议（不实施）

| 记录 | 字段原始预算 | 唯一写者 |
|---|---:|---|
| U发布记录 | G16 + H4 + publication_phase4 + committed4 + consumed_H4 = 32B | USB |
| C当前证据 | G16 + H4 + phase4 + B12 + M8 + flags4 = 48B | Camera |
| Frame终结拒旧F | 4B | Camera |
| 原last_ack所属G | 16B | Camera |
| 每StorageSlot G | 16B * S | Camera Offer，FREE后才能覆盖 |
| Media快照scratch | 20+7*12=104B | Camera调用者持有；只读内容 |

元数据总原始预算100+16*S；S=1为116B，若采用静态104B scratch则220B。该数字取代上轮约80B的粗估：本轮增加两端独立进度、消费握手和F字段。uint32/32位指针按4对齐是假设，不是ARMCC5实测；append到原结构的padding、符号位置和编译器临时栈需后续验证。无新增Qualification open/closed副本，不新增引用count。

每个新字段需完整offsetof/sizeof清单和基线Map比较；不调任务栈凑通过。静态scratch避免直接在CameraTask堆104B大局部，但单owner无重入是必要前提。U/C/G一致副本可能增加局部原始字节，需编译器HTM实际调用深度核对；不能仅将原始字段数当栈增量上界。不得将snapshot复制成第二份长期业务状态。

## 内部API草案及职责

以下仅签名意图，尚不声明为真实头文件API：

- `Bridge_PublishRequestGrant(const GrantKey *g, uint32_t h)`：USB owner，前D已退休，锁内发布U，不发送消息。
- `Bridge_ReadRequestGrant(uint32_t h, GrantKey *out)`：Camera owner，锁内一致读取U，缺失/冲突拒绝；不会从当前grant补齐。
- `Bridge_RecordTerminal(...)`：Camera owner，真实终结消息成功入队后提交同H的C/F并提示USB；不是资源释放函数。
- `Bridge_TryRetireRequest(uint32_t h)`：USB owner，结合C/F与consumed_H满足完整终结条件，锁内只改U；不操作Camera字段。
- `App_CameraReadActiveGrant(consumer_id, GrantKey *out)`与`App_CameraCanAdmitGrant(const GrantKey *g)`：Camera owner，读取唯一Qualifier ACTIVE/grant_*，不改变状态。现有GetStatus不含grant_request，不能假装足以取得完整G；需要新增只读访问契约。正常调用点在Bridge阶段，Qualifier Process已返回，避免其api_busy重入。
- Dispatcher至Storage Offer的内部受控适配显式携带const GrantKey，slot RESERVING绑定；保留原公开调用安全门，任何旧不带G的生产旁路都应被列入迁移/拒绝设计，不能留下可绕准入路径。是否调整公开C函数签名须另审，消息布局不变。
- `CameraMjpegFifo_ReadLoanIdentities(out)`：最小只读Media草案，详见专文；不能在Application裸读Media私有字段。

统一Camera核验按consumer identity路由资源证据，不增加固定USB/Storage/Network专用Camera核心API。当前只实现设计中的两个适配，未来Network不在本轮接入；接口数量及类型布局须人工复审。

## Qualification Ops与任务上下文

建议保留released(context, const AppLifecycleIdentity_t *grant)语义和现有签名；R窗口/正确G由Qual_Execute验证，资源适配仅查询G已归还事实。关闭线性化是回调之前原RELEASING写点，不能在只读查询中偷偷关闭/Release。资源回调不得再次调用会Qual_Enter的核心API造成重入；它应只读取交接来源和Media快照，不从GetStatus重构R。

目前两个NULL回调原样保留，本设计不把它们接上，也不声明生产退出已成功。idle真实证明及完整Shutdown仍需另审；普通停止唯一入口保持FinishRound，原保护性停止能力不变。

临界区只保护小型字段快照，保存原PRIMASK、禁中断、屏障、写发布字段/读一致副本、屏障、恢复；所有队列/UVC/Media/FS调用锁外。禁止新同步等待，ISR不写D；不假定USB高优先级消除了send后的抢占。单Camera所有者保证Qualifier关闭与实际取Frame/Retain之间不被任务重入，其他共享slot按原锁。

## 18项并发/异常设计

ConcurrencyMatrix覆盖用户要求的15场景，另含发送后抢占、错误owner、SD-only；每项列G/R/B/M/S、owner、D/slot阶段、队列、允许转换、拒绝和恢复。所有新增机制仍NOT_PROVEN，不把矩阵完成当运行通过。元数据只有身份与进度，不是第二套所有权计数。

## 下一阶段专项及门禁（尚未执行）

O0/O2分别验证U发布先于队列、send失败原H重试、Camera早于USB commit字段记账、USB早于Camera TERMINAL记账的抢占、已提交取消、负Offer消费、同H正文冲突、F拒旧、D复用旧ACK/Return、StorageRetain/STOP竞入、真实M及双owner、SD-only、文件/Release/Stop失败。使用真实Media实现，不用同名桩当只读查询证据；新增负对照去掉任一屏障/身份/退休条件应被独立判定识别（只模拟合法任务交错，不声称可覆盖全部硬件行为）。

之后须ARMCC5两核编译、Map/ABI/RW/ZI/调用深度与原断言回归；本轮无授权运行这些步骤。B2 ID23保持有界BUSY，ID31当前拒绝与未来成功重启复位拆分目标不变，不造成功START。

## 实施准备结论

D的发布/提交/终结消费/退休及迟到拒绝已有完整候选契约；G到M绑定及全部入口门已明确，需逐路径实施后采证。仍须人工审核：U/C具体布局与读取函数、F拒旧和负终结消息策略、公开Storage旧入口迁移方式、非JPEG处理边界、精确静态/栈预算。这些均无现成运行证据，整体NOT_PROVEN，不据此自动进入生产代码或C2.2C。只提交五份文档后停止。
