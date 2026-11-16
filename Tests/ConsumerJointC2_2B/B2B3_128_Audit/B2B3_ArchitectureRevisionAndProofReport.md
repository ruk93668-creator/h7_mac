# Architecture Revision and Lifecycle Proof Report

## 基线与Git状态

本地起点9cf537d47c1673b05ee20dd8a0c6f5e97965b660，分支codex/stage-C2-2B-B2B6-v1-compat；只有原未跟踪output/。本次查询远端成功，远端为2b0df5a50a48b5aeb2c31f3bf8f2e7767a99dc8e，即既有c3350b0及9cf537d尚未确认备份。没有reset/rebase或覆盖提交；完成后普通push，最终结果另报告。

## 审核结论

| 项目 | 结论 |
|---|---|
| AC-01消费者身份化核验 | 正式纳入目标约束；当前固定两路不是已泛化实现 |
| AC-02 ACQUIRE身份 | 正式纳入；与RELEASE request和Frame Lease分别处理 |
| AC-03 RELEASE身份 | 正式纳入；受控窗口关联原grant，不等同request_id |
| AC-04 Media未来网络扩展 | 正式纳入；不扩展现有owner位或注册第三消费者 |
| AC-05 ACK语义 | 正式纳入六类完成/确认事件分离，网络接受不等于Camera ACK |
| AC-06 DMA所有权 | 正式纳入复制/零拷贝和多异步操作的源Frame终止访问证明 |
| Grant→Frame Lease | NOT_PROVEN，局部凭据链存在，但无完整grant关联 |
| 同grant入口关闭后不重开 | NOT_PROVEN，局部STOP门和竞入排空存在，完整窗口不变量缺证 |
| 当前新生产缺陷 | 未由本轮复现或证明；缺证不等于必然缺陷 |
| C2.2B/C2.2C | 最终验收未完成；不进入C2.2C |

## 与上一版API契约的调整建议

未来Camera入口应按消费者完整身份路由核验，不增加USB/Storage/Network专用Camera核心函数。模块内部适配可以保留资源职责，但核验模型不固定消费者种类。Ops当前接收ACQUIRE grant、RELEASE窗口由Qualifier验证，两者职责须明确；不能原样把示例RELEASE消息传给grant回调。

现有FIFOusb_owned/sd_owned不足以直接支持Network，生命周期协议也绑定两路。只是未来扩展约束，不授权重写Media或改变消息ABI；需要未来独立阶段提出最小适配。Control Plane存在不等于持Frame；Data Plane中的真实持帧和DMA引用才构成该帧所有权。

## 下一阶段最小范围

先补设计而非实施：完整grant→Bridge/Media/slot关联方案；身份化核验源适配；Camera侧准入/关闭的所有者与稳定窗口；跨队列迟到与ACK消费的组合证明；复制和零拷贝异步源Frame责任边界。能复用现有字段则不新增；必须新增时先列唯一owner、更新/失效、同步和RAM预算再审。不可通过全局HasLoan、Debug、通知位或超时强放行。

之后另行授权专项采集：真实FIFO源码/ELF来源核对、逐grant与lease关联、STOP/Offer/RELEASE/ACK交错、双消费者独立归还、重复/错误owner、旧身份与新帧竞争。软件与硬件分别记录，当前未运行任何测试，不改33项历史状态。

## 本轮交付及范围

仅新增ArchitectureConstraints_Revision、GrantLeaseProofAudit、OfferClosureProofAudit和本综合报告四份md。各事实对应Application/Tasks/Media/Service真实函数及行号，非完整源码行范围的结论另标NOT_PROVEN。没有修改生产/测试/判定器、任务配置或ABI，没有新增Replay配额、Network消费者或网络实现，没有删除output/历史FAIL/BLOCKED。M4 //等OLED稳定 和HAL_Delay(500);保留，生产代码零写入。

两项NOT_PROVEN没有改成PASS；本次只完成架构约束与源码证明边界审核，不等于API批准。提交后停止，等待人工复审。
