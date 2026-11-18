# Phase 7 最终验收

以 GitHub 与本地一致的 Phase 6 `6b1a835` 开始。独立 Control Service 与传输/执行接口完成，实际 Composition / Execution Manager 参与软件执行。O0/O2 52/52，8/8 负对照检出。Phase 1～6、Replay、C1/C1.1、52B/56B ABI 实际复验通过。原 CM7/CM4 Target 完整构建各 0 Error、0 Warning。

真实线协议、认证策略、物理 M4/USB/BLE/Wi-Fi 入口、Keil 与 BOOT 接入和实板验收均 NOT_RUN。现有 IPC V3 只保留状态能力。Grant → Frame Lease 与 Stable Admission Closure 保持 NOT_PROVEN。Phase 8 未开始。

生产变更限 `CM7/ApplicationV2/Core/Runtime/control_*.c/.h`。Phase 1～6、Legacy、Keil、Replay、M4 OLED 与 main 保持。完整 Diff 保存在 `output/V2Phase7/Phase7_Full.diff`。

- [实施与命令契约](Phase7_ImplementationReport.md)
- [测试报告](Phase7_TestReport.md)
- [资源预算](Phase7_ResourceBudget.md)
- [Legacy 适配与 IPC V3 审计](Phase7_LegacyAdaptationPlan.md)
