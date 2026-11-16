# B2 Restart Decision Pending

基线2b0df5a。本次Replay/分消费者核验架构批准与B2重启行为隔离，ID23与31保持CONTRACT_REVIEW_REQUIRED；没有人工批准具体重启或FAULT复位行为。本文件不改原始契约和历史状态。

## B2/23：STOP START FIFO

原始来源：Tests/UsbModuleB2/behavior_test.c B2_TestRun scenario23：START10后Push STOP11再Push START12、推进四轮；run_tests.py以旧/新C入口及完整trace相等判定。该C分支无逐条消息断言，不能把return0看作操作成功证据。

当前路径：STOP11登记停止，Task继续Cancel/Process/Bridge/Process，FinishRound完成本地ModeStop后投递STOP结果并ConsumerEnd。RELEASE仍在途且grant保留，App_CameraConsumerBegin:18起拒绝非IDLE/FAILED或grant非零；START12得到BUSY，HandleCommand同步写入ACK结果及fault。不能把BUSY解读为硬件停止，也不能以新的Replay审批绕过Begin。

风险：旧“STOP后可马上重启”与当前保守资格清退不同；若测试直接清grant将掩盖资源风险，若仅检查START12 BUSY可能遗漏STOP ACK/FIFO或FAULT交付。待决：是否接受“本地STOP完成但RELEASE未完成期间，新START BUSY”的明确产品行为？若接受，需逐条独立ACK/FAULT表；若不接受，保持阻塞并单独设计等待/排队策略，不在本轮改生产。建议仅接受有界差异，但建议不是批准。

## B2/31：Fault report reset on START

原始来源同文件scenario31：START10后重复START11；再STOP12；再START13。原目标包括真正成功START后清除fault并复位fault_reported，不是只有FIFO。

当前START11产生BUSY；STOP12本地停止后资格RELEASE未完成；START13仍BUSY，因此不会经过App_USBProcess成功分支清fault/fault_reported及FPS起点。失败START未复位不能冒充成功重启后的正确复位。FAULT是否再次发送还受fault_reported影响，不能规定每次BUSY都必发一条新的FAULT。

风险：如果为了新契约只要求BUSY，可能删掉原故障复位目标；如果为了旧目标伪造释放，则破坏安全边界。待决：是否将本阶段验证拆为“未清退时拒绝重启”与“未来合法重启成功后的FAULT复位”，并明确后者尚未验收？不接受拆分则整项继续待审。建议拆分并保留原目标，仍待人工决策。

## 决策及证据记录要求

各自记录原/新命令序列、所有ACK/FAULT正文/成功及失败运输、身份、Mode调用、Consumer phase/grant、fault与fault_reported；不得仅比哈希自洽或最终返回。负对照建议：同grant未释放时伪改START OK、清grant、交换command_id、丢FAULT或无依据复位均须被识别。本轮未执行。

| 项目 | 审批状态 |
|---|---|
| Replay边界（不新增配额） | 已批准 |
| 分消费者核验方向 | 已批准 |
| 核验具体API | 待审核 |
| ID23重启BUSY行为 | 待单独人工决策 |
| ID31成功重启/FAULT复位目标拆分 | 待单独人工决策 |
| C2.2B最终验收 | 未完成 |
| C2.2C | 未进入 |

M4 //等OLED稳定 与HAL_Delay(500);保留。本文仅新增审核文档，不修改代码/测试，不运行测试，不合并main。
