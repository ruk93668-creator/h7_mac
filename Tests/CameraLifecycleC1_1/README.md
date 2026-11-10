# C1.1 生命周期协议可靠性整改

基线C1 `b684b76`；分支 `codex/stage-C1-1-lifecycle-reliability`。

- [完整验收报告](verification_report.md)
- [443项协议测试结果](results.json)：原419 + 新增24。
- [29项回归门禁自测](regression_gate_results.json)
- [相对C1的28组严格回归](regression_results.json)：688项有效通过，历史失败无新增差异。
- [相对main的补充比较](main_comparison_results.json)：保留两组C1继承的历史夹具链接差异。
- [正式构建结果](build_results.json)、[CM7日志](h7_mac_CM7_rebuild.log)、[CM4日志](h7_mac_CM4_rebuild.log)。
- [范围与内存审计](scope_audit.json)

运行命令及限制见完整报告。没有新增业务测试入口；没有进入C2；实板未验证。
