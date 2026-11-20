> VERIFIED：2026-11-20 P1-C1 修订补充见 [C1实施报告](USB_BulkOUT_C1_ImplementationReport.md)、[当前HAL与硬件映射](USB_BulkOUT_C1_HALHook_Contract.md)、[当前停止/NAK/RX契约](USB_BulkOUT_C1_StopProof_Contract.md)、[最终软件验证](USB_BulkOUT_C1_SoftwareVerification.md)。以下 C0 正文保留原基线、行号和当时阻塞结论，不代表已实施 C1 后的现状；尚未证明条件以 [C1清单](USB_BulkOUT_C1_RemainingRisks.md) 为准。

# P1-C — 尚未证明的硬件及软件条件

VERIFIED：本清单为C0门禁结果，不是可以忽略的测试备注。硬件基本位定义及停止顺序已由RM0399核验；以下尚不能仅用当前公开HAL接口形成C1安全证明。

| ID | 未闭合条件 | 证据/风险 | 必要闭合方式 |
| --- | --- | --- | --- |
| C0-B1 | Global OUT NAK唯一owner | HAL :1148 CGONAK、:1464 SGONAK绕过BSP owner；其他OUT可解除本屏障 | 统一所有写入者参与lease；申请接口扩展 |
| C0-B2 | 同一次RX pop/copy绑定身份与引用 | HAL :1084 pop、:1092 copy、:1095移动指针，上层回调已太迟 | 在真实消费者内取得不可变status与copy前后引用证据 |
| C0-B3 | 终止事件W1C前归属 | HAL :1127/1164提前清事件；正常XFRC回调可能重Arm | 终止事件pre-clear通知与单一收尾仲裁 |
| C0-B4 | Global NAK marker的消费水位与跨端点退休 | SETUP能越过NAK继续入队，不能用全RX空或任意pop替代 | 单消费者序号、目标proof、EP0继续服务 |
| C0-B5 | Reset/Disconnect与原引用独立 | HAL先RX后Reset，现有BSP只为Bulk IN提供完整身份机制 | Reset前失效发布、旧ledger保留、不可提前释放 |
| C0-B6 | 请求及Disable的真实最大时间 | 现HAL LL循环次数不等于HAL_GetTick超时、无完整EPDISD证据 | Task分阶段deadline、冻结tick保守上限、超时保留 |
| C0-B7 | 芯片实际OTG core GSNPSID与硅片细节 | C0模型分别用310A/300A；没有新读硬件 | 将来单独批准实板证据；不以DBGMCU REV_ID替代OTG core ID |
| C0-B8 | 失败后同身份Reconcile | 未能证明所有NAK写入和RX历史均连续 | 原lease/Attempt及新增真实证据；不可创建新Transfer清错 |

UNRESOLVED：没有声称不修改HAL在数学上绝无实现可能；结论是当前没有安全的独占分发/所有权证明。为了绕过门禁而复制HAL RX/EP0/ISO处理器、mask全部中断、手动pop或强清busy均未实施。

## 用户要求的C1专项状态

| 场景 | 本轮实际覆盖 | C1验收状态 |
| --- | --- | --- |
| EP2正常完成后Stop；接收中Stop | C0观察普通HAL完成/队列copy | NOT_TESTED，新Stop API未实现 |
| RXFLVL已产生未处理 | VERIFIED软件探针：DAINT mask不阻止copy，GRXSTSP不能重复读 | NOT_TESTED C1 Commit |
| XFRC/Stop竞争、重复Stop/Commit、旧事件迟到 | VERIFIED当前HAL W1C/callback顺序；INFERRED未来身份契约 | NOT_TESTED C1仲裁 |
| GONAKEFF超时 | VERIFIED marker不消费时有效位不会凭请求出现的模型；无Task Stop deadline代码 | NOT_TESTED C1超时 |
| EPDISD超时 | VERIFIED旧LL Abort迭代失败行为；不当作EPDISD超时证明 | NOT_TESTED C1超时 |
| 其他Global NAK使用者 | VERIFIED HAL ISO请求/任意OUT解除的软件分支 | UNRESOLVED C0-B1 |
| EP0 SETUP并发 | VERIFIED专用Setup copy、NAK marker后SETUP仍可消费的模型 | NOT_TESTED C1与实板 |
| EP1/EP2共享RX数据 | VERIFIED实际HAL分别消费、单次pop语义 | NOT_TESTED安全退休 |
| Reset/Disconnect交错 | VERIFIED RX先于Reset源码/探针；Disconnect调用链审计 | NOT_TESTED C1恢复 |
| 错误保留Buffer、同身份恢复 | VERIFIED旧LL失败保持数据；INFERRED未来ledger契约 | NOT_TESTED C1新接口 |
| IN在OUT Stop期间继续工作 | VERIFIED当前HAL可同次IRQ服务IN及RX；本轮没有真正OUT Stop | NOT_TESTED C1并行停止 |

NOT_TESTED：所有烧录、USBPcap实板、真实插拔、真实DMA/缓冲区硬件静止、MSC整机重连。本轮不因软件探针通过而把本表改为PASS。
