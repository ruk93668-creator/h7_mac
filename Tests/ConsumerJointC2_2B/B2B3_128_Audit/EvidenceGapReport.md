# Evidence Adjudication 第一阶段只读调查

调查基线：a0b82d5593bc7064a909dc8f40a6d354440af3c8。当前分支 codex/stage-C2-2B-B2B6-v1-compat；开始时仅有原未跟踪 output/。本轮只读取源码及既有证据，仅新增此调查文档；未修改工具、生产代码或原始结果，未运行测试或构建。

## 调查结论

上一轮“0仍缺软件证据的BLOCKED”不能作为完整验收结论。已生成的契约检查通过，仅说明该检查集合通过，不代表遗漏的契约得到证明。原34 PASS/119 CONTRACT_CHANGED、306/306及所有历史结果仍保留其原始记录身份，本轮不改写。

### 1. CONTRACT_CHANGED 分类过宽

report_inventory.py:52 将 trace不等且非current_only统一归为CONTRACT_CHANGED；没有先验证每项差异究竟是批准的契约变化、证据遗漏还是错误。第70行统一写 current_first_contract_violation=None 和 software_verifiable=True；summary将remaining_software_BLOCKED固定为0。它们缺少逐项证据闭包检查，不能作为阻塞已消除的证明。应增加独立的证据充分性判定，缺少证据优先BLOCKED，只有逐项确认新旧语义映射才能标CONTRACT_CHANGED；人工批准与软件分类必须分列。

### 2. B3 Frame Lease 证据范围

Tests/StorageModuleB3/behavior_test.c 中 CameraMjpegFifo_RetainFrame/ReleaseLease 是夹具实现：操作 sd_owned/retain_count/release_count 并Trace(20/21)，没有执行生产Media/FIFO账本。evaluate.py:94附近检查事件owner为Camera，112附近检查重试次数及最后槽位状态；只能证明真实App_Storage调用受控依赖的契约，不能证明生产CameraTask实际完成Media Lease归还。Records已写明mock边界，但SourceMapping/断言名称中的actual SD release容易造成过度判定。需用真实联合证据关联session/generation/stream_epoch/lease_id、Camera实际执行和Return ACK/槽位FREE；若关联证据不存在，相关实际归还结论BLOCKED，不得实现C2.2C回调来迎合验收。

运行记录中存在Trace21的B3 ID：13,14,15,16,17,18,24,25,26,27,28,29,32,33,34,35,36,37,38,39,40,41,42,48。此列表仅是已观察到桩归还事件的定位集合，不等于人工33项清单，也不宣称其他ID无需归还证明。

### 3. B2 ACK/FAULT 映射

evaluate.py:38核对完整正文与原运输桩记录的hash/result/source，自洽不等于符合预期业务语义；FIFO检查仅校验ACK command_id属于已消费命令及顺序。第84行附近计算baseline_msgs与candidate成功消息是否相同并赋给same_business，但该值只输出，没有统一作为失败条件。少数专项有期望result/FAULT检查，不能推广到每个ID。需从原场景提取独立预期ACK/FAULT表，检查消息类型、source/channel、command_id、result、次数及失败重试；合法架构差异单独白名单并给依据，不可仅由trace不同自动认可。

### 4. RTOS 等待/Stack/Priority

UsbTask源码：2048words、priority4；CameraTask：1536words、priority3；StorageTask：2048words、priority1。Camera/USB BOOT前无限等待，BOOT后20ms；Storage BOOT后100ms、BOOT前无限等待。确切调用见对应Task的xTaskCreateStatic及App_EventsWait。旧B2夹具Trace2记录stack/priority、Trace6记录等待ms，但evaluate.py未逐项建立预期值检查。Map中的静态栈与已知调用深度不证明真实优先级/等待语义或实际运行水位；Resources.json也标记all_indirect_stack_proven=false。需要独立创建参数、BOOT前后等待和高水位采样证据，动态栈上界及抢占仍需实板，不能由Map判PASS。

### 5. 负对照数量

test_gate.py现有10个调用check的负对照，最后Independent retry original evidence是正对照，共11项自检，不能称11项负对照全部有效。也未覆盖“ACK正文和运输hash一起被改成相同错误结果”“桩归还被误认真实归还”“RTOS等待/优先级错误”等遗漏。第二阶段应增加针对这些漏洞的负对照，不删除原有控制。

## 33项对齐状态及待补信息

已检索Tests/Docs中人工审查、33项缺口、EvidenceGap/Adjudication等记录，没有找到带具体ID和意见的33项独立清单。现有原始缺证说明主要是129项共用的旧Python谓词未映射和10项接口证据缺失，不能据此推导用户人工发现的33项。需要用户提供33项测试ID/审核意见或报告路径，再逐条建立对应表。目前33项解决数量无法确认，不能记0缺口或已关闭。

## 后续边界与当前答复

本轮未修改判定工具，未复跑O0/O2、负对照、CM7/CM4，全部本轮执行状态NOT_RUN，不能以旧PASS替代。没有人工批准记录支持118项中的任何一项已获认可，当前有据可确认认可数量为0（不代表用户否定）。新的BLOCKED精确数量待33项对齐和修订工具后重算，不能复用上一轮0。暂不具备宣布进入C2.2C实施的验收依据。

下一步仅修测试工具：逐ID证据充分性优先、独立ACK/FAULT预期、跨联合证据关联、RTOS参数门禁和新增负对照；不修改生产、接口、ABI、运行配置或历史断言。本调查完成后等待审核及33项清单，尚不生成冒充已执行的复验报告。

## 用户补充清单后的逐项复核

已收到2026-11-15分组审核记录：B3租约21项、B2 ACK/FAULT 5项、RTOS 7项，共33项。此前“没有找到独立清单”是收到补充前的调查状态，现在以用户提供ID为对齐依据，非独立逐项签核。

新增 B2B3_33_EvidenceGapReview.csv，每行对照实际原断言和O0/O2原始输出，保存原始文件SHA、归还入口身份和owner、桩最终sd_owned（可读取时）、完整ACK/FAULT消息及运输返回、原任务入口与现有事件。仅复核既有证据，不执行新测试；不修改判定工具。33项均暂列审查BLOCKED，表示补证目标尚未通过独立判定闭包，不是33项生产缺陷或33项测试实际FAIL。全153项重新裁决数量尚未生成，不把本清单计数冒充完整门禁重算。

现有B3桩可能已记录ReleaseLease和最终sd_owned=0，但不是生产FIFO执行证明；ID34有原CHECK(sd_owned)用于错误owner尝试后的保持，之后Camera owner归还仍需区分。B2五项存在消息及部分结果检查，不说它们“完全无证据”，但缺完整独立期望与抗错误判定证据。RTOS部分场景只有运行或创建，不能要求同一case同时覆盖不存在的动作，更不能省略其应验证的动作。

目前确认33项已定位33、独立补证关闭0、审查BLOCKED33；人工认可0（未获签核），新O0/O2和构建NOT_RUN。下一步先审核此逐项表，再仅修测试工具和证据判定。保持不进入C2.2C。
