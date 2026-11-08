# SDMMC Stage08B

- [设计、接口及实板验收](../../Docs/Design/SDMMC1_Stage08B.md)
- [验收报告](verification_report.md)
- 实际BSP/HAL与原08A断言：`python Tests/SdmmcStage08B/run_tests.py`
- 写测试编译保护：`python Tests/SdmmcStage08B/run_policy_checks.py`
- 历史回归（保留已知失败）：`python Tests/SdmmcStage08B/run_regressions.py`
- 失败专项基线对照：`python Tests/SdmmcStage08B/compare_baseline.py`
- 同一正式工程三种配置全量构建：`python Tests/SdmmcStage08B/run_profiles.py`
- 当前本机配置两核全量构建：`python Tests/SdmmcStage08B/run_build.py`

脚本不创建新的Keil工程。profile构建临时替换测试宏，finally恢复原项目字节。硬件写测试默认关闭，编译用合成CID不代表真实卡授权。output/和tmp/保留但不提交。
