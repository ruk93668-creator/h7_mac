# 原始与新联合测试对照

原 CameraModuleB1 代码及断言零修改。其原联合测试仍是0/11；新的独立测试不替代或覆写历史结果。旧夹具存在初始化/旧STOP契约问题，新测试不做逐事件相等比较。

| ID | 原场景 | 原联合测试 | 新联合测试 | 分类与说明 |
|---|---|---|---|---|
| 0 | 双消费者租约及AVI封尾 | FAIL（保留） | PASS | 独立数据面/故障注入验收，不覆盖旧断言 |
| 1 | 双段JPEG | FAIL（保留） | PASS | 独立数据面/故障注入验收，不覆盖旧断言 |
| 2 | Offer队列满重试 | FAIL（保留） | PASS | 独立数据面/故障注入验收，不覆盖旧断言 |
| 3 | Storage槽位占用 | FAIL（保留） | PASS | 独立数据面/故障注入验收，不覆盖旧断言 |
| 4 | Storage写入失败 | FAIL（保留） | PASS | 独立数据面/故障注入验收，不覆盖旧断言 |
| 5 | SD归还失败重试 | FAIL（保留） | PASS | 独立数据面/故障注入验收，不覆盖旧断言 |
| 6 | 双租约期间旧Camera STOP拒绝 | FAIL（保留） | PASS | A：新生命周期及独立验收契约；ID6验证拒绝旧STOP |
| 7 | USB取消失败 | FAIL（保留） | FAIL | C：后台停止成功遗漏USB RELEASE意图 |
| 8 | Return ACK队列满 | FAIL（保留） | PASS | 独立数据面/故障注入验收，不覆盖旧断言 |
| 9 | 重复Return | FAIL（保留） | PASS | 独立数据面/故障注入验收，不覆盖旧断言 |
| 10 | 错误Lease Return | FAIL（保留） | PASS | 独立数据面/故障注入验收，不覆盖旧断言 |

新ID7首个契约违例是Return ACK完成、USB_Mode_Stop成功后没有USB RELEASE请求；不是新增异步事件本身。详见[生产问题](Defects.md)。

[完整因果契约与序号](contracts.json)，[原始事件](Evidence/0.json.gz)。新测试中Storage在原帧归还后再STOP；“STOP与在途帧交错且文件封尾先于Return Pending”的完整链在本联合集中为NOT_RUN，不能由这些结果推定通过。原B3相关ID继续列入153项证据门禁。
