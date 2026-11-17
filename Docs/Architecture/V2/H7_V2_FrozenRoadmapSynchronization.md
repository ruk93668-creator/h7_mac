# 冻结路线同步与证据集合索引

日期2026-11-17；输入bd24c03242591a8f87ffa6e03cd2cba8048803db。该提交及6a96097、501330a完整保留。本次仅同步用户最后明确批准的路线，不重新设计五核心/目录，不授权代码实施。

## 旧编号到当前编号

| 旧文档阶段/内容 | 当前冻结落点 |
|---|---|
| 0 Architecture | 0 Architecture Freeze |
| 1 Core、2 Graph、3 Format、组合解析 | 1 Core + Composition（已实现纯基础） |
| 4 Resource Manager | 2 Media Resource |
| 5 Managed Lifecycle、9 Supervisor的执行职责 | 3 Execution Manager |
| 6 Camera / 7 USB / 8 Storage | 4 Camera / 5 USB / 6 Storage |
| 旧9中Control/IPC请求编排 | 7 Control Service，多传输入口，IPC非唯一 |
| 旧9中真实配置切换/补偿 | 8 Runtime Reconfiguration |
| 10 Network | 9 Network / AI Extension |
| 旧迁移计划最终清理 | 10 Legacy Cleanup |

按职责映射，不机械替换所有数字（例如B1 ID9、72场景编号、结构尺寸不能改）。阶段3可定义执行协调，但真实模块依赖各自阶段；阶段8集成切换不跳过资源排空。完整现行矩阵在MigrationPlan第2节。

## 修改范围与历史处理

MigrationPlan更新矩阵、依赖及文件退休落点；ApplicationArchitecture更正旧冻结声明；DirectoryStructure仅补阶段落点，目录树/依赖不改；根FinalReview保留历史研究并更正路线；Phase1 FinalReview仅替换已失效的阶段冲突判断，第1～5节源码审查保留。Phase2计划改为资源六工作包，接口/预算/矩阵同步编号。

Phase1已有Core/Registry/Resolver/Graph/Format/Composition不重复开发。单owner纯基础库仍有条件验收；两个NOT_PROVEN、未接BOOT、实板未验证、真实栈/WCET缺口保持。未获批准的API/容量仍PROPOSED/UNRESOLVED，路线名称的确认不能替代这些审批。

## 72与32集合核对

原集合A=R3-01～32（32）+J01～08（8）+C01～32（32），72个唯一ID，全部NOT_RUN；来源ResourceOwnershipAcceptance。新增集合B=P2-01～08（8）+R-01～24（24），32个唯一ID，全部NOT_RUN；来源Phase2_Resource_TestMatrix.csv。未重跑测试、未升级原C场景状态，即使Phase1的独立Mock有相近断言也不能标原端到端场景PASS。

ID命名空间交集为空，但语义覆盖存在多对多重叠。两表共104个追踪条目不等于104项独立契约/执行次数；去重后的完整可执行契约数量尚未裁定，不提供无依据的并集独立用例数。以下逐项关联为覆盖主题，不声称预置条件、故障注入及判据完全等价，也不删除任一历史条目。

| 新ID | 对应原72项ID | 关系 | 状态 |
|---|---|---|---|
| P2-01 | C09,C10 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| P2-02 | C12 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| P2-03 | C11 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| P2-04 | J01,J03,C03 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| P2-05 | J06 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| P2-06 | J08 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| P2-07 | J07,C16 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| P2-08 | C17 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-01 | R3-25 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-02 | C07 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-03 | R3-17 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-04 | R3-13,R3-14,R3-18 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-05 | R3-26 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-06 | R3-12 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-07 | R3-14,R3-16 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-08 | R3-05,R3-06,R3-09,C29 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-09 | R3-03,R3-07 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-10 | R3-02 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-11 | R3-04 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-12 | R3-10,R3-11,R3-12,J05 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-13 | R3-10 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-14 | R3-15,R3-16 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-15 | R3-21,R3-22 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-16 | R3-19 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-17 | R3-17,R3-18 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-18 | R3-19,R3-30 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-19 | R3-27,R3-28 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-20 | R3-29 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-21 | R3-31 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-22 | R3-23,R3-24 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-23 | R3-30 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |
| R-24 | R3-31,C31 | 部分覆盖/需保留原细粒度判据 | NOT_RUN |

后续执行建立独立Run ID与完整契约断言，只有同输入/前置/故障/判据被证明相同才可共享执行证据，不能靠标题去重或重复报PASS。原72条目与新增32条目的ID/状态本轮逐项检查，均无缺失/重复；原文件未改，新增表仅调整阶段标签。

## 本轮检查

仅文档Diff及静态一致性检查；不修改源码、测试、Keil、ABI、Replay、M4或main，不运行编译/实板。bd24c03的源码审核结论仍可追溯；新增独立提交正常推送，失败如实报告。
